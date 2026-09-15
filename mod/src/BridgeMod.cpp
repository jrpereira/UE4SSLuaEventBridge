#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>

#include <UE4SSLuaEventBridge/EventRegistry.hpp>

#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace RC;

class UE4SSLuaEventBridgeMod final : public CppUserModBase
{
public:
    UE4SSLuaEventBridgeMod()
    {
        ModName = STR("UE4SSLuaEventBridge");
        ModVersion = STR(UE4SSLEB_VERSION);
        ModDescription = STR("Native Unreal event bridge for UE4SS Lua mods");
        ModAuthors = STR("UE4SS Lua Event Bridge contributors");
    }

    ~UE4SSLuaEventBridgeMod() override = default;

    auto on_lua_start(
        StringViewType mod_name,
        LuaMadeSimple::Lua& lua,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua*) -> void override
    {
        // Public functions are registered separately in every Lua state. This
        // avoids global callback storage shared across unrelated Lua mods.
        lua.register_function("UE4SSLuaEventBridge_GetVersion", [](const LuaMadeSimple::Lua& state) -> int {
            state.set_string(UE4SSLEB_VERSION);
            return 1;
        });

        lua.register_function("UE4SSLuaEventBridge_GetCapabilities", [](const LuaMadeSimple::Lua& state) -> int {
            // The native Enhanced Input adapter is not advertised until its
            // ABI-specific backend has been compiled and verified.
            state.set_integer(1);
            state.set_bool(false);
            state.set_string(UE4SSLEB_TARGET_UE4SS_COMMIT);
            return 3;
        });

        lua.execute_string(R"lua(
            UE4SSLuaEventBridge = {
                API_VERSION = 1,
                GetVersion = UE4SSLuaEventBridge_GetVersion,
                GetCapabilities = function()
                    local api, enhanced_input, target =
                        UE4SSLuaEventBridge_GetCapabilities()
                    return {
                        api = api,
                        enhanced_input = enhanced_input,
                        target_ue4ss_commit = target,
                    }
                end,
            }
        )lua");

        const auto session_id = next_session_id_.fetch_add(1);
        sessions_.insert_or_assign(&lua, session_id);
        Output::send<LogLevel::Verbose>(
            STR("[UE4SSLuaEventBridge] Attached Lua session {} for {}\n"),
            session_id,
            mod_name);
    }

    auto on_lua_stop(
        StringViewType,
        LuaMadeSimple::Lua& lua,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua*) -> void override
    {
        const auto it = sessions_.find(&lua);
        if (it == sessions_.end())
        {
            return;
        }
        registry_.unsubscribe_all(it->second);
        sessions_.erase(it);
    }

private:
    UE4SSLuaEventBridge::EventRegistry registry_;
    std::atomic<UE4SSLuaEventBridge::SessionId> next_session_id_{1};
    std::unordered_map<LuaMadeSimple::Lua*, UE4SSLuaEventBridge::SessionId> sessions_;
};
} // namespace

#define UE4SS_LUA_EVENT_BRIDGE_API __declspec(dllexport)

extern "C"
{
UE4SS_LUA_EVENT_BRIDGE_API RC::CppUserModBase* start_mod()
{
    return new UE4SSLuaEventBridgeMod();
}

UE4SS_LUA_EVENT_BRIDGE_API void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
}
