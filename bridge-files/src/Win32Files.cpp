#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include <Win32Files.hpp>

#include <FileBridgeAtomicWrite.hpp>
#include <FileBridgePaths.hpp>
#include <FileBridgeText.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <utility>

namespace UE4SSLuaFileBridge::Win32
{
namespace
{
using NativeContract::ErrorCode;

// FOLDERID_LocalAppData and FOLDERID_Profile, defined here so no GUID library
// is needed.
const GUID local_app_data_id{0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};
const GUID profile_id{0x5E6C858F, 0x0E22, 0x4760, {0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73}};

std::wstring widen(std::string_view utf8)
{
    const auto wide = Core::utf8_to_utf16(utf8);
    if (!wide) return {};
    return std::wstring(wide->begin(), wide->end());
}

std::string narrow(std::wstring_view wide)
{
    const auto utf8 = Core::utf16_to_utf8(std::u16string(wide.begin(), wide.end()));
    return utf8 ? *utf8 : std::string{};
}

// Lossy: unpaired surrogates become U+FFFD.
std::string narrow_lossy(std::wstring_view wide)
{
    std::wstring copy(wide);
    for (std::size_t i = 0; i < copy.size(); ++i)
    {
        const auto c = copy[i];
        if (c >= 0xD800 && c <= 0xDBFF)
        {
            if (i + 1 < copy.size() && copy[i + 1] >= 0xDC00 && copy[i + 1] <= 0xDFFF) { ++i; continue; }
            copy[i] = 0xFFFD;
        }
        else if (c >= 0xDC00 && c <= 0xDFFF)
        {
            copy[i] = 0xFFFD;
        }
    }
    return narrow(copy);
}

// "C:/a/b" -> "\\?\C:\a\b".
std::wstring system_path(const std::string& path)
{
    std::wstring wide = widen(path);
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    return L"\\\\?\\" + wide;
}

std::string from_system_path(std::wstring_view wide)
{
    return Core::normalize_system_path(narrow(wide));
}

bool has_read_only_attribute(const std::string& path)
{
    const DWORD attributes = GetFileAttributesW(system_path(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
}

Failure win32_failure(std::string_view operation, const std::string& path, DWORD code)
{
    ErrorCode mapped = Core::map_win32_error(static_cast<uint32_t>(code));
    std::string reason;
    if (code == ERROR_ACCESS_DENIED && has_read_only_attribute(path))
    {
        mapped = ErrorCode::read_only;
        reason = "the file is read-only";
    }
    else
    {
        switch (mapped)
        {
        case ErrorCode::not_found: reason = "not found"; break;
        case ErrorCode::denied: reason = "Windows refused access"; break;
        case ErrorCode::exists: reason = "already exists"; break;
        case ErrorCode::read_only: reason = "the volume is write-protected"; break;
        case ErrorCode::busy: reason = "in use by another process"; break;
        case ErrorCode::invalid:
            reason = code == ERROR_DIR_NOT_EMPTY ? "the folder isn't empty" : "wrong kind of entry";
            break;
        default: reason = "system error"; break;
        }
    }
    return Failure{mapped, std::string(operation) + " " + path + ": " + reason + " (win32 " + std::to_string(code) + ")"};
}

Failure last_failure(std::string_view operation, const std::string& path)
{
    return win32_failure(operation, path, GetLastError());
}

class Handle
{
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    [[nodiscard]] bool valid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
    [[nodiscard]] HANDLE get() const { return handle_; }
    HANDLE release() { return std::exchange(handle_, INVALID_HANDLE_VALUE); }
    void reset()
    {
        if (valid()) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// `total` receives the bytes written, also when it fails part-way.
Status write_all(HANDLE handle, std::string_view data, std::string_view operation, const std::string& path,
    std::size_t* total = nullptr)
{
    if (total) *total = 0;
    while (!data.empty())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size(), 1u << 30));
        DWORD written = 0;
        if (!WriteFile(handle, data.data(), chunk, &written, nullptr)) return last_failure(operation, path);
        if (written == 0) return Failure{ErrorCode::io, std::string(operation) + " " + path + ": nothing was written"};
        data.remove_prefix(written);
        if (total) *total += written;
    }
    return Done{};
}

int64_t ticks(const FILETIME& time)
{
    return static_cast<int64_t>((static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
}

std::wstring module_file_name(HMODULE module)
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size())
        {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

std::string known_folder(const GUID& id)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) || !raw)
    {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    const std::string path = from_system_path(raw);
    CoTaskMemFree(raw);
    return path;
}

std::atomic_uint64_t temp_counter{1};

class Win32AtomicOps final : public Core::AtomicReplaceOps
{
public:
    // `source` set: fill the temporary file by copying it; otherwise write `data`.
    Win32AtomicOps(std::string_view data, std::string source) : data_(data), source_(std::move(source)) {}

    Status fill_temp(const std::string& temp) override
    {
        if (!source_.empty())
        {
            if (!CopyFileExW(system_path(source_).c_str(), system_path(temp).c_str(), nullptr, nullptr, nullptr,
                    COPY_FILE_FAIL_IF_EXISTS))
            {
                return last_failure("Copy", source_);
            }
            const auto wide = system_path(temp);
            const DWORD attributes = GetFileAttributesW(wide.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0)
            {
                SetFileAttributesW(wide.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
            }
            Handle handle(CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!handle.valid()) return last_failure("Copy", temp);
            if (!FlushFileBuffers(handle.get())) return last_failure("Copy", temp);
            return Done{};
        }
        Handle handle(CreateFileW(system_path(temp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!handle.valid()) return last_failure("WriteText", temp);
        auto written = write_all(handle.get(), data_, "WriteText", temp);
        if (!written) return written;
        if (!FlushFileBuffers(handle.get())) return last_failure("WriteText", temp);
        return Done{};
    }

    Outcome<bool> target_exists(const std::string& target) override
    {
        const DWORD attributes = GetFileAttributesW(system_path(target).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) return true;
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return false;
        return win32_failure("WriteText", target, code);
    }

    Core::ReplaceResult replace(const std::string& target, const std::string& temp, const std::string& backup) override
    {
        const auto backup_path = backup.empty() ? std::wstring{} : system_path(backup);
        if (ReplaceFileW(system_path(target).c_str(), system_path(temp).c_str(),
                backup.empty() ? nullptr : backup_path.c_str(), 0, nullptr, nullptr))
        {
            return Core::ReplaceResult{};
        }
        const DWORD code = GetLastError();
        return Core::ReplaceResult{win32_failure("Replace", target, code),
            Core::classify_replace_error(static_cast<uint32_t>(code), !backup.empty())};
    }

    void pause() override
    {
        Sleep(50);
    }

    Status rename_new(const std::string& temp, const std::string& target) override
    {
        if (!MoveFileExW(system_path(temp).c_str(), system_path(target).c_str(), MOVEFILE_WRITE_THROUGH))
        {
            return last_failure("Rename", target);
        }
        return Done{};
    }

    void discard(const std::string& temp) override
    {
        DeleteFileW(system_path(temp).c_str());
    }

private:
    std::string_view data_;
    std::string source_;
};

Status atomic_into(const std::string& target, std::string_view data, std::string source, const std::string& backup,
    bool overwrite)
{
    Win32AtomicOps ops(data, std::move(source));
    const auto temp = Core::temp_sibling(target, process_id(), temp_counter.fetch_add(1));
    return Core::atomic_replace(ops, target, temp, backup, overwrite);
}
}

bool equal_fold(std::string_view a, std::string_view b)
{
    const bool ascii = std::all_of(a.begin(), a.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }) &&
        std::all_of(b.begin(), b.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    if (ascii) return Core::ascii_equal_fold(a, b);
    const auto wa = widen(a);
    const auto wb = widen(b);
    return CompareStringOrdinal(wa.c_str(), static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size()), TRUE) ==
        CSTR_EQUAL;
}

Roots discover_roots(const void* address_in_module)
{
    Roots roots;
    const auto exe = from_system_path(module_file_name(nullptr));
    auto segments = Core::split_segments(exe);
    // <game>/<project>/Binaries/Win64/<exe>
    if (segments.size() < 5 || !equal_fold(segments[segments.size() - 2], "Win64") ||
        !equal_fold(segments[segments.size() - 3], "Binaries"))
    {
        roots.error = "the game executable isn't in <project>/Binaries/Win64: " + exe;
        return roots;
    }
    roots.project = std::string(segments[segments.size() - 4]);
    std::vector<std::string> game(segments.begin(), segments.end() - 4);
    roots.game = Core::join_segments(game);

    const auto local = known_folder(local_app_data_id);
    if (local.empty())
    {
        roots.error = "the local application data folder is unknown";
        return roots;
    }
    roots.user = Core::child_path(local, roots.project);
    roots.profile = known_folder(profile_id);

    const std::string fallback =
        roots.game + "/" + roots.project + "/Binaries/Win64/ue4ss/Mods";
    roots.mods = fallback;
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(address_in_module), &module))
    {
        // Mods/<bridge folder>/dlls/versions/<dll>
        auto dll = Core::split_segments(from_system_path(module_file_name(module)));
        if (dll.size() > 5)
        {
            std::vector<std::string> mods(dll.begin(), dll.end() - 4);
            const auto candidate = Core::join_segments(mods);
            if (Core::is_within(candidate, roots.game, &equal_fold)) roots.mods = candidate;
        }
    }
    roots.ok = true;
    return roots;
}

Outcome<std::string> Resolver::resolve(const std::string& lexical, bool follow_leaf)
{
    if (!follow_leaf)
    {
        const auto segments = Core::split_segments(lexical);
        if (segments.size() <= 1) return lexical;
        auto parent = resolve(Core::parent_path(lexical), true);
        if (!parent) return parent;
        return Core::child_path(parent.value(), segments.back());
    }
    const auto segments = Core::split_segments(lexical);
    for (std::size_t count = segments.size(); count >= 1; --count)
    {
        std::vector<std::string> prefix(segments.begin(), segments.begin() + static_cast<std::ptrdiff_t>(count));
        const auto path = Core::join_segments(prefix);
        Handle handle(CreateFileW(system_path(path).c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!handle.valid())
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND || code == ERROR_INVALID_NAME ||
                code == ERROR_BAD_NETPATH || code == ERROR_NOT_READY || code == ERROR_CANT_RESOLVE_FILENAME)
            {
                if (code == ERROR_CANT_RESOLVE_FILENAME)
                {
                    return Failure{ErrorCode::io, path + ": too many levels of links"};
                }
                continue; // try the parent
            }
            return win32_failure("Resolve", path, code);
        }
        std::wstring buffer(512, L'\0');
        DWORD length = GetFinalPathNameByHandleW(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length >= buffer.size())
        {
            buffer.resize(length + 1);
            length = GetFinalPathNameByHandleW(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        }
        if (length == 0 || length >= buffer.size()) return last_failure("Resolve", path);
        buffer.resize(length);
        auto real = from_system_path(buffer);
        if (real.empty()) return Failure{ErrorCode::outside_root, path + " resolves to a network or device path"};
        for (std::size_t i = count; i < segments.size(); ++i) real = Core::child_path(real, segments[i]);
        return real;
    }
    return lexical; // nothing exists, not even the drive: callers report not_found
}

Outcome<Probe> probe(const std::string& path)
{
    const DWORD attributes = GetFileAttributesW(system_path(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return Probe{};
        return win32_failure("Probe", path, code);
    }
    Probe result;
    result.kind = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? Kind::directory : Kind::file;
    result.link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    result.read_only = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    return result;
}

Outcome<StatResult> stat(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(system_path(path).c_str(), GetFileExInfoStandard, &data)) return last_failure("Stat", path);
    StatResult result;
    const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    result.type = directory ? "directory" : (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0 ? "other" : "file";
    result.size = directory ? 0 : static_cast<int64_t>((static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    result.modified = ticks(data.ftLastWriteTime);
    result.created = ticks(data.ftCreationTime);
    result.read_only = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    return result;
}

Outcome<std::vector<Core::ListEntry>> list(const std::string& path, std::size_t& undecodable)
{
    undecodable = 0;
    const auto kind = probe(path);
    if (!kind) return kind.failure();
    if (kind.value().kind == Kind::missing) return Failure{ErrorCode::not_found, "List " + path + ": not found"};
    if (kind.value().kind != Kind::directory) return Failure{ErrorCode::invalid, "List " + path + ": not a folder"};

    std::vector<Core::ListEntry> entries;
    WIN32_FIND_DATAW data{};
    const auto pattern = system_path(path) + L"\\*";
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) return entries;
        return win32_failure("List", path, code);
    }
    do
    {
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        const auto utf8 = Core::utf16_to_utf8(std::u16string(name.begin(), name.end()));
        if (!utf8)
        {
            ++undecodable;
            continue;
        }
        Core::ListEntry entry;
        entry.name = *utf8;
        const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.type = directory ? "directory" : (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0 ? "other" : "file";
        entry.size = directory ? 0 : static_cast<int64_t>((static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
        entry.modified_filetime = ticks(data.ftLastWriteTime);
        entry.created_filetime = ticks(data.ftCreationTime);
        entry.link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        entries.push_back(std::move(entry));
    } while (FindNextFileW(find, &data));
    const DWORD code = GetLastError();
    FindClose(find);
    if (code != ERROR_NO_MORE_FILES) return win32_failure("List", path, code);
    return entries;
}

Outcome<std::string> read(const std::string& path, int64_t offset, int64_t length, int64_t maximum)
{
    const auto kind = probe(path);
    if (!kind) return kind.failure();
    if (kind.value().kind == Kind::missing) return Failure{ErrorCode::not_found, "Read " + path + ": not found"};
    if (kind.value().kind == Kind::directory) return Failure{ErrorCode::invalid, "Read " + path + ": is a folder"};
    Handle handle(CreateFileW(system_path(path).c_str(), GENERIC_READ, share_all, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.valid()) return last_failure("Read", path);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.get(), &size)) return last_failure("Read", path);
    const int64_t available = std::max<int64_t>(0, size.QuadPart - offset);
    const int64_t wanted = length < 0 ? available : std::min(length, available);
    const int64_t requested = length < 0 ? available : length;
    if (requested > maximum || wanted > maximum)
    {
        return Failure{ErrorCode::invalid, "Read " + path + ": more than max_read_bytes in one read"};
    }
    std::string content(static_cast<std::size_t>(wanted), '\0');
    if (wanted == 0) return content;
    LARGE_INTEGER position{};
    position.QuadPart = offset;
    if (!SetFilePointerEx(handle.get(), position, nullptr, FILE_BEGIN)) return last_failure("Read", path);
    std::size_t filled = 0;
    while (filled < content.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(content.size() - filled, 1u << 30));
        DWORD got = 0;
        if (!ReadFile(handle.get(), content.data() + filled, chunk, &got, nullptr)) return last_failure("Read", path);
        if (got == 0) break; // shrank meanwhile
        filled += got;
    }
    content.resize(filled);
    return content;
}

Status create_directory(const std::string& path)
{
    if (CreateDirectoryW(system_path(path).c_str(), nullptr)) return Done{};
    const DWORD code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS)
    {
        const auto kind = probe(path);
        if (kind && kind.value().kind == Kind::directory) return Done{};
        return Failure{ErrorCode::exists, "MakeDir " + path + ": a file is in the way"};
    }
    return win32_failure("MakeDir", path, code);
}

Status write_atomic(const std::string& path, std::string_view data, const std::string& backup)
{
    return atomic_into(path, data, {}, backup, true);
}

Status write_in_place(const std::string& path, std::string_view data)
{
    Handle handle(CreateFileW(system_path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.valid()) return last_failure("WriteText", path);
    return write_all(handle.get(), data, "WriteText", path);
}

Status append(const std::string& path, std::string_view data)
{
    Handle handle(CreateFileW(system_path(path).c_str(), FILE_APPEND_DATA, share_all, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.valid()) return last_failure("Append", path);
    return write_all(handle.get(), data, "Append", path);
}

Status remove_entry(const std::string& path)
{
    const auto kind = probe(path);
    if (!kind) return kind.failure();
    if (kind.value().kind == Kind::missing) return Failure{ErrorCode::not_found, "Remove " + path + ": not found"};
    const auto wide = system_path(path);
    const BOOL removed = kind.value().kind == Kind::directory ? RemoveDirectoryW(wide.c_str()) : DeleteFileW(wide.c_str());
    if (!removed) return last_failure("Remove", path);
    return Done{};
}

Outcome<std::vector<TreeEntry>> enumerate_tree(const std::string& root)
{
    const auto kind = probe(root);
    if (!kind) return kind.failure();
    if (kind.value().kind == Kind::missing) return Failure{ErrorCode::not_found, "RemoveTree " + root + ": not found"};
    if (kind.value().kind != Kind::directory) return Failure{ErrorCode::invalid, "RemoveTree " + root + ": not a folder"};

    std::vector<TreeEntry> out;
    TreeEntry top{system_path(root), root, true, kind.value().link};
    // Depth-first; a folder is appended after its children.
    struct Frame
    {
        TreeEntry entry;
        bool expanded;
    };
    std::vector<Frame> stack{{top, top.link}}; // a link root is removed as a link
    while (!stack.empty())
    {
        if (stack.back().expanded)
        {
            out.push_back(std::move(stack.back().entry));
            stack.pop_back();
            continue;
        }
        stack.back().expanded = true;
        const TreeEntry folder = stack.back().entry;
        WIN32_FIND_DATAW data{};
        const auto pattern = folder.system + L"\\*";
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE)
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND) continue;
            return win32_failure("RemoveTree", folder.path, code);
        }
        do
        {
            const std::wstring_view name(data.cFileName);
            if (name == L"." || name == L"..") continue;
            TreeEntry entry;
            entry.system = folder.system + L"\\" + std::wstring(name);
            entry.path = Core::child_path(folder.path, narrow_lossy(name));
            entry.directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            entry.link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (entry.directory && !entry.link) stack.push_back(Frame{std::move(entry), false});
            else out.push_back(std::move(entry));
        } while (FindNextFileW(find, &data));
        const DWORD code = GetLastError();
        FindClose(find);
        if (code != ERROR_NO_MORE_FILES) return win32_failure("RemoveTree", folder.path, code);
    }
    return out;
}

Status remove_tree_entries(const std::vector<TreeEntry>& entries)
{
    for (const auto& entry : entries)
    {
        const BOOL removed = entry.directory ? RemoveDirectoryW(entry.system.c_str()) : DeleteFileW(entry.system.c_str());
        if (!removed)
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) continue;
            return win32_failure("RemoveTree", entry.path, code);
        }
    }
    return Done{};
}

