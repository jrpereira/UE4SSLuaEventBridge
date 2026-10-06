#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

namespace UE4SSLuaEventBridge
{
// Retry policy and status for the UObject layout probe that gates object
// lifetimes. A probe that runs before enough classes are loaded is retried a
// bounded number of times instead of disabling lifetimes for the process.
class LifetimeProbe
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t max_attempts = 10;
    static constexpr std::size_t required_classes = 2;
    static constexpr Clock::duration retry_interval = std::chrono::seconds(1);

    // Reserves an attempt when one is due; the caller must then record it.
    bool begin(Clock::time_point now)
    {
        std::scoped_lock lock(mutex_);
        if (ready_ || shut_down_ || attempts_ >= max_attempts) return false;
        if (attempts_ > 0 && now - last_attempt_ < retry_interval) return false;
        ++attempts_;
        last_attempt_ = now;
        return true;
    }

    void record(std::size_t checked, std::size_t verified)
    {
        std::scoped_lock lock(mutex_);
        checked_ = checked;
        verified_ = verified;
        ready_ = verified >= required_classes;
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
        if (attempts_ == 0) return std::string{"the UObject layout probe has not run"};
        auto text = "the UObject layout probe verified " + std::to_string(verified_) + " of " +
            std::to_string(checked_) + " classes (" + std::to_string(required_classes) + " required)";
        if (attempts_ >= max_attempts) return text + "; gave up after " + std::to_string(attempts_) + " attempts";
        return text + "; retrying on the game thread";
    }

private:
    mutable std::mutex mutex_;
    std::size_t attempts_{};
    std::size_t checked_{};
    std::size_t verified_{};
    Clock::time_point last_attempt_{};
    bool ready_{};
    bool shut_down_{};
};
}
