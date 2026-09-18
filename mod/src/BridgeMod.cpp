#include <UE4SSLuaEventBridge/EnhancedInputBackend.hpp>
#include <UE4SSLuaEventBridge/EmbeddedLuaAPI.hpp>
#include <UE4SSLuaEventBridge/SessionAliasIndex.hpp>
#include <UE4SSLuaEventBridge/QueueDispatchSchedule.hpp>
#include <UE4SSLuaEventBridge/UE4SSABI.hpp>
#include <UE4SSLuaEventBridge/Version.hpp>
#include <UE4SSLuaEventBridge/DispatchBudget.hpp>
#include <UE4SSLuaEventBridge/DispatchBacklog.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
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

extern "C" __declspec(dllimport) unsigned long UE4SSLEB_WINAPI GetEnvironmentVariableA(
    const char* name, char* buffer, unsigned long size);

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
constexpr int32_t packed_debug_word_count = 8;
constexpr int32_t packed_debug_max_length = packed_debug_word_count * 8;

class UE4SSLuaEventBridgeMod;
UE4SSLuaEventBridgeMod* active_mod{};

uint32_t configured_queue_check_rate()
{
    std::array<char, 32> value{};
    const auto size = GetEnvironmentVariableA(
        "UE4SSLEB_QUEUE_CHECKS_PER_SECOND", value.data(), static_cast<unsigned long>(value.size()));
    if (size == 0 || size >= value.size()) return UE4SSLuaEventBridge::default_queue_checks_per_second;
    return UE4SSLuaEventBridge::parse_queue_check_rate(std::string_view(value.data(), size));
}

uint32_t configured_limit(const char* name, uint32_t fallback, uint32_t maximum)
{
    std::array<char, 32> value{};
    const auto size = GetEnvironmentVariableA(name, value.data(), static_cast<unsigned long>(value.size()));
    if (size == 0 || size >= value.size()) return fallback;
    uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + size, result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + size && result <= maximum ? result : fallback;
}

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


bool decode_packed_text(
    const Lua& lua,
    int32_t word_count,
    int32_t maximum_length,
    bool allow_empty,
    std::string_view description,
    std::string& value,
    std::string& error)
{
    const auto length = lua.get_integer(1);
    if (length < (allow_empty ? 0 : 1) || length > maximum_length)
    {
        error = "invalid encoded " + std::string(description) + " length";
        return false;
    }

    std::array<uint64_t, packed_path_word_count> words{};
    for (int32_t index = 0; index < word_count; ++index)
    {
        words[static_cast<std::size_t>(index)] = static_cast<uint64_t>(lua.get_integer(1));
    }

    value.assign(static_cast<std::size_t>(length), '\0');
    for (int64_t index = 0; index < length; ++index)
    {
        const auto word = words[static_cast<std::size_t>(index / 8)];
        const auto shift = static_cast<uint32_t>((index % 8) * 8);
        const auto byte = static_cast<uint8_t>((word >> shift) & 0xFFu);
        if (byte == 0)
        {
            error = "invalid encoded " + std::string(description) + " byte";
            return false;
        }
        value[static_cast<std::size_t>(index)] = static_cast<char>(byte);
    }
    return true;
}

bool decode_packed_path(const Lua& lua, std::string& path, std::string& error)
{
    return decode_packed_text(
        lua,
        packed_path_word_count,
        packed_path_max_length,
        false,
        "object path",
        path,
        error);
}

