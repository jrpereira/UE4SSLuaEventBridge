#include <UE4SSLuaEventBridge/EventRegistry.hpp>

#include <stdexcept>
#include <utility>

namespace UE4SSLuaEventBridge
{
SubscriptionId EventRegistry::subscribe(
    SessionId owner,
    SubscriptionSpec spec,
    Callback callback)
{
    if (owner == 0)
    {
        throw std::invalid_argument("owner session must be non-zero");
    }
    if (spec.source_type.empty() || spec.source_id.empty())
    {
        throw std::invalid_argument("event source type and id are required");
    }
    if (!callback)
    {
        throw std::invalid_argument("callback is required");
    }

    std::scoped_lock lock{mutex_};
    const auto id = next_id_++;
    subscriptions_.emplace(id, Subscription{id, owner, std::move(spec), std::move(callback)});
    return id;
}

bool EventRegistry::unsubscribe(SessionId owner, SubscriptionId id)
{
    std::scoped_lock lock{mutex_};
    const auto it = subscriptions_.find(id);
    if (it == subscriptions_.end() || it->second.owner != owner)
    {
        return false;
    }
    subscriptions_.erase(it);
    return true;
}

std::size_t EventRegistry::unsubscribe_all(SessionId owner)
{
    std::scoped_lock lock{mutex_};
    std::size_t removed{};
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();)
    {
        if (it->second.owner == owner)
        {
            it = subscriptions_.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

std::size_t EventRegistry::dispatch(const Event& event)
{
    std::vector<Callback> callbacks;
    {
        std::scoped_lock lock{mutex_};
        for (const auto& [id, subscription] : subscriptions_)
        {
            if (subscription.spec.source_type == event.source_type &&
                subscription.spec.source_id == event.source_id &&
                subscription.spec.phase == event.phase)
            {
                callbacks.push_back(subscription.callback);
            }
        }
    }

    for (const auto& callback : callbacks)
    {
        callback(event);
    }
    return callbacks.size();
}

std::vector<Subscription> EventRegistry::snapshot() const
{
    std::scoped_lock lock{mutex_};
    std::vector<Subscription> result;
    result.reserve(subscriptions_.size());
    for (const auto& [id, subscription] : subscriptions_)
    {
        result.push_back(subscription);
    }
    return result;
}
} // namespace UE4SSLuaEventBridge

