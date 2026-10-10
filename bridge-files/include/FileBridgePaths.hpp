#pragma once

// Portable path model (API.md "Paths"). Paths are UTF-8, absolute paths use
// forward slashes and an uppercase drive letter: "C:/Games/x". The Win32
// backend converts to \\?\ UTF-16 only at the system-call boundary.

#include <FileBridgeNativeContract.hpp>
#include <FileBridgeOutcome.hpp>
#include <FileBridgeText.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace UE4SSLuaFileBridge::Core
{
struct Location
{
    std::string name;
    bool root{};
    bool per_mod{};
    bool enabled{};
    std::string absolute; // normalized absolute path; empty when unbound
};

class LocationSet
{
public:
    std::vector<Location> entries;

    [[nodiscard]] const Location* find(std::string_view name) const
    {
        for (const auto& entry : entries)
        {
            if (entry.name == name) return &entry;
        }
        return nullptr;
    }

    // An enabled, bound location, or nullptr.
    [[nodiscard]] const Location* bound(std::string_view name) const
    {
        const auto* entry = find(name);
        return entry && entry->enabled && !entry->absolute.empty() ? entry : nullptr;
    }
};

inline std::vector<std::string_view> split_segments(std::string_view path)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const auto end = path.find('/', start);
        const auto stop = end == std::string_view::npos ? path.size() : end;
        if (stop > start) out.push_back(path.substr(start, stop - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

inline std::string join_segments(const std::vector<std::string>& segments)
{
    std::string out;
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        if (i) out.push_back('/');
        out += segments[i];
    }
    if (segments.size() == 1) out.push_back('/'); // a drive root keeps its slash: "C:/"
    return out;
}

inline std::size_t segment_count(std::string_view absolute)
{
    return split_segments(absolute).size();
}

// `child` equals `parent` or lies below it, compared segment by segment.
inline bool is_within(std::string_view child, std::string_view parent, EqualFold eq)
{
    const auto c = split_segments(child);
    const auto p = split_segments(parent);
    if (p.empty() || c.size() < p.size()) return false;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        if (!eq(c[i], p[i])) return false;
    }
    return true;
}

inline bool same_path(std::string_view a, std::string_view b, EqualFold eq)
{
    return segment_count(a) == segment_count(b) && is_within(a, b, eq);
}

inline bool is_proper_ancestor(std::string_view ancestor, std::string_view descendant, EqualFold eq)
{
    return segment_count(descendant) > segment_count(ancestor) && is_within(descendant, ancestor, eq);
}

inline std::string_view leaf_name(std::string_view absolute)
{
    const auto segments = split_segments(absolute);
    return segments.empty() ? std::string_view{} : segments.back();
}

inline std::string parent_path(std::string_view absolute)
{
    auto segments = split_segments(absolute);
    std::vector<std::string> parts(segments.begin(), segments.end());
    if (parts.size() > 1) parts.pop_back();
    return join_segments(parts);
}

inline std::string child_path(std::string_view absolute, std::string_view name)
{
    std::string out(absolute);
    if (out.empty() || out.back() != '/') out.push_back('/');
    out += name;
    return out;
}

// Text after the last dot of a name; "" when there is none or the only dot
// leads (".owner").
inline std::string_view extension_of(std::string_view name)
{
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    return name.substr(dot + 1);
}

enum class SegmentFault
{
    none,
    invalid,
    reserved,
};

inline bool is_reserved_device_name(std::string_view segment)
{
    auto base = segment.substr(0, segment.find('.'));
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    static constexpr std::array<std::string_view, 4> plain{"CON", "PRN", "AUX", "NUL"};
    for (const auto name : plain)
    {
        if (ascii_equal_fold(base, name)) return true;
    }
    if (base.size() == 4 && base[3] >= '1' && base[3] <= '9' &&
        (ascii_equal_fold(base.substr(0, 3), "COM") || ascii_equal_fold(base.substr(0, 3), "LPT")))
    {
        return true;
    }
    return false;
}

inline SegmentFault classify_segment(std::string_view segment)
{
    if (segment.empty()) return SegmentFault::invalid;
    for (const char c : segment)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20) return SegmentFault::invalid;
        switch (c)
        {
        case '<': case '>': case ':': case '"': case '|': case '?': case '*': case '/': case '\\':
            return SegmentFault::invalid;
        default:
            break;
        }
    }
    if (segment.back() == ' ' || segment.back() == '.') return SegmentFault::invalid;
    if (is_reserved_device_name(segment)) return SegmentFault::reserved;
    return SegmentFault::none;
}

// A mod name usable as one folder name (binds mod, moddata, temp).
inline bool is_single_segment(std::string_view name)
{
    return valid_utf8(name) && name != "." && name != ".." &&
        classify_segment(name) == SegmentFault::none;
}

// Converts an absolute Windows path ("C:\\a\\b", "C:/a/b/") to the normalized
// form without validating against locations. Used for roots and real paths
// the backend produces. Returns "" if it isn't a plain drive path.
inline std::string normalize_system_path(std::string_view path)
{
    std::string s(path);
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.rfind("//?/", 0) == 0) s.erase(0, 4);
    if (s.size() < 2 || !((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) || s[1] != ':')
    {
        return {};
    }
    if (s.size() > 2 && s[2] != '/') return {};
    std::vector<std::string> parts{std::string{ascii_upper(s[0]), ':'}};
    for (const auto segment : split_segments(std::string_view(s).substr(2)))
    {
        if (segment == ".") continue;
        if (segment == "..")
        {
            if (parts.size() > 1) parts.pop_back();
            continue;
        }
        parts.emplace_back(segment);
    }
    return join_segments(parts);
}

