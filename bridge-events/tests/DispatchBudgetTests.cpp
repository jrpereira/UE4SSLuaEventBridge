#include <DispatchBudget.hpp>
#include <QueueCapacity.hpp>
#include <DispatchBacklog.hpp>
#include <QueueBuffers.hpp>
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

    // Exercise the production buffer/capacity/backlog components together under
    // sustained overload. Accepted IDs must survive partial passes exactly once
    // and in order, even while the producer fills a second batch.
    QueueCapacity bounded{11};
    std::vector<int> events, spare, accepted_ids, delivered_ids;
    DispatchBacklog<int> pending;
    QueueReadiness ready;
    auto dispatch = [&] {
        if (pending.empty()) {
            auto buffer = pending.release_buffer();
            buffer.clear();
            retain_empty_buffer(spare, buffer);
            if (ready.pending()) {
                pending.load(drain_queue(events, spare));
                ready.drained();
            }
        }
        DispatchBudget pass(zero, 3, 0us);
        std::size_t count{};
        while (!pending.empty() && pass.permits(count, zero)) {
            delivered_ids.push_back(pending.pop());
            ++count;
        }
        assert(count <= 3);
    };
    for (int id = 0; id < 10000; ++id) {
        if (bounded.admit(events.size())) {
            events.push_back(id);
            accepted_ids.push_back(id);
            bounded.accepted(events.size());
            ready.publish();
        }
        if (id % 10 == 0) dispatch();
        assert(events.size() <= bounded.limit);
        assert(pending.size() <= bounded.limit);
        assert(events.size() + pending.size() <= 2 * bounded.limit);
    }
    assert(bounded.rejected > 0 && bounded.high_water == bounded.limit);
    assert(accepted_ids.size() + bounded.rejected == 10000);
    while (ready.pending() || !pending.empty()) dispatch();
    assert(delivered_ids == accepted_ids);
    assert(!ready.pending() && events.empty() && pending.empty());
    // A new burst after complete draining must recover admission and readiness.
    assert(bounded.admit(events.size()));
    events.push_back(10000);
    ready.publish();
    dispatch();
    assert(delivered_ids.back() == 10000 && !ready.pending() && pending.empty());

    // Exercise the production dispatch loop: simulated expensive preparation
    // must only run for admitted items, even with a large pending batch.
    DispatchBacklog<int> traced;
    std::vector<int> burst;
    for (int i = 0; i < 10000; ++i) burst.push_back(i);
    traced.load(std::move(burst));
    auto now = zero;
    int prepared = 0, completed = 0;
    const auto consume = [&](int item) {
        assert(item == completed);
        ++prepared;
        now += 1500us; // Trace/preparation cost counts toward the same budget.
        ++completed;
        return true;
    };
    DispatchBudget traced_budget(now, 256, 2000us);
    assert(dispatch_budgeted(traced, traced_budget, consume, [&] { return now; }) == 2);
    assert(prepared == 2 && completed == 2 && traced.size() == 9998);
    // Already-expired setup still permits one item of progress, not a batch.
    DispatchBudget expired(now, 256, 2000us);
    now += 5ms;
    assert(dispatch_budgeted(traced, expired, consume, [&] { return now; }) == 1);
    assert(prepared == 3 && completed == 3 && traced.size() == 9997);
    DispatchBudget count_only(now, 1, 0us);
    assert(dispatch_budgeted(traced, count_only, consume, [&] { return now; }) == 1);
    assert(prepared == 4 && completed == 4);

    // Use the same dispatcher as BridgeMod. Cancellation and session teardown
    // suppress old callbacks without charging the live callback allowance.
    struct Item { int id; bool binding_active; bool session_active; };
    DispatchBacklog<Item> cancelled;
    std::vector<Item> stale;
    for (int i = 0; i < 65536; ++i) stale.push_back({i, false, true});
    cancelled.load(std::move(stale));
    now = zero;
    int examined = 0;
    std::vector<int> live;
    auto route = [&](Item item) {
        ++examined;
        now += 1us; // Includes discard work, not just callback work.
        if (!item.binding_active || !item.session_active) return false;
        live.push_back(item.id);
        return true;
    };
    DispatchBudget cancel_budget(now, 256, 2000us);
    assert(dispatch_budgeted(cancelled, cancel_budget, route, [&] { return now; }) == 0);
    assert(examined == 2000 && cancelled.size() == 63536 && live.empty());
    // An expired pass processes exactly one canceled entry, then stops.
    DispatchBudget cancel_expired(now, 256, 2000us);
    now += 3ms;
    assert(dispatch_budgeted(cancelled, cancel_expired, route, [&] { return now; }) == 0);
    assert(examined == 2001 && cancelled.size() == 63535);
    // No time limit: discard all stale entries despite the live count limit.
    DispatchBudget discard_all(now, 2, 0us);
    assert(dispatch_budgeted(cancelled, discard_all, route, [&] { return now; }) == 0);
    assert(cancelled.empty() && examined == 65536);
    // Model producer refill with interleaved dead bindings, stopped sessions,
    // and new binding IDs. New live events retain FIFO and the count cap.
    cancelled.load({{100, false, true}, {101, true, false}, {102, true, true},
                    {103, false, true}, {104, true, true}, {105, true, true}});
    assert(dispatch_budgeted(cancelled, discard_all, route, [&] { return now; }) == 2);
    assert((live == std::vector<int>{102, 104}) && cancelled.size() == 1);
    assert(dispatch_budgeted(cancelled, discard_all, route, [&] { return now; }) == 1);
    assert((live == std::vector<int>{102, 104, 105}) && cancelled.empty());
}
