#pragma once

// Record strings for structured results (contract section 6).

#include <FileBridgeNativeContract.hpp>
#include <FileBridgeText.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UE4SSLuaFileBridge::Core
{
struct ListEntry
{
    std::string name;
    std::string type; // "file", "directory" or "other"
    int64_t size{};
    int64_t modified_filetime{};
    bool link{};
    int64_t created_filetime{};
};

struct LocationRecord
{
    std::string name;
    std::string path; // "" when unbound
    bool root{};
    bool exists{};
};

inline bool record_safe(std::string_view field)
{
    return field.find(NativeContract::record_separator) == std::string_view::npos &&
        field.find(NativeContract::field_separator) == std::string_view::npos;
}

// Sorted by name (bytes). Entries whose name contains a separator are
// skipped; `skipped` counts them.
inline std::string encode_list(std::vector<ListEntry> entries, std::size_t& skipped)
{
    skipped = 0;
    std::sort(entries.begin(), entries.end(), [](const ListEntry& a, const ListEntry& b) { return a.name < b.name; });
    std::string out;
    for (const auto& entry : entries)
    {
        if (!record_safe(entry.name))
        {
            ++skipped;
            continue;
        }
        if (!out.empty()) out.push_back(NativeContract::record_separator);
        out += entry.name;
        out.push_back(NativeContract::field_separator);
        out += entry.type;
        out.push_back(NativeContract::field_separator);
        out += std::to_string(entry.size);
        out.push_back(NativeContract::field_separator);
        out += format_filetime(entry.modified_filetime);
        out.push_back(NativeContract::field_separator);
        out.push_back(entry.link ? '1' : '0');
        out.push_back(NativeContract::field_separator);
        out += format_filetime(entry.created_filetime);
    }
    return out;
}

inline std::string encode_locations(const std::vector<LocationRecord>& records)
{
    std::string out;
    for (const auto& record : records)
    {
        if (!out.empty()) out.push_back(NativeContract::record_separator);
        out += record.name;
        out.push_back(NativeContract::field_separator);
        out += record.path;
        out.push_back(NativeContract::field_separator);
        out.push_back(record.root ? '1' : '0');
        out.push_back(NativeContract::field_separator);
        out.push_back(record.exists ? '1' : '0');
    }
    return out;
}

// Removes a leading UTF-8 byte-order mark (ReadText).
inline std::string_view strip_utf8_bom(std::string_view content)
{
    return content.substr(0, 3) == "\xEF\xBB\xBF" ? content.substr(3) : content;
}
}
