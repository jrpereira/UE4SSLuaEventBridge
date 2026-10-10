#pragma once

// Thin Win32 backend of the file bridge. Every path here is a normalized
// absolute UTF-8 path ("C:/a/b") that the caller has already checked; the
// backend converts to \\?\ UTF-16 and maps errors to contract codes. No
// <windows.h> in this header.

#include <FileBridgeOutcome.hpp>
#include <FileBridgePolicy.hpp>
#include <FileBridgeRecords.hpp>
#include <FileBridgeTail.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace UE4SSLuaFileBridge::Win32
{
using Core::Done;
using Core::Failure;
using Core::Outcome;
using Core::Status;

// Ordinal, case-insensitive comparison (CompareStringOrdinal).
bool equal_fold(std::string_view a, std::string_view b);

struct Roots
{
    bool ok{};
    std::string error;
    std::string game;    // Steam install folder (holds <project>/ and Engine/)
    std::string user;    // %LOCALAPPDATA%/<project>
    std::string project; // "Dawnwalker"
    std::string mods;    // UE4SS Mods folder
    std::string profile; // %USERPROFILE%
};

// `address_in_module` is any address inside the bridge DLL; the Mods folder is
// found three folders above it.
Roots discover_roots(const void* address_in_module);

class Resolver final : public Core::Resolver
{
public:
    Outcome<std::string> resolve(const std::string& lexical, bool follow_leaf) override;
};

enum class Kind
{
    missing,
    file,
    directory,
};

struct Probe
{
    Kind kind{Kind::missing};
    bool link{};
    bool read_only{};
};

// Attributes of the entry itself (a link is not followed).
Outcome<Probe> probe(const std::string& path);

struct StatResult
{
    std::string type;
    int64_t size{};
    int64_t modified{}; // FILETIME ticks
    int64_t created{};
    bool read_only{};
};

Outcome<StatResult> stat(const std::string& path);
Outcome<std::vector<Core::ListEntry>> list(const std::string& path, std::size_t& undecodable);
// length < 0 reads to the end. More than `maximum` bytes is `invalid`.
Outcome<std::string> read(const std::string& path, int64_t offset, int64_t length, int64_t maximum);

Status create_directory(const std::string& path);
Status write_atomic(const std::string& path, std::string_view data, const std::string& backup);
Status write_in_place(const std::string& path, std::string_view data);
Status append(const std::string& path, std::string_view data);
Status remove_entry(const std::string& path);

struct TreeEntry
{
    std::wstring system; // exact \\?\ path, also for names that aren't valid UTF-16
    std::string path;    // UTF-8 (lossy for such names), for checks and messages
    bool directory{};
    bool link{};
};

// Entries of a tree, children before their folder, the root last. Links are
// listed but never entered.
Outcome<std::vector<TreeEntry>> enumerate_tree(const std::string& root);
Status remove_tree_entries(const std::vector<TreeEntry>& entries);

Status copy_file(const std::string& from, const std::string& to, bool overwrite, const std::string& backup);
Status move_entry(const std::string& from, const std::string& to, bool overwrite, const std::string& backup);

// Names of the folders directly inside `path` (none on failure).
std::vector<std::string> subfolders(const std::string& path);

uint32_t process_id();
uint32_t environment_limit(const char* name, uint32_t fallback, uint32_t maximum);
void debug_output(std::string_view line);
std::string utc_timestamp(); // ISO 8601, seconds, "Z"

// A write stream (API.md "Streams: Open"). Thread-safe.
class Stream
{
public:
    static Outcome<std::unique_ptr<Stream>> open(const std::string& path, bool truncate, bool create, bool manual_flush);
    ~Stream();
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    Status write(std::string_view data);
    Status flush();
    Status close();

private:
    Stream() = default;
    Status write_through(std::string_view data);

    std::mutex mutex_;
    void* handle_{};
    bool manual_flush_{};
    std::string buffer_;
    std::string path_;
};

// A tailed file (API.md "File sockets: Tail"). Used from on_update only.
class TailFile
{
public:
    // Fails with not_found if the folder is missing, invalid if `path` is a folder.
    static Outcome<std::unique_ptr<TailFile>> open(const std::string& path, bool chunks, bool from_start);
    ~TailFile();
    TailFile(const TailFile&) = delete;
    TailFile& operator=(const TailFile&) = delete;

    // Reads at most `budget_bytes` of new data into deliveries and reports the
    // bytes read in `consumed`. On rotation or deletion the old file is read to
    // its end before switching. A failure means the subscription must close
    // (code and message for the log).
    Status poll(std::vector<Core::TailDelivery>& out, std::size_t budget_bytes, std::chrono::steady_clock::time_point now,
        std::size_t& consumed);

private:
    TailFile(std::string path, bool chunks) : path_(std::move(path)), state_(chunks) {}
    void release();

    std::string path_;
    Core::TailState state_;
    void* handle_{};
    uint64_t identity_[2]{};
    void* next_handle_{};          // the rotated-in file, while the old one drains
    uint64_t next_identity_[2]{};
    bool switching_{};             // draining handle_ before moving to next_handle_
    bool seen_{};
    std::chrono::steady_clock::time_point next_identity_check_{};
};
}
