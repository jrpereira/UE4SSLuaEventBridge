#pragma once
#include <cstddef>
#include <cstdint>
namespace UE4SSLuaEventBridge {
// Used under the existing producer mutex; adds no mutex or atomic to each event.
struct QueueCapacity {
    std::size_t limit{65536};
    std::size_t high_water{};
    uint64_t rejected{};
    bool warning_pending{};
    bool overflow_reported{};
    bool admit(std::size_t depth) {
        if (limit != 0 && depth >= limit) {
            ++rejected;
            if (!overflow_reported) { warning_pending = true; overflow_reported = true; }
            return false;
        }
        overflow_reported = false;
        warning_pending = false;
        return true;
    }
    void accepted(std::size_t depth) { if (depth > high_water) high_water = depth; }
};
}
