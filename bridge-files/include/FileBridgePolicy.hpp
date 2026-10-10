#pragma once

// Portable access-environment model (contract section 3, API.md "Access
// environments"): grants, inheritance, most-specific-wins, extension rules,
// the delete floor, the .owner rule and the savegames rules.

#include <FileBridgeNativeContract.hpp>
#include <FileBridgeOutcome.hpp>
#include <FileBridgePaths.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace UE4SSLuaFileBridge::Core
{
using NativeContract::Access;
using NativeContract::Mode;

struct PolicyGrant
{
    std::string given;   // the text the mod passed, for messages
    std::string lexical; // normalized absolute path
    std::string real;    // real path when the grant was added (used to replace)
    Mode mode{Mode::inherit};
    bool extensions_inherited{true};
    std::vector<std::string> extensions; // authoritative when !extensions_inherited
    bool delete_allowed{false};
    bool recursive{true};
};

struct Policy
{
    std::vector<PolicyGrant> grants;
};

// Resolves a normalized absolute path to its real path (links followed; the
// leaf unfollowed when `follow_leaf` is false). Fails with outside_root for a
// non-drive result, io for other failures.
class Resolver
{
public:
    virtual ~Resolver() = default;
    virtual Outcome<std::string> resolve(const std::string& lexical, bool follow_leaf) = 0;
};

enum class TargetKind
{
    file,
    directory,
};

struct CheckEnvironment
{
    const LocationSet& locations;
    Resolver& resolver;
    EqualFold eq;
    std::string profile_root; // normalized; "" if unknown
};

struct CheckRequest
{
    std::string_view operation;
    std::string_view given; // for messages
    Access access{Access::stat};
    TargetKind kind{TargetKind::file};
    bool bypasses_backup{false}; // non-atomic WriteText, truncating Open
};

inline std::string mode_name(Mode mode)
{
    switch (mode)
    {
    case Mode::stat: return "stat";
    case Mode::ro: return "ro";
    case Mode::wo: return "wo";
    case Mode::rw: return "rw";
    case Mode::ao: return "ao";
    case Mode::inherit: return "no mode";
    }
    return "?";
}

// Validates AddPath's wire arguments and builds the grant (real path supplied
// by the caller after resolving `lexical`).
inline Outcome<PolicyGrant> make_grant(
    std::string given, std::string lexical, std::string real,
    std::string_view mode_text, std::string_view extensions_text, int64_t flags)
{
    using NativeContract::AddPathFlag::known;
    namespace Flag = NativeContract::AddPathFlag;
    const auto mode = NativeContract::parse_mode(mode_text);
    if (!mode) return fail(ErrorCode::invalid, "AddPath: unknown mode \"" + std::string(mode_text) + "\"");
    if ((flags & ~known) != 0) return fail(ErrorCode::invalid, "AddPath: unknown flags");
    PolicyGrant grant;
    grant.given = std::move(given);
    grant.lexical = std::move(lexical);
    grant.real = std::move(real);
    grant.mode = *mode;
    grant.delete_allowed = (flags & Flag::delete_allowed) != 0;
    grant.recursive = (flags & Flag::non_recursive) == 0;
    grant.extensions_inherited = (flags & Flag::extensions_set) == 0;
    if (grant.delete_allowed && (grant.mode == Mode::ro || grant.mode == Mode::stat || grant.mode == Mode::ao))
    {
        return fail(ErrorCode::invalid, "AddPath: delete can't be combined with mode \"" + std::string(mode_text) + "\"");
    }
    if (grant.extensions_inherited)
    {
        if (!extensions_text.empty()) return fail(ErrorCode::invalid, "AddPath: extensions given without the extensions flag");
        return grant;
    }
    if (extensions_text.empty()) return grant; // {} covers no files
    std::size_t start = 0;
    while (true)
    {
        const auto end = extensions_text.find(NativeContract::extension_separator, start);
        const auto entry = extensions_text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (entry.empty() || !valid_utf8(entry)) return fail(ErrorCode::invalid, "AddPath: empty or invalid extension");
        for (const char c : entry)
        {
            if (static_cast<unsigned char>(c) < 0x20 || NativeContract::extension_forbidden.find(c) != std::string_view::npos)
            {
                return fail(ErrorCode::invalid, "AddPath: extension \"" + std::string(entry) + "\" has a forbidden character");
            }
        }
        std::string lowered(entry);
        for (auto& c : lowered) c = ascii_lower(c);
        grant.extensions.push_back(std::move(lowered));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return grant;
}

// Parent's grants plus `grant`; a grant on the same real path is replaced.
inline Outcome<Policy> add_grant(const Policy& parent, PolicyGrant grant, EqualFold eq)
{
    Policy next = parent;
    for (auto& existing : next.grants)
    {
        if (same_path(existing.real, grant.real, eq))
        {
            existing = std::move(grant);
            return next;
        }
    }
    if (static_cast<int64_t>(next.grants.size()) >= NativeContract::max_grants_per_policy)
    {
        return fail(ErrorCode::io, "AddPath: an environment holds at most 256 grants");
    }
    next.grants.push_back(std::move(grant));
    return next;
}

// The safe set: game ro, user ro, mod rw, temp rw + delete. Real paths are
// the lexical ones; they are re-resolved at every check.
inline Policy safe_policy(const LocationSet& locations)
{
    Policy policy;
    const auto add = [&](std::string_view name, Mode mode, bool delete_allowed) {
        const auto* location = locations.bound(name);
        if (!location) return;
        PolicyGrant grant;
        grant.given = std::string(name);
        grant.lexical = location->absolute;
        grant.real = location->absolute;
        grant.mode = mode;
        grant.delete_allowed = delete_allowed;
        policy.grants.push_back(std::move(grant));
    };
    add("game", Mode::ro, false);
    add("user", Mode::ro, false);
    add("mod", Mode::rw, false);
    add("temp", Mode::rw, true);
    return policy;
}

// Runs checks for one native call. Grant and location resolutions are
// memoized for the checker's lifetime (one call), so RemoveTree can check
// every entry cheaply.
class Checker
{
public:
    Checker(const Policy& policy, const CheckEnvironment& environment) : policy_(policy), env_(environment) {}

    // Normalized lexical path -> real path, then check_real.
    Outcome<std::string> check(const std::string& lexical, bool follow_leaf, const CheckRequest& request)
    {
        auto real = env_.resolver.resolve(lexical, follow_leaf);
        if (!real) return real.failure();
        auto status = check_real(real.value(), request);
        if (!status) return status.failure();
        return real;
    }

    Status check_real(const std::string& real, const CheckRequest& request)
    {
        prepare();
        const auto describe = [&] {
            return std::string(request.operation) + " " + std::string(request.given.empty() ? real : request.given);
        };
        const auto* game = location_real("game");
        const auto* user = location_real("user");
        if (!((game && is_within(real, *game, env_.eq)) || (user && is_within(real, *user, env_.eq))))
        {
            return fail(ErrorCode::outside_root, describe() + ": resolves outside the game and user folders");
        }

        const auto winner = winning_grant(real, request.kind);
        if (!winner) return fail(ErrorCode::denied, describe() + ": no grant covers this path");
        const auto& grant = policy_.grants[*winner];
        const auto mode = effective_mode(*winner);
        const auto granted = NativeContract::permissions_for(mode, grant.delete_allowed);
        const auto required = NativeContract::required_permissions(request.access);
        if ((required & ~granted) != 0)
        {
            std::string why = " allows " + mode_name(mode);
            if ((required & NativeContract::Permission::remove) != 0 && !grant.delete_allowed) why += " without delete";
            return fail(ErrorCode::denied, describe() + ": the grant on \"" + grant.given + "\"" + why);
        }

        if (request.access == Access::delete_entry || request.access == Access::move_source)
        {
            auto floor = delete_floor(real, *winner);
            if (!floor) return fail(ErrorCode::denied, describe() + ": " + floor.failure().message);
        }
        if (const auto* moddata = location_real("moddata"))
        {
            if (same_path(real, child_path(*moddata, ".owner"), env_.eq) &&
                request.access != Access::stat && request.access != Access::read)
            {
                return fail(ErrorCode::denied, describe() + ": the .owner marker is maintained by the bridge");
            }
        }
        if (request.bypasses_backup)
        {
            if (const auto* saves = location_real("savegames"); saves && is_within(real, *saves, env_.eq))
            {
                return fail(ErrorCode::invalid, describe() + ": savegames needs atomic writes that keep a .bak");
            }
        }
        return Done{};
    }

    // Real path of an enabled, bound location (resolved once), or nullptr.
    const std::string* location_real(std::string_view name)
    {
        const auto key = std::string(name);
        if (auto found = locations_.find(key); found != locations_.end())
        {
            return found->second ? &*found->second : nullptr;
        }
        std::optional<std::string> value;
        if (const auto* location = env_.locations.bound(name))
        {
            auto real = env_.resolver.resolve(location->absolute, true);
            if (real) value = std::move(real.value());
        }
        auto& slot = locations_[key];
        slot = std::move(value);
        return slot ? &*slot : nullptr;
    }

private:
    const Policy& policy_;
    const CheckEnvironment& env_;
    bool prepared_{false};
    std::vector<std::optional<std::string>> reals_;
    std::vector<std::optional<Mode>> modes_;
    std::vector<std::optional<std::optional<std::vector<std::string>>>> extensions_;
    std::unordered_map<std::string, std::optional<std::string>> locations_;

    void prepare()
    {
        if (prepared_) return;
        prepared_ = true;
        const auto count = policy_.grants.size();
        reals_.resize(count);
        modes_.resize(count);
        extensions_.resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            auto real = env_.resolver.resolve(policy_.grants[i].lexical, true);
            if (real) reals_[i] = std::move(real.value()); // unresolvable grants cover nothing
        }
    }

    bool covers_path(std::size_t index, std::string_view path) const
    {
        const auto& real = reals_[index];
        if (!real || !is_within(path, *real, env_.eq)) return false;
        return policy_.grants[index].recursive || segment_count(path) <= segment_count(*real) + 1;
    }

    // Most specific grant other than `self` covering `path` as a folder.
    std::optional<std::size_t> enclosing(std::size_t self, std::string_view path) const
    {
        std::optional<std::size_t> best;
        std::size_t best_depth = 0;
        for (std::size_t i = 0; i < policy_.grants.size(); ++i)
        {
            if (i == self || !covers_path(i, path)) continue;
            const auto depth = segment_count(*reals_[i]);
            if (!best || depth >= best_depth)
            {
                best = i;
                best_depth = depth;
            }
        }
        return best;
    }

    Mode effective_mode(std::size_t index, int depth = 0)
    {
        if (modes_[index]) return *modes_[index];
        Mode mode = policy_.grants[index].mode;
        if (mode == Mode::inherit && reals_[index] && depth < 512)
        {
            if (const auto parent = enclosing(index, *reals_[index])) mode = effective_mode(*parent, depth + 1);
        }
        modes_[index] = mode;
        return mode;
    }

    // nullopt = any extension.
    const std::optional<std::vector<std::string>>& effective_extensions(std::size_t index, int depth = 0)
    {
        if (extensions_[index]) return *extensions_[index];
        std::optional<std::vector<std::string>> value;
        const auto& grant = policy_.grants[index];
        if (!grant.extensions_inherited)
        {
            value = grant.extensions;
        }
        else if (reals_[index] && depth < 512)
        {
            if (const auto parent = enclosing(index, *reals_[index])) value = effective_extensions(*parent, depth + 1);
        }
        extensions_[index] = std::move(value);
        return *extensions_[index];
    }

    std::optional<std::size_t> winning_grant(const std::string& real, TargetKind kind)
    {
        std::optional<std::size_t> best;
        std::size_t best_depth = 0;
        const auto extension = extension_of(leaf_name(real));
        for (std::size_t i = 0; i < policy_.grants.size(); ++i)
        {
            if (!covers_path(i, real)) continue;
            if (kind == TargetKind::file)
            {
                const auto& allowed = effective_extensions(i);
                if (allowed)
                {
                    bool listed = false;
                    for (const auto& entry : *allowed) listed = listed || env_.eq(entry, extension);
                    if (!listed) continue;
                }
            }
            const auto depth = segment_count(*reals_[i]);
            if (!best || depth >= best_depth)
            {
                best = i;
                best_depth = depth;
            }
        }
        return best;
    }

    Status delete_floor(const std::string& real, std::size_t winner)
    {
        if (segment_count(real) <= 1) return fail(ErrorCode::denied, "a drive root can't be removed");
        if (!env_.profile_root.empty() && same_path(real, env_.profile_root, env_.eq))
        {
            return fail(ErrorCode::denied, "the user profile folder can't be removed");
        }
        const auto& grant = policy_.grants[winner];
        const bool exact_delete_grant = grant.delete_allowed && reals_[winner] && same_path(*reals_[winner], real, env_.eq);
        for (const auto& location : env_.locations.entries)
        {
            if (!location.enabled || location.absolute.empty()) continue;
            const auto* location_path = location_real(location.name);
            if (!location_path) continue;
            if (same_path(real, *location_path, env_.eq))
            {
                return fail(ErrorCode::denied, "the location \"" + location.name + "\" itself can't be removed");
            }
            if (!exact_delete_grant && is_proper_ancestor(real, *location_path, env_.eq))
            {
                return fail(ErrorCode::denied, "this folder contains the location \"" + location.name +
                    "\"; grant delete on exactly this folder to remove it");
            }
        }
        return Done{};
    }
};
}
