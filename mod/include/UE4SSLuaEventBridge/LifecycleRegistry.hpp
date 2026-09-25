#pragma once

#include <cstddef>
#include <compare>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace UE4SSLuaEventBridge
{
struct ObjectLifetimeIdentity
{
    std::uintptr_t address{};
    int32_t index{-1};
    int32_t serial{};

    auto operator<=>(const ObjectLifetimeIdentity&) const = default;
};

class LifecycleRegistry
{
public:
    enum class CaptureFailure { none, unknown_session, capacity, faulted };
    struct CaptureResult { uint64_t token{}; CaptureFailure failure{CaptureFailure::none}; };
    struct LostResult { std::optional<uint64_t> token; bool overflow{}; bool unknown_session{}; };

    explicit LifecycleRegistry(std::size_t observation_limit = 4096, std::size_t loss_limit = 4096)
        : observation_limit_(observation_limit), loss_limit_(loss_limit) {}

    void start_session(uint64_t session)
    {
        std::scoped_lock lock(mutex_);
        sessions_.try_emplace(session);
    }

    void stop_session(uint64_t session)
    {
        std::scoped_lock lock(mutex_);
        sessions_.erase(session);
    }

    CaptureResult capture(uint64_t session, ObjectLifetimeIdentity identity)
    {
        std::scoped_lock lock(mutex_);
        const auto found = sessions_.find(session);
        if (found == sessions_.end()) return {0, CaptureFailure::unknown_session};
        auto& state = found->second;
        if (state.overflow) return {0, CaptureFailure::faulted};
        for (const auto& [token, record] : state.observations)
        {
            if (record == identity) return {token, CaptureFailure::none};
        }
        if (state.observations.size() >= observation_limit_)
            return {0, CaptureFailure::capacity};
        const auto token = next_token_++;
        state.observations.emplace(token, identity);
        return {token, CaptureFailure::none};
    }

    bool valid(uint64_t session, uint64_t token, ObjectLifetimeIdentity identity) const
    {
        std::scoped_lock lock(mutex_);
        const auto found = sessions_.find(session);
        if (found == sessions_.end() || found->second.overflow) return false;
        const auto record = found->second.observations.find(token);
        return record != found->second.observations.end() && record->second == identity;
    }

    void invalidate(std::uintptr_t address, int32_t index)
    {
        std::scoped_lock lock(mutex_);
        for (auto& [_, state] : sessions_)
        {
            for (auto it = state.observations.begin(); it != state.observations.end();)
            {
                const auto& identity = it->second;
                if (identity.address != address && identity.index != index)
                {
                    ++it;
                    continue;
                }
                if (!state.overflow)
                {
                    if (state.lost.size() < loss_limit_) state.lost.push_back(it->first);
                    else
                    {
                        state.lost.clear();
                        state.overflow = true;
                    }
                }
                it = state.observations.erase(it);
            }
        }
    }

    LostResult take_lost(uint64_t session)
    {
        std::scoped_lock lock(mutex_);
        const auto found = sessions_.find(session);
        if (found == sessions_.end()) return {{}, false, true};
        auto& state = found->second;
        if (state.overflow) return {{}, true, false};
        if (state.lost.empty()) return {};
        const auto token = state.lost.front();
        state.lost.pop_front();
        return {token, false, false};
    }

    void shutdown()
    {
        std::scoped_lock lock(mutex_);
        sessions_.clear();
    }

private:
    struct SessionState
    {
        std::unordered_map<uint64_t, ObjectLifetimeIdentity> observations;
        std::deque<uint64_t> lost;
        bool overflow{};
    };

    const std::size_t observation_limit_;
    const std::size_t loss_limit_;
    mutable std::mutex mutex_;
    uint64_t next_token_{1};
    std::unordered_map<uint64_t, SessionState> sessions_;
};
}
