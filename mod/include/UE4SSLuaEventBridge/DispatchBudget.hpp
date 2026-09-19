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
    bool permits(std::size_t processed, Clock::time_point now, bool made_progress = false) const {
        return (processed == 0 && !made_progress) || ((count_ == 0 || processed < count_) &&
            (time_.count() == 0 || now - start_ < time_));
    }
private:
    Clock::time_point start_;
    std::size_t count_;
    std::chrono::microseconds time_;
};

// All per-item preparation (including trace formatting) belongs in consume.
// Return true for a live callback attempt, false for a discarded entry.
// Clock injection keeps the boundary testable without sleeps or timing noise.
template<class Backlog, class Consumer, class Now>
std::size_t dispatch_budgeted(Backlog& pending, const DispatchBudget& budget,
                             Consumer consume, Now now) {
    std::size_t processed{};
    bool made_progress = false;
    while (!pending.empty() && budget.permits(processed, now(), made_progress)) {
        made_progress = true;
        if (consume(pending.pop())) ++processed;
    }
    return processed;
}
}
