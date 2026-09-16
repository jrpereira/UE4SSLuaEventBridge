#include <UE4SSLuaEventBridge/EnhancedInputBackend.hpp>
#include <UE4SSLuaEventBridge/EmbeddedLuaAPI.hpp>
#include <UE4SSLuaEventBridge/SessionAliasIndex.hpp>
#include <UE4SSLuaEventBridge/UE4SSABI.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#define UE4SSLEB_WINAPI __stdcall
#else
#define UE4SSLEB_WINAPI
#endif

extern "C" __declspec(dllimport) int UE4SSLEB_WINAPI GetModuleHandleExW(
    unsigned long flags,
    const wchar_t* module_address,
    void** module);

#undef UE4SSLEB_WINAPI

namespace UE4SSLuaEventBridge
{
struct LuaSession
{
    uint64_t id{};
    RC::LuaMadeSimple::Lua* lua{};
    int32_t dispatcher_ref{};
    std::atomic_bool active{true};
};
}

namespace
{
using RC::LuaMadeSimple::Lua;
using UE4SSLuaEventBridge::EnhancedInputABI::TriggerEvent;

constexpr int32_t packed_path_word_count = 64;
constexpr int32_t packed_path_max_length = packed_path_word_count * 8;

class UE4SSLuaEventBridgeMod;
UE4SSLuaEventBridgeMod* active_mod{};

bool pin_current_module()
{
    constexpr unsigned long from_address = 0x00000004UL;
    static const bool pinned = [] {
        void* module{};
        return GetModuleHandleExW(
                   from_address,
                   reinterpret_cast<const wchar_t*>(&active_mod),
                   &module) != 0;
    }();
    return pinned;
}


bool decode_packed_path(const Lua& lua, std::string& path, std::string& error)
{
    const auto length = lua.get_integer(1);
    if (length <= 0 || length > packed_path_max_length)
    {
        error = "invalid encoded object path length";
        return false;
    }

    std::array<uint64_t, packed_path_word_count> words{};
    for (int32_t index = 0; index < packed_path_word_count; ++index)
    {
        words[static_cast<std::size_t>(index)] = static_cast<uint64_t>(lua.get_integer(1));
    }

    path.assign(static_cast<std::size_t>(length), '\0');
    for (int64_t index = 0; index < length; ++index)
    {
        const auto word = words[static_cast<std::size_t>(index / 8)];
        const auto shift = static_cast<uint32_t>((index % 8) * 8);
        const auto byte = static_cast<uint8_t>((word >> shift) & 0xFFu);
        if (byte == 0)
        {
            error = "invalid encoded object path byte";
            return false;
        }
        path[static_cast<std::size_t>(index)] = static_cast<char>(byte);
    }
    return true;
}

class UE4SSLuaEventBridgeMod final : public RC::CppUserModBase
{
public:
    UE4SSLuaEventBridgeMod()
    {
        ModName = L"UE4SSLuaEventBridge";
        ModVersion = L"0.3.0";
        ModDescription = L"Game-agnostic native Enhanced Input callbacks for UE4SS Lua mods";
        ModAuthors = L"UE4SS Lua Event Bridge contributors";
        ModIntendedSDKVersion = L"3.0.1-97b7e501";
        active_mod = this;
    }

    ~UE4SSLuaEventBridgeMod() override
    {
        (void)backend_.shutdown();
        active_mod = nullptr;
    }

