#include <LifecycleRegistry.hpp>

#include <cassert>

using UE4SSLuaEventBridge::LifecycleRegistry;
using UE4SSLuaEventBridge::ObjectLifetimeIdentity;

int main()
{
    LifecycleRegistry registry(2, 1);
    registry.start_session(1);
    registry.start_session(2);
    const ObjectLifetimeIdentity first{0x1000, 7, 40};
    const ObjectLifetimeIdentity second{0x2000, 8, 50};
    const ObjectLifetimeIdentity replacement{0x1000, 7, 41};

    const auto one = registry.capture(1, first);
    assert(one.token > 0 && registry.capture(1, first).token == one.token);
    const auto other_session = registry.capture(2, first);
    assert(other_session.token > one.token);
    assert(registry.valid(1, one.token, first));
    assert(!registry.valid(2, one.token, first));

    registry.invalidate(first.address, first.index);
    assert(!registry.valid(1, one.token, first));
    assert(registry.take_lost(1).token == one.token);
    assert(registry.take_lost(2).token == other_session.token);
    const auto replacement_token = registry.capture(1, replacement);
    assert(replacement_token.token > other_session.token);

    const auto two = registry.capture(1, second);
    assert(two.token > 0);
    const auto over_capacity = registry.capture(1, {0x3000, 9, 60});
    assert(over_capacity.failure == LifecycleRegistry::CaptureFailure::capacity);

    registry.invalidate(replacement.address, replacement.index);
    registry.invalidate(second.address, second.index);
    const auto overflow = registry.take_lost(1);
    assert(overflow.overflow && !registry.valid(1, two.token, second));
    assert(registry.capture(1, first).failure == LifecycleRegistry::CaptureFailure::faulted);

    registry.stop_session(1);
    assert(registry.take_lost(1).unknown_session);
    registry.start_session(1);
    assert(registry.capture(1, first).token > two.token);
}
