#include <UE4SSLuaEventBridge/DispatchBudget.hpp>
#include <UE4SSLuaEventBridge/QueueCapacity.hpp>
#include <UE4SSLuaEventBridge/DispatchBacklog.hpp>
#include <cassert>
#include <chrono>
#include <deque>
#include <memory>
using namespace UE4SSLuaEventBridge;
using namespace std::chrono_literals;
int main() {
    const DispatchBudget::Clock::time_point zero{};
    DispatchBudget budget(zero, 3, 2ms);
    assert(budget.permits(0, zero + 10ms)); // Slow first callback cannot starve work.
    assert(budget.permits(2, zero + 1ms));
    assert(!budget.permits(3, zero + 1ms));
    assert(!budget.permits(1, zero + 2ms));
    DispatchBudget unlimited(zero, 0, 0us);
    assert(unlimited.permits(100000, zero + 1h));
    std::deque<int> queue;
    for (int i=0; i<10000; ++i) queue.push_back(i);
    int received=0;
    while (!queue.empty()) {
        std::size_t processed=0;
        DispatchBudget pass(zero, 256, 0us);
        while (!queue.empty() && pass.permits(processed, zero)) {
            assert(queue.front()==received++);
            queue.pop_front(); ++processed;
        }
        assert(processed<=256);
    }
    assert(received==10000);
    QueueCapacity capacity{3};
    for (std::size_t n=0; n<3; ++n) { assert(capacity.admit(n)); capacity.accepted(n+1); }
    assert(!capacity.admit(3) && capacity.rejected==1 && capacity.warning_pending);
    capacity.warning_pending=false;
    assert(!capacity.admit(3) && !capacity.warning_pending); // No log storm.
    assert(capacity.high_water==3);
    assert(capacity.admit(0)); // Recovery after draining retains diagnostics.
    capacity.limit=0;
    assert(capacity.admit(1000000));
    DispatchBacklog<std::unique_ptr<int>> backlog;
    std::vector<std::unique_ptr<int>> producer;
    for(int i=0;i<1000;++i) producer.push_back(std::make_unique<int>(i));
    backlog.load(std::move(producer));
    bool rejected=false;
    try { backlog.release_buffer(); } catch(const std::logic_error&) { rejected=true; }
    assert(rejected);
    int expected=0;
    while(!backlog.empty()) {
        DispatchBudget pass(zero, 7, 0us);
        std::size_t processed=0;
        while(!backlog.empty() && pass.permits(processed,zero)) {
            auto event=backlog.pop();
            assert(*event==expected++); ++processed;
        }
    }
    auto recycled=backlog.release_buffer();
    for(const auto& entry:recycled) assert(!entry); // No retained payload owners.
    assert(expected==1000 && backlog.empty());
    std::vector<std::unique_ptr<int>> next;
    next.push_back(std::make_unique<int>(1000));
    backlog.load(std::move(next));
    assert(*backlog.pop()==1000);
}