    [[nodiscard]] bool prepare_for_unload() { return backend_.shutdown(); }

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
                lua.registry().get_function_ref(session->dispatcher_ref);
                lua.set_integer(static_cast<int64_t>(subscription->callback_token));
                lua.set_integer(static_cast<int64_t>(event.subscription));
                lua.set_string(subscription->action_path_utf8);
                lua.set_string(subscription->phase_name);
                lua.set_float(event.elapsed_processed);
                lua.set_float(event.elapsed_triggered);
                lua.set_number(event.x);
                lua.set_number(event.y);
                lua.set_number(event.z);
                lua.set_integer(static_cast<int64_t>(event.value_type));
                lua.call_function(10, 0);
            }
            catch (...)
            {
                // Lua callbacks run on UE4SS's event-loop thread. Deactivate
                // immediately, then let the next game-thread bridge operation
                // detach the native binding.
                backend_.deactivate(*session, subscription->id);
            }
        }
    }

    void on_lua_start(RC::StringViewType, Lua& lua, Lua& main_lua, Lua& async_lua, Lua* hook_lua) override
    {
        auto session = std::make_unique<UE4SSLuaEventBridge::LuaSession>();
        session->id = next_session_id_.fetch_add(1);
        session->lua = &lua;
        auto* session_ptr = session.get();

        lua.register_function("UE4SSLuaEventBridge_GetVersion", &get_version);
        lua.register_function("UE4SSLuaEventBridge_GetCapabilities", &get_capabilities);
        lua.register_function("UE4SSLuaEventBridge_OpenInputComponent", &open_input_component);
        lua.register_function("UE4SSLuaEventBridge_CloseInputComponent", &close_input_component);
        lua.register_function("UE4SSLuaEventBridge_BindAction", &bind_action);
        lua.register_function("UE4SSLuaEventBridge_Unbind", &unbind);
        lua.register_function("UE4SSLuaEventBridge_UnbindAll", &unbind_all);

        const auto session_script =
            std::string{"__UE4SSLuaEventBridge_SessionId = "} + std::to_string(session_ptr->id);
        lua.execute_string(session_script);

        lua.execute_string(UE4SSLuaEventBridge::embedded_lua_api);

        // The setup chunk returns one dispatcher closure. Keeping one registry
        // reference per Lua session avoids retaining one native registry entry
        // for every bind/unbind cycle. The Lua-side tables own individual
        // callbacks and release them immediately on unbind or bind failure.
        session_ptr->dispatcher_ref = lua.registry().make_ref();

        // Publish the session only after all Lua setup has succeeded. If either
        // setup chunk throws, no stale Lua pointer or state alias survives.
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
    }

    void on_lua_stop(RC::StringViewType, Lua& lua, Lua&, Lua&, Lua*) override
    {
        auto* session = session_for(lua);
        if (!session) return;

        if (RC::Unreal::IsInGameThread())
        {
            try
            {
                lua.execute_string(
                    "if __UE4SSLuaEventBridge_CloseHelperScopes ~= nil then "
                    "__UE4SSLuaEventBridge_CloseHelperScopes() end");
            }
            catch (...)
            {
                // The native fail-safe below still detaches every action
                // binding if reflected helper cleanup cannot complete.
            }
            backend_.unsubscribe_all(*session);
            session->active.store(false, std::memory_order_release);
        }
        else
        {
            session->active.store(false, std::memory_order_release);
            backend_.deactivate_all(*session);
        }
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

    UE4SSLuaEventBridge::LuaSession* session_for_id(uint64_t id)
    {
        const auto found = sessions_.find(id);
        if (found == sessions_.end() || !found->second || !found->second->active.load())
        {
            return nullptr;
        }
        return found->second.get();
    }

private:
    static int get_version(const Lua& lua)
    {
        lua.set_string("0.3.0");
        return 1;
    }

    static int get_capabilities(const Lua& lua)
    {
        lua.set_integer(3);
        lua.set_bool(active_mod && active_mod->backend_.available());
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_string("97b7e501");
        return 8;
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

    static UE4SSLuaEventBridge::LuaSession* consume_session(const Lua& lua)
    {
        if (!active_mod)
        {
            return nullptr;
        }
        const auto id = static_cast<uint64_t>(lua.get_integer(1));
        return active_mod->session_for_id(id);
    }

    static int open_input_component(const Lua& lua)
    {
        if (!active_mod || !active_mod->backend_.available())
        {
            lua.set_nil();
            lua.set_string("Enhanced Input backend is not initialized");
            return 2;
        }
        if (!RC::Unreal::IsInGameThread())
        {
            lua.set_nil();
            lua.set_string("OpenInputComponent must run on the Unreal game thread");
            return 2;
        }

        auto* session = consume_session(lua);
        if (!session)
        {
            lua.set_nil();
            lua.set_string("Lua session is not registered");
            return 2;
        }

        std::string component_path;
        std::string error;
        if (!decode_packed_path(lua, component_path, error))
        {
            lua.set_nil();
            lua.set_string(error);
            return 2;
        }

        auto [handle, backend_error] =
            active_mod->backend_.open_target(*session, std::move(component_path));
        if (handle == 0)
        {
            lua.set_nil();
            lua.set_string(backend_error);
            return 2;
        }

        lua.set_integer(static_cast<int64_t>(handle));
        return 1;
    }

    static int close_input_component(const Lua& lua)
    {
        auto* session = consume_session(lua);
        const auto handle = static_cast<uint64_t>(lua.get_integer(1));
        lua.set_bool(
            RC::Unreal::IsInGameThread() && session && active_mod &&
            active_mod->backend_.close_target(*session, handle));
        return 1;
    }

    static int bind_action(const Lua& lua)
    {
        if (!active_mod || !active_mod->backend_.available())
        {
            lua.set_nil();
            lua.set_string("Enhanced Input backend is not initialized");
            return 2;
        }
        if (!RC::Unreal::IsInGameThread())
        {
            lua.set_nil();
            lua.set_string("BindAction must run on the Unreal game thread");
            return 2;
        }

        auto* session = consume_session(lua);
        if (!session)
        {
            lua.set_nil();
            lua.set_string("Lua session is not registered");
            return 2;
        }

        const auto target = static_cast<uint64_t>(lua.get_integer(1));
        std::string action_path;
        std::string error;
        if (!decode_packed_path(lua, action_path, error))
        {
            lua.set_nil();
            lua.set_string(error);
            return 2;
        }

        const auto [phase, phase_view] = parse_phase(lua.get_integer(1));
        if (phase == TriggerEvent::None)
        {
            lua.set_nil();
            lua.set_string("invalid trigger event");
            return 2;
        }
        const std::string phase_name(phase_view);

        const auto callback_token_value = lua.get_integer(1);
        if (callback_token_value <= 0)
        {
            lua.set_nil();
            lua.set_string("invalid callback token");
            return 2;
        }
        const auto callback_token = static_cast<uint64_t>(callback_token_value);
        auto [handle, backend_error] = active_mod->backend_.subscribe(
            *session, target, callback_token, std::move(action_path), phase_name, phase);
        if (handle == 0)
        {
            lua.set_nil();
            lua.set_string(backend_error);
            return 2;
        }

        lua.set_integer(static_cast<int64_t>(handle));
        return 1;
    }

    static int unbind(const Lua& lua)
    {
        auto* session = consume_session(lua);
        const auto handle = static_cast<uint64_t>(lua.get_integer(1));
        lua.set_bool(
            RC::Unreal::IsInGameThread() && session && active_mod &&
            active_mod->backend_.unsubscribe(*session, handle));
        return 1;
    }

    static int unbind_all(const Lua& lua)
    {
        auto* session = consume_session(lua);
        const bool can_unbind = RC::Unreal::IsInGameThread() && session && active_mod;
        lua.set_integer(
            can_unbind ? static_cast<int64_t>(active_mod->backend_.unsubscribe_all(*session)) : 0);
        lua.set_bool(can_unbind);
        return 2;
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
UE4SS_LUA_EVENT_BRIDGE_API void uninstall_mod(RC::CppUserModBase* mod)
{
    auto* bridge = static_cast<UE4SSLuaEventBridgeMod*>(mod);
    if (bridge && !bridge->prepare_for_unload())
    {
        // UE4SS unloads C++ mods from its event-loop thread. If Unreal still
        // owns a native binding (including an engine-created clone), retain
        // this DLL so its vtable and destructor code cannot become dangling.
        (void)pin_current_module();
    }
    delete bridge;
}
}
