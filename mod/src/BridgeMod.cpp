#include <UE4SSLuaEventBridge/EnhancedInputBackend.hpp>
#include <UE4SSLuaEventBridge/SessionAliasIndex.hpp>
#include <UE4SSLuaEventBridge/UE4SSABI.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace UE4SSLuaEventBridge
{
struct LuaSession
{
    uint64_t id{};
    RC::LuaMadeSimple::Lua* lua{};
    std::atomic_bool active{true};
};
}

namespace
{
using RC::LuaMadeSimple::Lua;
using UE4SSLuaEventBridge::EnhancedInputABI::TriggerEvent;

class UE4SSLuaEventBridgeMod;
UE4SSLuaEventBridgeMod* active_mod{};

class UE4SSLuaEventBridgeMod final : public RC::CppUserModBase
{
public:
    UE4SSLuaEventBridgeMod()
    {
        ModName = L"UE4SSLuaEventBridge";
        ModVersion = L"0.2.3";
        ModDescription = L"Native Unreal event callbacks for UE4SS Lua mods";
        ModAuthors = L"UE4SS Lua Event Bridge contributors";
        ModIntendedSDKVersion = L"3.0.1-97b7e501";
        active_mod = this;
    }

    ~UE4SSLuaEventBridgeMod() override
    {
        backend_.shutdown();
        active_mod = nullptr;
    }

    void on_unreal_init() override { backend_.initialize(); }

    void on_update() override
    {
        for (auto& event : backend_.take_events())
        {
            const auto& subscription = event.owner;
            auto* session = subscription ? subscription->session : nullptr;
            if (!subscription || !subscription->active.load() || !session || !session->active.load() || !session->lua)
            {
                continue;
            }
            try
            {
                const auto& lua = *session->lua;
                lua.registry().get_function_ref(subscription->callback_ref);
                lua.set_integer(static_cast<int64_t>(event.subscription));
                lua.set_string(subscription->action_path_utf8);
                lua.set_string(subscription->phase_name);
                lua.set_float(event.elapsed_processed);
                lua.set_float(event.elapsed_triggered);
                lua.set_number(event.x);
                lua.set_number(event.y);
                lua.set_number(event.z);
                lua.set_integer(static_cast<int64_t>(event.value_type));
                lua.call_function(9, 0);
            }
            catch (...)
            {
                backend_.unsubscribe(*session, subscription->id);
            }
        }
    }

    void on_lua_start(RC::StringViewType, Lua& lua, Lua& main_lua, Lua& async_lua, Lua* hook_lua) override
    {
        auto session = std::make_unique<UE4SSLuaEventBridge::LuaSession>();
        session->id = next_session_id_.fetch_add(1);
        session->lua = &lua;
        auto* session_ptr = session.get();
        const auto register_state = [&](Lua* state) {
            if (!state) return;
            auto* raw_state = state->get_lua_state();
            if (!raw_state) return;
            session_index_.bind(raw_state, session_ptr);
        };
        register_state(&lua);
        register_state(&main_lua);
        register_state(&async_lua);
        register_state(hook_lua);
        sessions_.insert_or_assign(session_ptr->id, std::move(session));

        lua.register_function("UE4SSLuaEventBridge_GetVersion", &get_version);
        lua.register_function("UE4SSLuaEventBridge_GetCapabilities", &get_capabilities);
        lua.register_function("UE4SSLuaEventBridge_BindAction", &bind_action);
        lua.register_function("UE4SSLuaEventBridge_Unbind", &unbind);
        lua.register_function("UE4SSLuaEventBridge_UnbindAll", &unbind_all);
        lua.execute_string(R"lua(
            UE4SSLuaEventBridge = {
                API_VERSION = 1,
                GetVersion = UE4SSLuaEventBridge_GetVersion,
                GetCapabilities = function()
                    local api, enhancedInput, target = UE4SSLuaEventBridge_GetCapabilities()
                    return { api = api, enhanced_input = enhancedInput, target_ue4ss_commit = target }
                end,
                BindAction = function(action, event, callback)
                    assert(type(action) == "string", "action must be an object path")
                    assert(type(event) == "string", "event must be an ETriggerEvent name")
                    assert(type(callback) == "function", "callback must be a function")
                    local phases = {
                        Triggered = 1,
                        Started = 2,
                        Ongoing = 3,
                        Canceled = 4,
                        Completed = 5,
                    }
                    local phase = phases[event]
                    assert(phase ~= nil, "unsupported ETriggerEvent name")
                    return UE4SSLuaEventBridge_BindAction(action, phase,
                        function(handle, sourceAction, phase, elapsed, triggered, x, y, z, valueType)
                            callback({
                                subscription = handle,
                                source_type = "enhanced_input",
                                action = sourceAction,
                                phase = phase,
                                elapsed_processed = elapsed,
                                elapsed_triggered = triggered,
                                value = { x = x, y = y, z = z, type = valueType },
                            })
                        end)
                end,
                SubscribeEnhancedInput = function(spec, callback)
                    assert(type(spec) == "table", "spec must be a table")
                    assert(spec.receiver == nil or spec.receiver == "local_player",
                        "only receiver='local_player' is supported")
                    return UE4SSLuaEventBridge.BindAction(spec.action, spec.event, callback)
                end,
                Unbind = UE4SSLuaEventBridge_Unbind,
                Unsubscribe = UE4SSLuaEventBridge_Unbind,
                UnbindAll = UE4SSLuaEventBridge_UnbindAll,
                UnsubscribeAll = UE4SSLuaEventBridge_UnbindAll,
            }
        )lua");
    }

