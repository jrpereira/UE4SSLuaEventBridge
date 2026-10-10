#pragma once

// Atomic replace sequencing (API.md "Atomic write"), behind an interface so
// the Win32 calls stay thin and the order of steps is testable.

#include <FileBridgeOutcome.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace UE4SSLuaFileBridge::Core
{
// How a failed ReplaceFileW left the files (Win32 docs for ReplaceFile).
enum class ReplaceFault
{
    unchanged,   // both files keep their names: the temporary file may be discarded
    target_free, // the target name no longer exists: the new content is only in the temporary file
};

inline constexpr uint32_t error_unable_to_remove_replaced = 1175;
inline constexpr uint32_t error_unable_to_move_replacement = 1176;
inline constexpr uint32_t error_unable_to_move_replacement_2 = 1177;

// 1176 without a backup name: "the replaced file no longer exists and the
// replacement file exists under its original name". 1177: "the file being
// replaced still exists with a different name" (the backup, if given). 1175
// and every other error leave both files as they were.
constexpr ReplaceFault classify_replace_error(uint32_t win32, bool has_backup)
{
    if (win32 == error_unable_to_move_replacement_2) return ReplaceFault::target_free;
    if (win32 == error_unable_to_move_replacement && !has_backup) return ReplaceFault::target_free;
    return ReplaceFault::unchanged;
}

struct ReplaceResult
{
    std::optional<Failure> failure; // empty: replaced
    ReplaceFault fault{ReplaceFault::unchanged};
};

inline constexpr int rename_attempts_after_partial_replace = 3;

class AtomicReplaceOps
{
public:
    virtual ~AtomicReplaceOps() = default;
    // CREATE_NEW the temporary file, write everything, FlushFileBuffers, close.
    virtual Status fill_temp(const std::string& temp) = 0;
    virtual Outcome<bool> target_exists(const std::string& target) = 0;
    // ReplaceFileW(target, temp, backup or none); keeps attributes and ACL.
    virtual ReplaceResult replace(const std::string& target, const std::string& temp, const std::string& backup) = 0;
    // MoveFileExW(temp, target, MOVEFILE_WRITE_THROUGH), failing if target exists.
    virtual Status rename_new(const std::string& temp, const std::string& target) = 0;
    // Short wait between rename attempts after a partial replace.
    virtual void pause() = 0;
    // Best effort; never fails the operation.
    virtual void discard(const std::string& temp) = 0;
};

inline std::string temp_sibling(const std::string& target, uint32_t pid, uint64_t counter)
{
    return target + ".xbtmp-" + std::to_string(pid) + "-" + std::to_string(counter);
}

inline std::string backup_sibling(const std::string& target)
{
    return target + ".bak";
}

// A name made by temp_sibling: "<name>.xbtmp-<pid>-<n>". Returns the pid, or
// nullopt for any other name.
inline std::optional<uint32_t> temp_sibling_pid(std::string_view name)
{
    const auto at = name.rfind(".xbtmp-");
    if (at == std::string_view::npos || at == 0) return std::nullopt;
    auto rest = name.substr(at + 7);
    const auto dash = rest.find('-');
    if (dash == std::string_view::npos || dash == 0 || dash + 1 == rest.size()) return std::nullopt;
    uint64_t pid = 0;
    for (std::size_t i = 0; i < rest.size(); ++i)
    {
        if (i == dash) continue;
        if (rest[i] < '0' || rest[i] > '9') return std::nullopt;
        if (i < dash)
        {
            pid = pid * 10 + static_cast<uint64_t>(rest[i] - '0');
            if (pid > 0xFFFFFFFFull) return std::nullopt;
        }
    }
    return static_cast<uint32_t>(pid);
}

// After ReplaceFileW freed the target name, puts the temporary file there.
// Never discards it: if every attempt fails, the new content stays in `temp`
// and the failure names it (and the backup, which holds the old content).
inline Status finish_partial_replace(
    AtomicReplaceOps& ops, const std::string& target, const std::string& temp, const std::string& backup,
    const Failure& cause)
{
    Status placed = Done{};
    for (int attempt = 0; attempt < rename_attempts_after_partial_replace; ++attempt)
    {
        if (attempt) ops.pause();
        placed = ops.rename_new(temp, target);
        if (placed) return placed;
    }
    std::string message = cause.message + "; the new content is kept in " + temp;
    if (!backup.empty()) message += " and the previous content in " + backup;
    return fail(ErrorCode::io, std::move(message));
}

// 1. fill the temporary sibling; 2. replace an existing target (keeping
// `backup` when non-empty) or rename onto a missing one. On a failure that
// leaves the target unchanged the temporary file is discarded; when
// ReplaceFileW has already freed the target name, the rename is retried and
// the temporary file is never discarded. `overwrite` false refuses an
// existing target with `exists`.
inline Status atomic_replace(
    AtomicReplaceOps& ops, const std::string& target, const std::string& temp, const std::string& backup, bool overwrite)
{
    auto filled = ops.fill_temp(temp);
    if (!filled)
    {
        ops.discard(temp);
        return filled;
    }
    auto exists = ops.target_exists(target);
    if (!exists)
    {
        ops.discard(temp);
        return exists.failure();
    }
    if (!exists.value())
    {
        auto placed = ops.rename_new(temp, target);
        if (!placed) ops.discard(temp);
        return placed;
    }
    if (!overwrite)
    {
        ops.discard(temp);
        return fail(ErrorCode::exists, "the destination exists");
    }
    const auto replaced = ops.replace(target, temp, backup);
    if (!replaced.failure) return Done{};
    if (replaced.fault == ReplaceFault::target_free)
    {
        return finish_partial_replace(ops, target, temp, backup, *replaced.failure);
    }
    ops.discard(temp);
    return *replaced.failure;
}
}
