#pragma once

// Atomic replace sequencing (API.md "Atomic write"), behind an interface so
// the Win32 calls stay thin and the order of steps is testable.

#include <FileBridgeOutcome.hpp>

#include <cstdint>
#include <string>

namespace UE4SSLuaFileBridge::Core
{
class AtomicReplaceOps
{
public:
    virtual ~AtomicReplaceOps() = default;
    // CREATE_NEW the temporary file, write everything, FlushFileBuffers, close.
    virtual Status fill_temp(const std::string& temp) = 0;
    virtual Outcome<bool> target_exists(const std::string& target) = 0;
    // ReplaceFileW(target, temp, backup or none); keeps attributes and ACL.
    virtual Status replace(const std::string& target, const std::string& temp, const std::string& backup) = 0;
    // MoveFileExW(temp, target, MOVEFILE_WRITE_THROUGH), failing if target exists.
    virtual Status rename_new(const std::string& temp, const std::string& target) = 0;
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

// 1. fill the temporary sibling; 2. replace an existing target (keeping
// `backup` when non-empty) or rename onto a missing one; on any failure the
// temporary file is discarded and the target is unchanged. `overwrite` false
// refuses an existing target with `exists`.
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
    Status placed = Done{};
    if (exists.value())
    {
        if (!overwrite)
        {
            ops.discard(temp);
            return fail(ErrorCode::exists, "the destination exists");
        }
        placed = ops.replace(target, temp, backup);
    }
    else
    {
        placed = ops.rename_new(temp, target);
    }
    if (!placed) ops.discard(temp);
    return placed;
}
}
