#include <UE4SSLuaEventBridge/SessionAliasIndex.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <thread>
#include <vector>

using namespace UE4SSLuaEventBridge;

int main()
{
    struct State {};
    struct Session {};

    State parent, main_thread, async_thread, hook_thread, unrelated;
    Session first_session, second_session;
    SessionAliasIndex<State, Session> sessions;

    sessions.bind(nullptr, &first_session);
    sessions.bind(&parent, nullptr);
    assert(sessions.size() == 0);

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

    sessions.bind(&parent, &second_session);
    assert(sessions.size() == 5);
    assert(sessions.find(&parent) == &second_session);
    sessions.bind(&parent, &first_session);

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
