#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace UE4SSLuaEventBridge
{
// Status of the UObject layout probe that gates object lifetimes. The probe
// runs once at Unreal initialization: it checks offsets against objects that
// already sit in the object array then, so a failure is a layout mismatch that
// a later attempt would not change, and registering object-array listeners
// mid-game could race the engine's loading threads.
class LifetimeProbe
{
public:
    static constexpr std::size_t required_objects = 2;

    void record(std::size_t checked, std::size_t verified)
    {
        std::scoped_lock lock(mutex_);
        ran_ = true;
        checked_ = checked;
        verified_ = verified;
        ready_ = verified >= required_objects;
    }

    void shut_down()
    {
        std::scoped_lock lock(mutex_);
        shut_down_ = true;
        ready_ = false;
    }

    bool ready() const
    {
        std::scoped_lock lock(mutex_);
        return ready_;
    }

    // Why lifetimes are unavailable, or nothing when they are available.
    std::optional<std::string> reason() const
    {
        std::scoped_lock lock(mutex_);
        if (shut_down_) return std::string{"the UObject array has shut down"};
        if (ready_) return std::nullopt;
        if (!ran_) return std::string{"the UObject layout probe has not run"};
        if (checked_ == 0) return std::string{"the UObject layout probe found no live objects to check"};
        return "the UObject layout probe verified " + std::to_string(verified_) + " of " +
            std::to_string(checked_) + " objects (" + std::to_string(required_objects) + " required)";
    }

private:
    mutable std::mutex mutex_;
    std::size_t checked_{};
    std::size_t verified_{};
    bool ran_{};
    bool ready_{};
    bool shut_down_{};
};
}
