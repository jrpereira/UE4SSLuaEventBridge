#pragma once
#include <chrono>
#include <cstddef>

namespace UE4SSLuaEventBridge {
// Non-preemptive: one slow callback can exceed the time allowance. Always make
// at least one unit of progress; zero disables the corresponding limit.
class DispatchBudget {
public:
    using Clock = std::chrono::steady_clock;
    DispatchBudget(Clock::time_point start, std::size_t count, std::chrono::microseconds time)
        : start_(start), count_(count), time_(time) {}
    bool permits(std::size_t processed, Clock::time_point now) const {
        return processed == 0 || ((count_ == 0 || processed < count_) &&
            (time_.count() == 0 || now - start_ < time_));
    }
private:
    Clock::time_point start_;
    std::size_t count_;
    std::chrono::microseconds time_;
};
}