Status copy_file(const std::string& from, const std::string& to, bool overwrite, const std::string& backup)
{
    const auto source = probe(from);
    if (!source) return source.failure();
    if (source.value().kind == Kind::missing) return Failure{ErrorCode::not_found, "Copy " + from + ": not found"};
    if (source.value().kind == Kind::directory) return Failure{ErrorCode::invalid, "Copy " + from + ": folders aren't copied"};
    const auto target = probe(to);
    if (!target) return target.failure();
    if (target.value().kind == Kind::directory) return Failure{ErrorCode::invalid, "Copy " + to + ": is a folder"};
    if (target.value().kind == Kind::file && !overwrite) return Failure{ErrorCode::exists, "Copy " + to + ": already exists"};
    if (target.value().read_only) return Failure{ErrorCode::read_only, "Copy " + to + ": the file is read-only"};
    return atomic_into(to, {}, from, backup, overwrite);
}

Status move_entry(const std::string& from, const std::string& to, bool overwrite, const std::string& backup)
{
    const auto source = probe(from);
    if (!source) return source.failure();
    if (source.value().kind == Kind::missing) return Failure{ErrorCode::not_found, "Move " + from + ": not found"};
    if (source.value().kind == Kind::directory && !source.value().link)
    {
        return Failure{ErrorCode::invalid, "Move " + from + ": folders aren't moved"};
    }
    const auto target = probe(to);
    if (!target) return target.failure();
    if (target.value().kind == Kind::directory && !target.value().link)
    {
        return Failure{ErrorCode::invalid, "Move " + to + ": is a folder"};
    }
    const bool target_exists = target.value().kind != Kind::missing;
    if (target_exists && !overwrite) return Failure{ErrorCode::exists, "Move " + to + ": already exists"};
    if (target.value().read_only) return Failure{ErrorCode::read_only, "Move " + to + ": the file is read-only"};

    if (target_exists && !backup.empty())
    {
        if (ReplaceFileW(system_path(to).c_str(), system_path(from).c_str(), system_path(backup).c_str(), 0, nullptr, nullptr))
        {
            return Done{};
        }
        const DWORD code = GetLastError();
        if (Core::classify_replace_error(static_cast<uint32_t>(code), true) == Core::ReplaceFault::target_free)
        {
            // 1177: the old destination is already the .bak and `from` kept its
            // name; the destination name is free, so finish with a rename.
            Win32AtomicOps ops({}, {});
            return Core::finish_partial_replace(ops, to, from, backup, win32_failure("Move", to, code));
        }
        if (code != ERROR_UNABLE_TO_MOVE_REPLACEMENT && code != ERROR_NOT_SAME_DEVICE) return win32_failure("Move", to, code);
    }
    else
    {
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
        if (MoveFileExW(system_path(from).c_str(), system_path(to).c_str(), flags)) return Done{};
        const DWORD code = GetLastError();
        if (code != ERROR_NOT_SAME_DEVICE) return win32_failure("Move", to, code);
    }
    // Across volumes: copy (atomically, keeping the .bak), flush, then remove the source.
    // A copy would follow a link and leave a plain file, so links stay on their volume.
    if (source.value().link) return Failure{ErrorCode::invalid, "Move " + from + ": links aren't moved across volumes"};
    auto copied = atomic_into(to, {}, from, backup, overwrite);
    if (!copied) return copied;
    if (!DeleteFileW(system_path(from).c_str())) return last_failure("Move", from);
    return Done{};
}

