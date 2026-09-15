#include <UE4SSLuaEventBridge/EventRegistry.hpp>

#include <cassert>
#include <cstddef>

using namespace UE4SSLuaEventBridge;

int main()
{
    EventRegistry registry;
    std::size_t owner_one_calls{};
    std::size_t owner_two_calls{};

    const auto first = registry.subscribe(
        1,
        {"enhanced_input", "/Game/Input/IA_Test", EventPhase::Triggered},
        [&](const Event&) { ++owner_one_calls; });

    const auto second = registry.subscribe(
        2,
        {"enhanced_input", "/Game/Input/IA_Test", EventPhase::Triggered},
        [&](const Event&) { ++owner_two_calls; });

    Event event{};
    event.source_type = "enhanced_input";
    event.source_id = "/Game/Input/IA_Test";
    event.phase = EventPhase::Triggered;

    assert(registry.dispatch(event) == 2);
    assert(owner_one_calls == 1);
    assert(owner_two_calls == 1);

    assert(!registry.unsubscribe(2, first));
    assert(registry.unsubscribe(1, first));
    assert(registry.dispatch(event) == 1);
    assert(owner_one_calls == 1);
    assert(owner_two_calls == 2);

    assert(registry.unsubscribe_all(2) == 1);
    assert(registry.snapshot().empty());
    assert(!registry.unsubscribe(2, second));
}

