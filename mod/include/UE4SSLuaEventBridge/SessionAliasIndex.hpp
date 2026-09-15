#pragma once

#include <cstddef>
#include <unordered_map>

namespace UE4SSLuaEventBridge
{
template <typename State, typename Session>
class SessionAliasIndex
{
public:
    void bind(State* state, Session* session)
    {
        if (state && session) aliases_.insert_or_assign(state, session);
    }

    [[nodiscard]] Session* find(State* state) const
    {
        const auto it = aliases_.find(state);
        return it == aliases_.end() ? nullptr : it->second;
    }

    std::size_t unbind(Session* session)
    {
        std::size_t removed{};
        for (auto it = aliases_.begin(); it != aliases_.end();)
        {
            if (it->second == session)
            {
                it = aliases_.erase(it);
                ++removed;
            }
            else
            {
                ++it;
            }
        }
        return removed;
    }

    [[nodiscard]] std::size_t size() const { return aliases_.size(); }

private:
    std::unordered_map<State*, Session*> aliases_;
};
}
