#pragma once
#include <vector>
#include <atomic>
#include <cstddef>
namespace UE4SSLuaEventBridge {
// Publication/reset must happen under the same mutex as enqueue/drain.
// The consumer uses this only to avoid locking an empty queue. A racing
// enqueue may be observed at the next host update, as with normal draining.
class QueueReadiness {
public:
    bool pending() const { return pending_.load(std::memory_order_acquire); }
    void publish() { pending_.store(true, std::memory_order_release); }
    void drained() { pending_.store(false, std::memory_order_release); }
private:
    std::atomic_bool pending_{false};
};
// Caller holds the queue mutex. Neither operation changes queued item order.
template<class T> std::vector<T> drain_queue(std::vector<T>& queue, std::vector<T>& spare) {
    std::vector<T> batch;
    batch.swap(queue);
    queue.swap(spare);
    return batch;
}
template<class T> void retain_empty_buffer(std::vector<T>& spare, std::vector<T>& batch) {
    // Payloads must be cleared before taking the mutex. Retain only modest
    // allocations so a one-off burst cannot establish an unlimited idle cost.
    constexpr std::size_t max_retained_elements = 4096;
    if (batch.empty() && batch.capacity() <= max_retained_elements && batch.capacity() > spare.capacity())
        spare.swap(batch);
}
}
