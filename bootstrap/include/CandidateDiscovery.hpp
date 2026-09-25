#pragma once

#include <charconv>
#include <compare>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SemanticVersion
{
    uint32_t major{};
    uint32_t minor{};
    uint32_t patch{};
    auto operator<=>(const SemanticVersion&) const = default;
};

inline std::optional<SemanticVersion> parse_semantic_version(std::string_view text)
{
    SemanticVersion result{};
    uint32_t* fields[]{&result.major, &result.minor, &result.patch};
    for (std::size_t index = 0; index < 3; ++index)
    {
        const auto separator = text.find('.');
        const auto field = separator == std::string_view::npos ? text : text.substr(0, separator);
        if (field.empty() || (field.size() > 1 && field.front() == '0')) return {};
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), *fields[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size()) return {};
        if (index < 2)
        {
            if (separator == std::string_view::npos) return {};
            text.remove_prefix(separator + 1);
        }
        else if (separator != std::string_view::npos)
        {
            return {};
        }
    }
    return result;
}

struct BootstrapCandidate
{
    SemanticVersion version;
    std::filesystem::path path;
};

struct CandidateMetadata
{
    SemanticVersion version;
    std::wstring filename;
    std::wstring original_filename;
    std::wstring product_name;
    std::wstring role;
    std::wstring implementation_abi;
    std::wstring ue4ss_commit;
    std::wstring unreal_version;
};

inline std::wstring candidate_filename(const SemanticVersion& version)
{
    return L"UE4SSLuaEventBridge-" + std::to_wstring(version.major) + L"." +
           std::to_wstring(version.minor) + L"." + std::to_wstring(version.patch) + L".dll";
}

inline bool same_candidate_name(const std::wstring& left, const std::wstring& right)
{
#if defined(_WIN32)
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
#else
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto lower = [](wchar_t value) {
            return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value + (L'a' - L'A')) : value;
        };
        if (lower(left[index]) != lower(right[index])) return false;
    }
    return true;
#endif
}

inline bool compatible_candidate(const CandidateMetadata& metadata)
{
    const auto expected = candidate_filename(metadata.version);
    return metadata.product_name == L"UE4SSLuaEventBridge" &&
           metadata.role == L"Implementation" && metadata.implementation_abi == L"1" &&
           metadata.ue4ss_commit == L"97b7e501" && metadata.unreal_version == L"5.5" &&
           same_candidate_name(metadata.filename, expected) &&
           same_candidate_name(metadata.original_filename, expected);
}

inline const BootstrapCandidate* select_candidate(
    const std::vector<BootstrapCandidate>& candidates,
    const std::optional<SemanticVersion>& exact)
{
    const BootstrapCandidate* selected{};
    for (const auto& candidate : candidates)
    {
        if (exact && candidate.version != *exact) continue;
        if (!selected || candidate.version > selected->version) selected = &candidate;
    }
    return selected;
}