// API.md "Paths": location-relative or absolute input -> normalized absolute
// path inside `game` or `user`. Pure: no file-system access.
inline Outcome<std::string> normalize(std::string_view input, const LocationSet& locations, EqualFold eq)
{
    using NativeContract::ErrorCode;
    const auto quoted = [&] { return "\"" + std::string(input) + "\""; };
    if (input.empty()) return fail(ErrorCode::invalid, "path is empty");
    if (input.find('\0') != std::string_view::npos) return fail(ErrorCode::invalid, "path contains a NUL byte");
    if (!valid_utf8(input)) return fail(ErrorCode::invalid, "path is not valid UTF-8");

    std::string s(input);
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.rfind("//", 0) == 0 || s.rfind("/?" "?/", 0) == 0)
    {
        return fail(ErrorCode::outside_root, quoted() + " is a device or UNC path");
    }
    if (s[0] == '/') return fail(ErrorCode::invalid, quoted() + " has no drive or location");

    std::vector<std::string> stack;
    std::string_view rest;
    bool absolute = false;
    std::size_t floor = 0;
    const bool drive = s.size() >= 2 && s[1] == ':' &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'));
    if (drive)
    {
        if (s.size() > 2 && s[2] != '/') return fail(ErrorCode::invalid, quoted() + " is drive-relative");
        stack.push_back(std::string{ascii_upper(s[0]), ':'});
        rest = std::string_view(s).substr(std::min<std::size_t>(3, s.size()));
        absolute = true;
        floor = 1;
    }
    else
    {
        const auto slash = s.find('/');
        const std::string_view name = std::string_view(s).substr(0, slash);
        const auto* location = locations.find(name);
        if (!location || !location->enabled)
        {
            return fail(ErrorCode::invalid, quoted() + ": unknown location \"" + std::string(name) + "\"");
        }
        if (location->absolute.empty())
        {
            return fail(ErrorCode::invalid, quoted() + ": location \"" + std::string(name) + "\" is unbound");
        }
        for (const auto segment : split_segments(location->absolute)) stack.emplace_back(segment);
        floor = stack.size();
        rest = slash == std::string::npos ? std::string_view{} : std::string_view(s).substr(slash + 1);
    }

    // Lowest depth reached by a ".." step; npos when there was none.
    std::size_t minimum = std::string::npos;
    for (const auto segment : split_segments(rest))
    {
        if (segment == ".") continue;
        if (segment == "..")
        {
            if (stack.size() <= 1) return fail(ErrorCode::outside_root, quoted() + " climbs above the drive");
            stack.pop_back();
            minimum = std::min(minimum, stack.size());
            continue;
        }
        switch (classify_segment(segment))
        {
        case SegmentFault::invalid:
            return fail(ErrorCode::invalid, quoted() + ": invalid name \"" + std::string(segment) + "\"");
        case SegmentFault::reserved:
            return fail(ErrorCode::outside_root, quoted() + ": \"" + std::string(segment) + "\" is a device name");
        case SegmentFault::none:
            break;
        }
        stack.emplace_back(segment);
    }
    if (!absolute && minimum != std::string::npos && minimum < floor)
    {
        return fail(ErrorCode::outside_root, quoted() + " climbs out of its location");
    }

    const auto result = join_segments(stack);
    std::size_t root_depth = 0;
    for (const auto* name : {"game", "user"})
    {
        const auto* root = locations.bound(name);
        if (root && is_within(result, root->absolute, eq)) root_depth = segment_count(root->absolute);
    }
    if (root_depth == 0) return fail(ErrorCode::outside_root, quoted() + " is outside the game and user folders");
    if (absolute && minimum != std::string::npos && minimum < root_depth) return fail(ErrorCode::outside_root, quoted() + " climbs out of its root");
    return result;
}

// Builds the location table of one Lua environment from the contract's list.
// `mods_folder` is the UE4SS Mods folder; `mod_name` is bound only when it is
// a single valid segment.
inline LocationSet make_locations(
    const std::string& game, const std::string& user, const std::string& mods_folder, std::string_view mod_name)
{
    LocationSet set;
    const bool bound = !mod_name.empty() && is_single_segment(mod_name);
    const std::string moddata = bound ? user + "/Saved/ModData/" + std::string(mod_name) : std::string{};
    for (const auto& entry : NativeContract::locations)
    {
        Location location{std::string(entry.name), entry.root, entry.per_mod, entry.enabled, {}};
        if (entry.name == "game") location.absolute = game;
        else if (entry.name == "user") location.absolute = user;
        else if (entry.name == "mod") location.absolute = bound ? mods_folder + "/" + std::string(mod_name) : "";
        else if (entry.name == "moddata") location.absolute = moddata;
        else if (entry.name == "temp") location.absolute = bound ? moddata + "/temp" : "";
        else if (entry.name == "savegames") location.absolute = user + "/Saved/SaveGames";
        else if (entry.name == "mods") location.absolute = mods_folder;
        set.entries.push_back(std::move(location));
    }
    return set;
}
}
