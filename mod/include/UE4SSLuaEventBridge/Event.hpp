#pragma once

#include <cstdint>
#include <string>

namespace UE4SSLuaEventBridge
{
enum class EventPhase : std::uint8_t
{
    None = 0,
    Triggered = 1,
    Started = 2,
    Ongoing = 4,
    Canceled = 8,
    Completed = 16,
};

struct Event
{
    std::uint64_t subscription_id{};
    std::string source_type;
    std::string source_id;
    EventPhase phase{EventPhase::None};
    double elapsed_processed_seconds{};
    double elapsed_triggered_seconds{};
    double value_x{};
    double value_y{};
    double value_z{};
};
} // namespace UE4SSLuaEventBridge