std::vector<std::string> subfolders(const std::string& path)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAW data{};
    const auto pattern = system_path(path) + L"\\*";
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return out;
    do
    {
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            continue;
        }
        if (const auto utf8 = Core::utf16_to_utf8(std::u16string(name.begin(), name.end()))) out.push_back(*utf8);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return out;
}

uint32_t process_id()
{
    return static_cast<uint32_t>(GetCurrentProcessId());
}

uint32_t environment_limit(const char* name, uint32_t fallback, uint32_t maximum)
{
    std::array<char, 32> value{};
    const DWORD size = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (size == 0 || size >= value.size()) return fallback;
    uint64_t result = 0;
    for (DWORD i = 0; i < size; ++i)
    {
        const char c = value[i];
        if (c < '0' || c > '9') return fallback;
        result = result * 10 + static_cast<uint64_t>(c - '0');
        if (result > maximum) return fallback;
    }
    return static_cast<uint32_t>(result);
}

void debug_output(std::string_view line)
{
    const std::string copy(line);
    OutputDebugStringA(copy.c_str());
}

std::string utc_timestamp()
{
    SYSTEMTIME now{};
    GetSystemTime(&now);
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02uZ", static_cast<unsigned>(now.wYear),
        static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
        static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond));
    return text.data();
}

