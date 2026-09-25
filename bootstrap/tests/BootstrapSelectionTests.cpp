#include <CandidateDiscovery.hpp>
#include <LoaderConfig.hpp>

#include <cassert>
#include <optional>
#include <string_view>
#include <vector>

int main()
{
    assert(parse_semantic_version("1.0.7") == SemanticVersion({1, 0, 7}));
    assert(!parse_semantic_version("1.0"));
    assert(!parse_semantic_version("01.0.7"));
    assert(!parse_semantic_version("1.0.7-rc"));

    const auto missing = parse_loader_config(std::nullopt);
    assert(missing.valid && !missing.exact_version);

    constexpr std::string_view automatic_source = R"({"schema":1,"version":"auto"})";
    const auto automatic = parse_loader_config(automatic_source);
    assert(automatic.valid && !automatic.exact_version);
    const auto bom = parse_loader_config(std::string_view("\xEF\xBB\xBF{\"schema\":1,\"version\":\"auto\"}"));
    assert(bom.valid && !bom.exact_version);

    constexpr std::string_view exact_source = R"({ "version": "1.0.7", "schema": 1 })";
    const auto exact = parse_loader_config(exact_source);
    assert(exact.valid && exact.exact_version == SemanticVersion({1, 0, 7}));

    for (const auto invalid : {
             R"({"schema":2,"version":"1.0.7"})",
             R"({"schema":1,"version":"latest"})",
             R"({"schema":1,"version":"1.0.7",})",
             R"({"schema":1,"version":"1.0.7","path":"other.dll"})",
             R"({"schema":1,"schema":1,"version":"1.0.7"})"})
    {
        assert(!parse_loader_config(std::string_view(invalid)).valid);
    }

    const std::vector<BootstrapCandidate> candidates{
        {{1, 0, 6}, "one.dll"},
        {{1, 0, 7}, "two.dll"},
        {{1, 1, 0}, "three.dll"},
    };
    const auto* newest = select_candidate(candidates, std::nullopt);
    assert(newest && newest->version == SemanticVersion({1, 1, 0}));
    const auto* pinned = select_candidate(candidates, SemanticVersion{1, 0, 7});
    assert(pinned && pinned->path == "two.dll");
    assert(!select_candidate(candidates, SemanticVersion{2, 0, 0}));

    CandidateMetadata metadata{
        {1, 0, 7}, L"UE4SSLuaEventBridge-1.0.7.dll", L"UE4SSLuaEventBridge-1.0.7.dll",
        L"UE4SSLuaEventBridge", L"Implementation", L"1", L"97b7e501", L"5.5"};
    assert(compatible_candidate(metadata));
    metadata.ue4ss_commit = L"other";
    assert(!compatible_candidate(metadata));
    metadata.ue4ss_commit = L"97b7e501";
    metadata.original_filename = L"other.dll";
    assert(!compatible_candidate(metadata));
}