    void on_lua_stop(RC::StringViewType, Lua& lua, Lua&, Lua&, Lua*) override
    {
        auto* session = session_for(lua);
        if (!session) return;
        session->active.store(false);
        backend_.unsubscribe_all(*session);
        session_index_.unbind(session);
        const auto owned = sessions_.find(session->id);
        if (owned != sessions_.end())
        {
            retired_sessions_.push_back(std::move(owned->second));
            sessions_.erase(owned);
        }
    }

    UE4SSLuaEventBridge::LuaSession* session_for(const Lua& lua)
    {
        return session_index_.find(lua.get_lua_state());
    }

private:
    static int get_version(const Lua& lua)
    {
        lua.set_string("0.2.3");
        return 1;
    }

    static int get_capabilities(const Lua& lua)
    {
        lua.set_integer(1);
        lua.set_bool(active_mod && active_mod->backend_.available());
        lua.set_string("97b7e501");
        return 3;
    }

    static std::pair<TriggerEvent, std::string_view> parse_phase(int64_t phase)
    {
        if (phase == 1) return {TriggerEvent::Triggered, "Triggered"};
        if (phase == 2) return {TriggerEvent::Started, "Started"};
        if (phase == 3) return {TriggerEvent::Ongoing, "Ongoing"};
        if (phase == 4) return {TriggerEvent::Canceled, "Canceled"};
        if (phase == 5) return {TriggerEvent::Completed, "Completed"};
        return {TriggerEvent::None, {}};
    }

    static int bind_action(const Lua& lua)
    {
        if (!active_mod || !active_mod->backend_.available())
        {
            lua.set_nil();
            lua.set_string("Enhanced Input backend is not initialized");
            return 2;
        }
        auto* session = active_mod->session_for(lua);
        if (!session)
        {
            lua.set_nil();
            lua.set_string("Lua session is not registered");
            return 2;
        }
        if (!lua.is_function(3))
        {
            lua.set_nil();
            lua.set_string("callback must be a function");
            return 2;
        }

        const std::string action_path(lua.get_string(1));
        const auto [phase, phase_view] = parse_phase(lua.get_integer(2));
        const std::string phase_name(phase_view);
        if (action_path.empty() || phase == TriggerEvent::None)
        {
            lua.set_nil();
            lua.set_string("invalid action path or trigger event");
            return 2;
        }

        const int32_t callback_ref = lua.registry().make_ref();
        const uint64_t handle = active_mod->backend_.subscribe(
            *session, callback_ref, action_path, phase_name, phase);
        lua.set_integer(static_cast<int64_t>(handle));
        return 1;
    }

    static int unbind(const Lua& lua)
    {
        auto* session = active_mod ? active_mod->session_for(lua) : nullptr;
        const auto handle = static_cast<uint64_t>(lua.get_integer(1));
        lua.set_bool(session && active_mod->backend_.unsubscribe(*session, handle));
        return 1;
    }

    static int unbind_all(const Lua& lua)
    {
        auto* session = active_mod ? active_mod->session_for(lua) : nullptr;
        lua.set_integer(session ? static_cast<int64_t>(active_mod->backend_.unsubscribe_all(*session)) : 0);
        return 1;
    }

    UE4SSLuaEventBridge::EnhancedInputBackend backend_;
    std::atomic_uint64_t next_session_id_{1};
    std::unordered_map<uint64_t, std::unique_ptr<UE4SSLuaEventBridge::LuaSession>> sessions_;
    UE4SSLuaEventBridge::SessionAliasIndex<lua_State, UE4SSLuaEventBridge::LuaSession> session_index_;
    std::vector<std::unique_ptr<UE4SSLuaEventBridge::LuaSession>> retired_sessions_;
};
}

#define UE4SS_LUA_EVENT_BRIDGE_API __declspec(dllexport)
extern "C"
{
UE4SS_LUA_EVENT_BRIDGE_API RC::CppUserModBase* start_mod() { return new UE4SSLuaEventBridgeMod(); }
UE4SS_LUA_EVENT_BRIDGE_API void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }
}