// --- Stream ----------------------------------------------------------------

Outcome<std::unique_ptr<Stream>> Stream::open(const std::string& path, bool truncate, bool create, bool manual_flush)
{
    const auto kind = probe(path);
    if (!kind) return kind.failure();
    if (kind.value().kind == Kind::directory) return Failure{ErrorCode::invalid, "Open " + path + ": is a folder"};
    if (kind.value().kind == Kind::missing && !create) return Failure{ErrorCode::not_found, "Open " + path + ": not found"};
    const DWORD disposition = truncate ? (create ? CREATE_ALWAYS : TRUNCATE_EXISTING) : (create ? OPEN_ALWAYS : OPEN_EXISTING);
    const DWORD access = truncate ? GENERIC_WRITE : FILE_APPEND_DATA;
    HANDLE handle = CreateFileW(system_path(path).c_str(), access, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, disposition,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return last_failure("Open", path);
    std::unique_ptr<Stream> stream(new Stream());
    stream->handle_ = handle;
    stream->manual_flush_ = manual_flush;
    stream->path_ = path;
    return stream;
}

Stream::~Stream()
{
    (void)close();
}

Status Stream::write_through(std::string_view data)
{
    return write_all(static_cast<HANDLE>(handle_), data, "Write", path_);
}

// Writes the buffer; on failure only the bytes that reached the file are
// dropped, so a later Flush or Close retries the rest without a gap.
Status Stream::flush_buffer()
{
    std::size_t written = 0;
    auto status = write_all(static_cast<HANDLE>(handle_), buffer_, "Write", path_, &written);
    buffer_.erase(0, written);
    return status;
}

Status Stream::write(std::string_view data)
{
    std::scoped_lock lock(mutex_);
    if (!handle_) return Failure{ErrorCode::invalid, "stream is closed"};
    if (!manual_flush_) return write_through(data);
    if (buffer_.size() + data.size() > static_cast<std::size_t>(NativeContract::stream_buffer_bytes))
    {
        auto flushed = flush_buffer();
        if (!flushed) return flushed; // `data` isn't taken; the buffer keeps what didn't reach the file
        if (data.size() >= static_cast<std::size_t>(NativeContract::stream_buffer_bytes)) return write_through(data);
    }
    buffer_.append(data);
    return Done{};
}

Status Stream::flush()
{
    std::scoped_lock lock(mutex_);
    if (!handle_) return Failure{ErrorCode::invalid, "stream is closed"};
    return flush_buffer();
}

Status Stream::close()
{
    std::scoped_lock lock(mutex_);
    if (!handle_) return Done{};
    // Closing always releases the file; data that still can't be written is
    // reported and then dropped with the stream.
    Status flushed = buffer_.empty() ? Status{Done{}} : flush_buffer();
    buffer_.clear();
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
    return flushed;
}

// --- TailFile --------------------------------------------------------------

namespace
{
struct Identity
{
    uint64_t volume{};
    uint64_t index{};
};

bool identity_of(HANDLE handle, Identity& out, int64_t& size)
{
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return false;
    out.volume = info.dwVolumeSerialNumber;
    out.index = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    size = static_cast<int64_t>((static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
    return true;
}

HANDLE open_for_tail(const std::string& path)
{
    return CreateFileW(system_path(path).c_str(), GENERIC_READ, share_all, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
        nullptr);
}
}

Outcome<std::unique_ptr<TailFile>> TailFile::open(const std::string& path, bool chunks, bool from_start)
{
    const auto kind = probe(path);
    if (!kind) return kind.failure();
    if (kind.value().kind == Kind::directory) return Failure{ErrorCode::invalid, "Tail " + path + ": is a folder"};
    const auto parent = probe(Core::parent_path(path));
    if (!parent) return parent.failure();
    if (parent.value().kind != Kind::directory) return Failure{ErrorCode::not_found, "Tail " + path + ": the folder doesn't exist"};

    std::unique_ptr<TailFile> tail(new TailFile(path, chunks));
    if (kind.value().kind == Kind::file)
    {
        HANDLE handle = open_for_tail(path);
        if (handle == INVALID_HANDLE_VALUE) return last_failure("Tail", path);
        Identity identity;
        int64_t size = 0;
        if (!identity_of(handle, identity, size))
        {
            const auto failure = last_failure("Tail", path);
            CloseHandle(handle);
            return failure;
        }
        tail->handle_ = handle;
        tail->identity_[0] = identity.volume;
        tail->identity_[1] = identity.index;
        tail->seen_ = true;
        tail->state_.start_at(from_start ? 0 : size);
    }
    return tail;
}

TailFile::~TailFile()
{
    release();
}

void TailFile::release()
{
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    if (next_handle_) CloseHandle(static_cast<HANDLE>(next_handle_));
    handle_ = nullptr;
    next_handle_ = nullptr;
    switching_ = false;
}

namespace
{
// A file deleted while another handle (ours) keeps it open is "delete
// pending": opening its path fails with access denied until the last handle
// closes.
bool delete_pending(HANDLE handle)
{
    FILE_STANDARD_INFO info{};
    return GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info)) && info.DeletePending;
}
}

Status TailFile::poll(std::vector<Core::TailDelivery>& out, std::size_t budget_bytes,
    std::chrono::steady_clock::time_point now, std::size_t& consumed)
{
    consumed = 0;
    // Look for rotation or deletion, unless the old file is still being drained.
    if (!switching_ && (!handle_ || now >= next_identity_check_))
    {
        next_identity_check_ = now + std::chrono::milliseconds(NativeContract::tail_identity_check_ms);
        HANDLE fresh = open_for_tail(path_);
        if (fresh == INVALID_HANDLE_VALUE)
        {
            const DWORD code = GetLastError();
            const bool gone = code == ERROR_FILE_NOT_FOUND || code == ERROR_DELETE_PENDING ||
                (code == ERROR_ACCESS_DENIED && handle_ && delete_pending(static_cast<HANDLE>(handle_)));
            if (gone)
            {
                if (!handle_) return Done{};
                switching_ = true; // drain what the held handle still has, then wait for a new file
            }
            else if (code == ERROR_SHARING_VIOLATION)
            {
                if (!handle_) return Done{};
            }
            else
            {
                return win32_failure("Tail", path_, code);
            }
        }
        else
        {
            Identity identity;
            int64_t ignored = 0;
            if (!identity_of(fresh, identity, ignored))
            {
                const auto failure = last_failure("Tail", path_);
                CloseHandle(fresh);
                return failure;
            }
            if (!handle_)
            {
                // First appearance, or a new file after a deletion.
                handle_ = fresh;
                identity_[0] = identity.volume;
                identity_[1] = identity.index;
                if (seen_) state_.reset();
                else state_.start_at(0);
                seen_ = true;
            }
            else if (identity.volume != identity_[0] || identity.index != identity_[1])
            {
                // Rotated: finish the old file first, then switch.
                next_handle_ = fresh;
                next_identity_[0] = identity.volume;
                next_identity_[1] = identity.index;
                switching_ = true;
            }
            else
            {
                CloseHandle(fresh);
            }
        }
    }
    if (!handle_) return Done{};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(static_cast<HANDLE>(handle_), &size)) return last_failure("Tail", path_);
    state_.observe_size(size.QuadPart);
    const int64_t available = size.QuadPart - state_.read_offset();
    int64_t got_total = 0;
    if (available > 0 && budget_bytes > 0)
    {
        const auto wanted = static_cast<std::size_t>(std::min<int64_t>(available, static_cast<int64_t>(budget_bytes)));
        std::string bytes(wanted, '\0');
        LARGE_INTEGER position{};
        position.QuadPart = state_.read_offset();
        if (!SetFilePointerEx(static_cast<HANDLE>(handle_), position, nullptr, FILE_BEGIN)) return last_failure("Tail", path_);
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(handle_), bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr))
        {
            return last_failure("Tail", path_);
        }
        bytes.resize(got);
        consumed = got;
        got_total = got;
        state_.feed(bytes, out);
    }

    if (switching_ && got_total >= available)
    {
        // The old file is drained: its held partial line is discarded (API.md
        // "Truncation and rotation") and reading moves to the new file, if any.
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = next_handle_;
        next_handle_ = nullptr;
        switching_ = false;
        if (handle_)
        {
            identity_[0] = next_identity_[0];
            identity_[1] = next_identity_[1];
            state_.reset();
        }
    }
    return Done{};
}
}