class UE4SSLuaEventBridgeMod final : public RC::CppUserModBase
{
public:
    UE4SSLuaEventBridgeMod()
    {
        ModName = L"UE4SSLuaEventBridge";
        ModVersion = UE4SSLEB_WIDEN(UE4SSLEB_VERSION);
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

    void on_unreal_init() override {
        backend_.set_queue_limit(configured_limit("UE4SSLEB_MAX_QUEUED_EVENTS", 65536, 1000000));
        backend_.initialize();
    }

    void on_update() override
    {
        if (!queue_schedule_.due(UE4SSLuaEventBridge::QueueDispatchSchedule::Clock::now())) return;
        if (pending_events_.empty())
        {
            backend_.recycle_events(pending_events_.release_buffer());
            pending_events_.load(backend_.take_events());
        }
        using Budget = UE4SSLuaEventBridge::DispatchBudget;
        const Budget budget(Budget::Clock::now(), max_events_per_pass_, std::chrono::microseconds(max_dispatch_us_));
        std::size_t processed{};
        while (!pending_events_.empty() && budget.permits(processed, Budget::Clock::now()))
        {
            auto event = pending_events_.pop();
            ++processed;
            const auto& subscription = event.owner;
            auto* session = subscription ? subscription->session : nullptr;
            if (!subscription)
            {
                continue;
            }
            if (!subscription->active.load())
            {
                if (session)
                {
                    backend_.trace(
                        *session,
                        subscription->debug,
                        "callback_skipped",
                        subscription->phase_name,
                        event.sequence,
                        "binding_inactive");
                }
                continue;
            }
            if (!session || !session->active.load() || !session->lua)
            {
                if (session)
                {
                    backend_.trace(
                        *session,
                        subscription->debug,
                        "callback_skipped",
                        subscription->phase_name,
                        event.sequence,
                        "lua_session_inactive");
                }
                continue;
            }

            try
            {
                backend_.trace(
                    *session,
                    subscription->debug,
                    "lua_callback_started",
                    subscription->phase_name,
                    event.sequence);
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
                lua.set_integer(static_cast<int64_t>(event.sequence));
                lua.call_function(11, 0);
                backend_.trace(
                    *session,
                    subscription->debug,
                    "lua_callback_completed",
                    subscription->phase_name,
                    event.sequence);
            }
            catch (const std::exception& callback_error)
            {
                backend_.trace(
                    *session,
                    subscription->debug,
                    "lua_callback_failed",
                    subscription->phase_name,
                    event.sequence,
                    callback_error.what());
                // Lua callbacks run on UE4SS's event-loop thread. Deactivate
                // immediately, then let the next game-thread bridge operation
                // detach the native binding.
                backend_.deactivate(*session, subscription->id);
            }
            catch (...)
            {
                backend_.trace(
                    *session,
                    subscription->debug,
                    "lua_callback_failed",
                    subscription->phase_name,
                    event.sequence,
                    "unknown_lua_exception");
                backend_.deactivate(*session, subscription->id);
            }
        }
        flush_debug_traces();
    }

    void on_lua_start(RC::StringViewType, Lua& lua, Lua& main_lua, Lua& async_lua, Lua* hook_lua) override
    {
        auto session = std::make_unique<UE4SSLuaEventBridge::LuaSession>();
        session->id = next_session_id_.fetch_add(1);
        session->lua = &lua;
        auto* session_ptr = session.get();

        lua.register_function("UE4SSLuaEventBridge_GetVersion", &get_version);
        lua.register_function("UE4SSLuaEventBridge_GetDispatchStats", &get_dispatch_stats);
        lua.register_function("UE4SSLuaEventBridge_GetCapabilities", &get_capabilities);
        lua.register_function("UE4SSLuaEventBridge_IsInGameThread", &is_in_game_thread);
        lua.register_function("UE4SSLuaEventBridge_OpenInputComponent", &open_input_component);
        lua.register_function("UE4SSLuaEventBridge_CloseInputComponent", &close_input_component);
        lua.register_function("UE4SSLuaEventBridge_InspectInputComponent", &inspect_input_component);
        lua.register_function("UE4SSLuaEventBridge_BindAction", &bind_action);
        lua.register_function("UE4SSLuaEventBridge_Unbind", &unbind);
        lua.register_function("UE4SSLuaEventBridge_UnbindAll", &unbind_all);
        lua.register_function("UE4SSLuaEventBridge_TraceScope", &trace_scope);

        const auto session_script =
            std::string{"__UE4SSLuaEventBridge_SessionId = "} + std::to_string(session_ptr->id);
        lua.execute_string(session_script);

        std::string lua_api;
        for (const auto chunk : UE4SSLuaEventBridge::embedded_lua_api_chunks)
        {
            lua_api.append(chunk);
        }
        lua.execute_string(lua_api);

        // The setup chunk returns one dispatcher closure. Keeping one registry
        // reference per Lua session avoids retaining one native registry entry
        // for every bind/unbind cycle. The Lua-side tables own individual
        // callbacks and release them immediately on unbind or bind failure.
        session_ptr->dispatcher_ref = lua.registry().make_ref();

        // Publish the session only after all Lua setup has succeeded. Session
        // storage owns the pointer before aliases become visible, and alias
        // publication rolls back completely if an allocation fails.
        const auto register_state = [&](Lua* state) {
            if (!state) return;
            auto* raw_state = state->get_lua_state();
            if (!raw_state) return;
            session_index_.bind(raw_state, session_ptr);
        };

        {
            std::scoped_lock lock(session_mutex_);
            const bool inserted = sessions_.emplace(session_ptr->id, std::move(session)).second;
            if (!inserted)
            {
                throw std::runtime_error("Lua session ID collision");
            }
        }

        try
        {
            register_state(&lua);
            register_state(&main_lua);
            register_state(&async_lua);
            register_state(hook_lua);
        }
        catch (...)
        {
            session_index_.unbind(session_ptr);
            std::scoped_lock lock(session_mutex_);
            sessions_.erase(session_ptr->id);
            throw;
        }
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
        // Keep the inactive record until bridge destruction. Queued events
        // and in-flight child-state calls can still hold its raw address, and
        // session IDs are never reused.
    }

    UE4SSLuaEventBridge::LuaSession* session_for(const Lua& lua)
    {
        return session_index_.find(lua.get_lua_state());
    }

    UE4SSLuaEventBridge::LuaSession* session_for_id(uint64_t id)
    {
        std::scoped_lock lock(session_mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end() || !found->second || !found->second->active.load())
        {
            return nullptr;
        }
        return found->second.get();
    }

private:
    UE4SSLuaEventBridge::QueueDispatchSchedule queue_schedule_{configured_queue_check_rate()};
    const uint32_t max_events_per_pass_{configured_limit("UE4SSLEB_MAX_EVENTS_PER_PASS", 256, 1000000)};
    const uint32_t max_dispatch_us_{configured_limit("UE4SSLEB_MAX_DISPATCH_US", 2000, 1000000)};
    UE4SSLuaEventBridge::DispatchBacklog<UE4SSLuaEventBridge::EnhancedInputEvent> pending_events_;
    UE4SSLuaEventBridge::DispatchBacklog<UE4SSLuaEventBridge::EnhancedInputTrace> pending_traces_;

    void flush_debug_traces()
    {
        if (pending_traces_.empty())
        {
            backend_.recycle_traces(pending_traces_.release_buffer());
            pending_traces_.load(backend_.take_traces());
        }
        using Budget = UE4SSLuaEventBridge::DispatchBudget;
        const Budget budget(Budget::Clock::now(), 32, std::chrono::microseconds(250));
        std::size_t processed{};
        while (!pending_traces_.empty() && budget.permits(processed, Budget::Clock::now()))
        {
            auto trace = pending_traces_.pop();
            ++processed;
            auto* session = trace.session;
            if (!session || !session->active.load(std::memory_order_acquire) || !session->lua)
            {
                continue;
            }
            try
            {
                const auto& lua = *session->lua;
                lua.registry().get_function_ref(session->dispatcher_ref);
                lua.set_integer(0);
                lua.set_string(trace.line);
                lua.call_function(2, 0);
            }
            catch (...)
            {
                // The native trace writer already emitted the same line to
                // OutputDebugString. Logging must not affect input dispatch.
            }
        }
    }

    static int get_version(const Lua& lua)
    {
        lua.set_string(UE4SSLEB_VERSION);
        return 1;
    }

    static int get_dispatch_stats(const Lua& lua)
    {
        if (!active_mod) { lua.set_nil(); return 1; }
        const auto stats = active_mod->backend_.queue_stats();
        lua.set_integer(static_cast<int64_t>(stats.queued));
        lua.set_integer(static_cast<int64_t>(stats.high_water));
        lua.set_integer(static_cast<int64_t>(stats.rejected));
        lua.set_integer(static_cast<int64_t>(stats.traces_rejected));
        return 4;
    }

    static int get_capabilities(const Lua& lua)
    {
        lua.set_integer(4);
        lua.set_bool(active_mod && active_mod->backend_.available());
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_bool(true);
        lua.set_string("97b7e501");
        lua.set_bool(true); // Additive binding_snapshot capability.
        return 11;
    }

    static int is_in_game_thread(const Lua& lua)
    {
        lua.set_bool(RC::Unreal::IsInGameThread());
        return 1;
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
            lua.set_string("Lua session is not registered or is stopping");
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

    static int inspect_input_component(const Lua& lua)
    {
        const auto fail = [&](std::string_view error) { lua.set_nil(); lua.set_string(error); return 2; };
        if (!active_mod || !active_mod->backend_.available()) return fail("Enhanced Input backend is not initialized");
        if (!RC::Unreal::IsInGameThread()) return fail("InspectInputComponent must run on the Unreal game thread");
        auto* session = consume_session(lua);
        if (!session) return fail("Lua session is not registered or is stopping");
        const auto target = lua.get_integer(1);
        if (target <= 0) return fail("invalid Enhanced Input target handle");
        try {
            auto [snapshot, error] = active_mod->backend_.inspect_target(*session, static_cast<uint64_t>(target));
            if (!error.empty()) return fail(error);
            lua.set_string(snapshot);
            return 1;
        } catch (const std::exception& error) { return fail(error.what()); }
        catch (...) { return fail("binding snapshot failed"); }
    }

    static int close_input_component(const Lua& lua)
    {
        if (!active_mod || !active_mod->backend_.available())
        {
            lua.set_bool(false);
            lua.set_string("Enhanced Input backend is not initialized");
            return 2;
        }
        if (!RC::Unreal::IsInGameThread())
        {
            lua.set_bool(false);
            lua.set_string("CloseInputComponent must run on the Unreal game thread");
            return 2;
        }

        auto* session = consume_session(lua);
        if (!session)
        {
            lua.set_bool(false);
            lua.set_string("Lua session is not registered or is stopping");
            return 2;
        }

        const auto handle_value = lua.get_integer(1);
        if (handle_value <= 0)
        {
            lua.set_bool(false);
            lua.set_string("invalid Enhanced Input target handle");
            return 2;
        }

        if (!active_mod->backend_.close_target(*session, static_cast<uint64_t>(handle_value)))
        {
            lua.set_bool(false);
            const auto message = active_mod->backend_.available()
                ? "Enhanced Input target handle is unknown or belongs to another Lua session: " +
                      std::to_string(handle_value)
                : "Enhanced Input backend shut down while closing target handle: " +
                      std::to_string(handle_value);
            lua.set_string(message);
            return 2;
        }

        lua.set_bool(true);
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
            lua.set_string("Lua session is not registered or is stopping");
            return 2;
        }

        const auto target_value = lua.get_integer(1);
        if (target_value <= 0)
        {
            lua.set_nil();
            lua.set_string("invalid Enhanced Input target handle");
            return 2;
        }
        const auto target = static_cast<uint64_t>(target_value);
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

        UE4SSLuaEventBridge::EnhancedInputDebugInfo debug;
        const auto debug_enabled = lua.get_integer(1);
        if (debug_enabled != 0 && debug_enabled != 1)
        {
            lua.set_nil();
            lua.set_string("invalid debug tracing flag");
            return 2;
        }
        if (debug_enabled == 1)
        {
            const auto scope_id = lua.get_integer(1);
            const auto binding_id = lua.get_integer(1);
            const auto primary = lua.get_integer(1);
            const auto trigger = lua.get_integer(1);
            if (scope_id <= 0 || binding_id <= 0 || (primary != 0 && primary != 1) ||
                (trigger != 1 && trigger != 2))
            {
                lua.set_nil();
                lua.set_string("invalid debug tracing metadata");
                return 2;
            }

            std::string key;
            std::string label;
            if (!decode_packed_text(
                    lua,
                    packed_debug_word_count,
                    packed_debug_max_length,
                    false,
                    "debug key",
                    key,
                    error) ||
                !decode_packed_text(
                    lua,
                    packed_debug_word_count,
                    packed_debug_max_length,
                    false,
                    "debug label",
                    label,
                    error))
            {
                lua.set_nil();
                lua.set_string(error);
                return 2;
            }

            debug.enabled = true;
            debug.primary = primary == 1;
            debug.scope_id = static_cast<uint64_t>(scope_id);
            debug.binding_id = static_cast<uint64_t>(binding_id);
            debug.key = std::move(key);
            debug.label = std::move(label);
            debug.trigger_name = trigger == 1 ? "Tap" : "Hold";
        }

        auto [handle, backend_error] = active_mod->backend_.subscribe(
            *session,
            target,
            callback_token,
            std::move(action_path),
            phase_name,
            phase,
            std::move(debug));
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
        if (!active_mod || !active_mod->backend_.available())
        {
            lua.set_bool(false);
            lua.set_string("Enhanced Input backend is not initialized");
            return 2;
        }
        if (!RC::Unreal::IsInGameThread())
        {
            lua.set_bool(false);
            lua.set_string("Unbind must run on the Unreal game thread");
            return 2;
        }

        auto* session = consume_session(lua);
        if (!session)
        {
            lua.set_bool(false);
            lua.set_string("Lua session is not registered or is stopping");
            return 2;
        }

        const auto handle_value = lua.get_integer(1);
        if (handle_value <= 0)
        {
            lua.set_bool(false);
            lua.set_string("invalid Enhanced Input subscription handle");
            return 2;
        }

        if (!active_mod->backend_.unsubscribe(*session, static_cast<uint64_t>(handle_value)))
        {
            lua.set_bool(false);
            const auto message = active_mod->backend_.available()
                ? "Enhanced Input subscription handle is unknown or belongs to another Lua session: " +
                      std::to_string(handle_value)
                : "Enhanced Input backend shut down while unbinding subscription handle: " +
                      std::to_string(handle_value);
            lua.set_string(message);
            return 2;
        }

        lua.set_bool(true);
        return 1;
    }

    static int unbind_all(const Lua& lua)
    {
        if (!active_mod || !active_mod->backend_.available())
        {
            lua.set_integer(0);
            lua.set_bool(false);
            lua.set_string("Enhanced Input backend is not initialized");
            return 3;
        }
        if (!RC::Unreal::IsInGameThread())
        {
            lua.set_integer(0);
            lua.set_bool(false);
            lua.set_string("UnbindAll must run on the Unreal game thread");
            return 3;
        }

        auto* session = consume_session(lua);
        if (!session)
        {
            lua.set_integer(0);
            lua.set_bool(false);
            lua.set_string("Lua session is not registered or is stopping");
            return 3;
        }

        lua.set_integer(static_cast<int64_t>(active_mod->backend_.unsubscribe_all(*session)));
        lua.set_bool(true);
        return 2;
    }

    static int trace_scope(const Lua& lua)
    {
        auto* session = consume_session(lua);
        const auto scope_id = lua.get_integer(1);
        const auto stage = lua.get_integer(1);
        std::string label;
        std::string error;
        const bool decoded = decode_packed_text(
            lua,
            packed_debug_word_count,
            packed_debug_max_length,
            false,
            "debug label",
            label,
            error);
        if (!session || scope_id <= 0 || (stage != 1 && stage != 2) || !decoded)
        {
            return 0;
        }

        UE4SSLuaEventBridge::EnhancedInputDebugInfo debug;
        debug.enabled = true;
        debug.scope_id = static_cast<uint64_t>(scope_id);
        debug.label = std::move(label);
        active_mod->backend_.trace(
            *session, debug, stage == 1 ? "scope_opened" : "scope_closed");
        return 0;
    }

    UE4SSLuaEventBridge::EnhancedInputBackend backend_;
    std::atomic_uint64_t next_session_id_{1};
    std::mutex session_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<UE4SSLuaEventBridge::LuaSession>> sessions_;
    UE4SSLuaEventBridge::SessionAliasIndex<lua_State, UE4SSLuaEventBridge::LuaSession> session_index_;
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
