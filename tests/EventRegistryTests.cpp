#include <UE4SSLuaEventBridge/EventRegistry.hpp>
#include <UE4SSLuaEventBridge/SessionAliasIndex.hpp>

#include <cassert>
#include <array>
#include <cstddef>
#include <thread>
#include <vector>

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

    struct State {};
    struct Session {};
    State parent, main_thread, async_thread, hook_thread, unrelated;
    Session first_session, second_session;
    SessionAliasIndex<State, Session> sessions;

    sessions.bind(&parent, &first_session);
    sessions.bind(&main_thread, &first_session);
    sessions.bind(&async_thread, &first_session);
    sessions.bind(&hook_thread, &first_session);
    sessions.bind(&unrelated, &second_session);

    assert(sessions.size() == 5);
    assert(sessions.find(&parent) == &first_session);
    assert(sessions.find(&main_thread) == &first_session);
    assert(sessions.find(&async_thread) == &first_session);
    assert(sessions.find(&hook_thread) == &first_session);
    assert(sessions.find(&unrelated) == &second_session);

    assert(sessions.unbind(&first_session) == 4);
    assert(sessions.find(&parent) == nullptr);
    assert(sessions.find(&main_thread) == nullptr);
    assert(sessions.find(&unrelated) == &second_session);
    assert(sessions.size() == 1);

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t aliases_per_thread = 32;
    std::array<State, thread_count * aliases_per_thread> concurrent_states{};
    std::array<Session, thread_count> concurrent_sessions{};
    SessionAliasIndex<State, Session> concurrent_index;
    std::vector<std::thread> workers;

    for (std::size_t thread = 0; thread < thread_count; ++thread)
    {
        workers.emplace_back([&, thread] {
            for (std::size_t alias = 0; alias < aliases_per_thread; ++alias)
            {
                const auto index = thread * aliases_per_thread + alias;
                concurrent_index.bind(&concurrent_states[index], &concurrent_sessions[thread]);
                assert(concurrent_index.find(&concurrent_states[index]) == &concurrent_sessions[thread]);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    assert(concurrent_index.size() == concurrent_states.size());

    workers.clear();
    for (std::size_t thread = 0; thread < thread_count; ++thread)
    {
        workers.emplace_back([&, thread] {
            assert(concurrent_index.unbind(&concurrent_sessions[thread]) == aliases_per_thread);
        });
    }
    for (auto& worker : workers) worker.join();
    assert(concurrent_index.size() == 0);
}
