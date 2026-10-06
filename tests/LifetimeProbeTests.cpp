#include <LifetimeProbe.hpp>

#include <cassert>

using UE4SSLuaEventBridge::LifetimeProbe;

int main()
{
    const auto start = LifetimeProbe::Clock::time_point{} + std::chrono::hours(1);
    {
        LifetimeProbe probe;
        assert(probe.reason()->find("has not run") != std::string::npos);
        assert(probe.begin(start));
        probe.record(40, 1);
        assert(!probe.ready() && probe.reason()->find("verified 1 of 40") != std::string::npos);
        assert(probe.reason()->find("retrying") != std::string::npos);
        assert(!probe.begin(start + std::chrono::milliseconds(500)) && "retries are rate limited");
        assert(probe.begin(start + std::chrono::seconds(1)));
        probe.record(300, 2);
        assert(probe.ready() && !probe.reason());
        assert(!probe.begin(start + std::chrono::seconds(5)) && "a ready probe never runs again");
        probe.shut_down();
        assert(!probe.ready() && probe.reason()->find("shut down") != std::string::npos);
        assert(!probe.begin(start + std::chrono::seconds(10)));
    }
    {
        LifetimeProbe probe;
        auto now = start;
        for (std::size_t attempt = 0; attempt < LifetimeProbe::max_attempts; ++attempt)
        {
            assert(probe.begin(now));
            probe.record(10, 0);
            now += LifetimeProbe::retry_interval;
        }
        assert(!probe.begin(now + std::chrono::hours(1)) && "attempts are bounded");
        assert(probe.reason()->find("gave up after 10 attempts") != std::string::npos);
    }
}
