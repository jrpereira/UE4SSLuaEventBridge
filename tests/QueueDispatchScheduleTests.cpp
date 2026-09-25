#include <QueueDispatchSchedule.hpp>
#include <cassert>
#include <chrono>
using namespace UE4SSLuaEventBridge;
using namespace std::chrono_literals;
int main()
{
    assert(parse_queue_check_rate("") == 20);
    assert(parse_queue_check_rate("30") == 30);
    assert(parse_queue_check_rate("60") == 60);
    assert(parse_queue_check_rate("1") == 1);
    assert(parse_queue_check_rate("1000") == 1000);
    for (auto invalid : {"0", "-1", "1001", "30.5", "60x", " 60", "4294967296"})
        assert(parse_queue_check_rate(invalid) == 20);
    const QueueDispatchSchedule::Clock::time_point zero{};
    QueueDispatchSchedule schedule;
    assert(schedule.due(zero));
    assert(!schedule.due(zero + 49ms));
    assert(schedule.due(zero + 50ms));
    assert(!schedule.due(zero + 50ms));
    assert(schedule.due(zero + 10s));
    assert(!schedule.due(zero + 10s + 1ms));
    QueueDispatchSchedule host;
    int count = 0;
    for (int ms = 0; ms < 1000; ms += 5)
        if (host.due(zero + std::chrono::milliseconds(ms))) ++count;
    assert(count == 20);
    QueueDispatchSchedule sixty(60);
    assert(sixty.due(zero));
    assert(!sixty.due(zero + 16ms));
    assert(sixty.due(zero + 17ms));
    QueueDispatchSchedule one(1);
    assert(one.due(zero));
    assert(!one.due(zero + 999ms));
    assert(one.due(zero + 1s));
    QueueDispatchSchedule invalid(0);
    assert(invalid.due(zero));
    assert(!invalid.due(zero + 49ms));
    assert(invalid.due(zero + 50ms));
}