#pragma once
#include <charconv>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace UE4SSLuaEventBridge
{
inline constexpr uint32_t default_queue_checks_per_second = 20;

inline uint32_t parse_queue_check_rate(std::string_view value)
{
    if (value.empty()) return default_queue_checks_per_second;
    uint32_t rate{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), rate);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        rate < 1 || rate > 1000)
        return default_queue_checks_per_second;
    return rate;
}

class QueueDispatchSchedule
{
public:
    using Clock = std::chrono::steady_clock;
    explicit QueueDispatchSchedule(uint32_t rate = default_queue_checks_per_second)
        : interval_(std::chrono::ceil<Clock::duration>(std::chrono::nanoseconds(
              (1000000000LL + validated_rate(rate) - 1) / validated_rate(rate))))
    {
    }

    bool due(Clock::time_point now)
    {
        if (started_ && now < next_) return false;
        started_ = true;
        // No catch-up bursts after a pause or slow callback.
        next_ = now + interval_;
        return true;
    }

private:
    static uint32_t validated_rate(uint32_t rate)
    {
        return rate >= 1 && rate <= 1000 ? rate : default_queue_checks_per_second;
    }
    Clock::duration interval_;
    Clock::time_point next_{};
    bool started_{};
};
}