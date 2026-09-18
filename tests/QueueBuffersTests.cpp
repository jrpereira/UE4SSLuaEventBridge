#include <UE4SSLuaEventBridge/QueueBuffers.hpp>
#include <cassert>
#include <memory>
#include <thread>
#include <mutex>
using namespace UE4SSLuaEventBridge;
int main() {
 QueueReadiness ready;
 assert(!ready.pending());
 std::mutex mutex;
 std::vector<int> concurrent, concurrentSpare;
 constexpr int count = 10000;
 std::thread producer([&] {
     for (int i=0; i<count; ++i) {
         std::scoped_lock lock(mutex);
         concurrent.push_back(i);
         ready.publish();
     }
 });
 int received = 0;
 while (received < count) {
     if (!ready.pending()) { std::this_thread::yield(); continue; }
     std::vector<int> current;
     {
         std::scoped_lock lock(mutex);
         current=drain_queue(concurrent,concurrentSpare);
         ready.drained();
     }
     for (int value : current) { assert(value == received); ++received; }
     current.clear();
     { std::scoped_lock lock(mutex); retain_empty_buffer(concurrentSpare,current); }
 }
 producer.join();
 assert(!ready.pending() && concurrent.empty());
 // Enqueue immediately after a drain cannot be cleared by recycling.
 { std::scoped_lock lock(mutex); concurrent.push_back(count); ready.publish(); }
 assert(ready.pending());
 { std::scoped_lock lock(mutex); auto last=drain_queue(concurrent,concurrentSpare); ready.drained(); assert(last[0]==count); }

 std::vector<int> queue{1,2,3}, spare;
 auto batch=drain_queue(queue,spare);
 assert((batch==std::vector<int>{1,2,3}) && queue.empty());
 queue.push_back(4); // Producer adds data while consumer owns old batch.
 retain_empty_buffer(spare,batch);
 assert(spare.empty() && (batch==std::vector<int>{1,2,3})); // Cannot recycle live data.
 auto oldCapacity=batch.capacity(); batch.clear(); retain_empty_buffer(spare,batch);
 assert(queue.size()==1 && queue[0]==4 && spare.capacity()==oldCapacity);
 auto next=drain_queue(queue,spare);
 assert(next.size()==1 && next[0]==4 && queue.empty() && queue.capacity()==oldCapacity);
 std::vector<int> oversized; oversized.reserve(8192);
 retain_empty_buffer(spare,oversized); assert(spare.capacity()==0);
 std::vector<std::shared_ptr<int>> owned, reusable;
 auto owner=std::make_shared<int>(7); std::weak_ptr<int> weak=owner;
 owned.push_back(owner); owner.reset();
 auto ownedBatch=drain_queue(owned,reusable);
 assert(!weak.expired()); ownedBatch.clear(); assert(weak.expired());
 retain_empty_buffer(reusable,ownedBatch); assert(weak.expired());
}
