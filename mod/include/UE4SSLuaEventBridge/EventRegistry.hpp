#pragma once

#include "Event.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace UE4SSLuaEventBridge
{
using SessionId = std::uint64_t;
using SubscriptionId = std::uint64_t;
using Callback = std::function<void(const Event&)>;

struct SubscriptionSpec
{
    std::string source_type;
    std::string source_id;
    EventPhase phase{EventPhase::Triggered};
};

struct Subscription
{
    SubscriptionId id{};
    SessionId owner{};
    SubscriptionSpec spec;
    Callback callback;
};

class EventRegistry final
{
public:
    [[nodiscard]] SubscriptionId subscribe(
        SessionId owner,
        SubscriptionSpec spec,
        Callback callback);

    [[nodiscard]] bool unsubscribe(SessionId owner, SubscriptionId id);
    std::size_t unsubscribe_all(SessionId owner);
    std::size_t dispatch(const Event& event);

    [[nodiscard]] std::vector<Subscription> snapshot() const;

private:
    mutable std::mutex mutex_;
    SubscriptionId next_id_{1};
    std::unordered_map<SubscriptionId, Subscription> subscriptions_;
};
} // namespace UE4SSLuaEventBridge

