#pragma once

// Portable text helpers: UTF-8 validation, UTF-8 <-> UTF-16, ASCII case
// folding and the decimal time format used in records.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace UE4SSLuaFileBridge::Core
{
// Case-insensitive equality used for every path comparison. The Win32 backend
// supplies an ordinal comparison (CompareStringOrdinal); the portable default
// folds ASCII only.
using EqualFold = bool (*)(std::string_view, std::string_view);

constexpr char ascii_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char ascii_upper(char c)
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

inline bool ascii_equal_fold(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

// Decodes one code point; returns false on any malformed sequence, overlong
// form, surrogate or value above U+10FFFF.
inline bool next_code_point(std::string_view s, std::size_t& index, char32_t& out)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned char lead = byte(index);
    std::size_t length{};
    char32_t value{};
    char32_t minimum{};
    if (lead < 0x80)
    {
        out = lead;
        ++index;
        return true;
    }
    if ((lead & 0xE0) == 0xC0) { length = 2; value = lead & 0x1F; minimum = 0x80; }
    else if ((lead & 0xF0) == 0xE0) { length = 3; value = lead & 0x0F; minimum = 0x800; }
    else if ((lead & 0xF8) == 0xF0) { length = 4; value = lead & 0x07; minimum = 0x10000; }
    else return false;
    if (index + length > s.size()) return false;
    for (std::size_t i = 1; i < length; ++i)
    {
        const unsigned char continuation = byte(index + i);
        if ((continuation & 0xC0) != 0x80) return false;
        value = (value << 6) | (continuation & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    out = value;
    index += length;
    return true;
}

inline bool valid_utf8(std::string_view s)
{
    std::size_t index{};
    char32_t ignored{};
    while (index < s.size())
    {
        if (!next_code_point(s, index, ignored)) return false;
    }
    return true;
}

inline std::optional<std::u16string> utf8_to_utf16(std::string_view s)
{
    std::u16string out;
    out.reserve(s.size());
    std::size_t index{};
    char32_t cp{};
    while (index < s.size())
    {
        if (!next_code_point(s, index, cp)) return std::nullopt;
        if (cp < 0x10000)
        {
            out.push_back(static_cast<char16_t>(cp));
        }
        else
        {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

// Returns nullopt for an unpaired surrogate (Windows permits them in names).
inline std::optional<std::string> utf16_to_utf8(std::u16string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        char32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF)
        {
            if (i + 1 >= s.size() || s[i + 1] < 0xDC00 || s[i + 1] > 0xDFFF) return std::nullopt;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        else if (cp >= 0xDC00 && cp <= 0xDFFF)
        {
            return std::nullopt;
        }
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// 100-nanosecond intervals between 1601-01-01 (FILETIME) and 1970-01-01.
inline constexpr int64_t filetime_unix_epoch = 116444736000000000LL;

// Unix seconds as decimal text with up to 7 fraction digits, trailing zeros
// removed: 1791640800, 1791640800.5, -0.25.
inline std::string format_unix_ticks(int64_t ticks_since_unix_epoch)
{
    constexpr int64_t per_second = 10000000;
    const bool negative = ticks_since_unix_epoch < 0;
    // Work on the magnitude in unsigned arithmetic so INT64_MIN is safe.
    const uint64_t magnitude = negative ? 0 - static_cast<uint64_t>(ticks_since_unix_epoch)
                                        : static_cast<uint64_t>(ticks_since_unix_epoch);
    const uint64_t seconds = magnitude / per_second;
    uint64_t fraction = magnitude % per_second;
    std::string out = negative ? "-" : "";
    out += std::to_string(seconds);
    if (fraction != 0)
    {
        std::string digits = std::to_string(fraction);
        digits.insert(0, 7 - digits.size(), '0');
        while (!digits.empty() && digits.back() == '0') digits.pop_back();
        out += '.';
        out += digits;
    }
    return out;
}

inline std::string format_filetime(int64_t filetime_ticks)
{
    return format_unix_ticks(filetime_ticks - filetime_unix_epoch);
}
}
