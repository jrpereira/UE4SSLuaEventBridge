#pragma once

#ifdef _WIN32

#include <UE4SSLuaEventBridge/EnhancedInputABI.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace UE4SSLuaEventBridge
{
struct LuaSession;

struct EnhancedInputEvent
{
    uint64_t subscription{};
    uint64_t sequence{};
    std::shared_ptr<struct EnhancedInputSubscription> owner;
    float elapsed_processed{};
    float elapsed_triggered{};
    double x{};
    double y{};
    double z{};
    EnhancedInputABI::ValueType value_type{EnhancedInputABI::ValueType::Boolean};
};

struct EnhancedInputDebugInfo
{
    bool enabled{};
    bool primary{};
    uint64_t scope_id{};
    uint64_t binding_id{};
    std::string label;
    std::string key;
    std::string trigger_name;
};

struct EnhancedInputTrace
{
    LuaSession* session{};
    std::string line;
};

struct EnhancedInputDispatchState
{
    std::atomic_bool accepting{false};
    std::atomic_bool clone_created{false};
    std::atomic_uint64_t live_bindings{0};
    std::atomic_uint64_t next_event_sequence{1};
    std::mutex mutex;
    std::vector<EnhancedInputEvent> events;
    std::vector<EnhancedInputTrace> traces;
};

void write_debug_trace(
    const std::shared_ptr<EnhancedInputDispatchState>& dispatch_state,
    LuaSession* session,
    const EnhancedInputDebugInfo& debug,
    std::string_view stage,
    std::string_view phase,
    uint64_t sequence = 0,
    std::string_view reason = {});

struct EnhancedInputSubscription
{
    uint64_t id{};
    uint64_t target{};
    LuaSession* session{};
    uint64_t callback_token{};
    std::string action_path_utf8;
    std::string phase_name;
    EnhancedInputABI::TriggerEvent phase{EnhancedInputABI::TriggerEvent::None};
    EnhancedInputDebugInfo debug;
    std::atomic_bool active{true};
};

class EnhancedInputBackend
{
public:
    EnhancedInputBackend();
    ~EnhancedInputBackend();

    EnhancedInputBackend(const EnhancedInputBackend&) = delete;
    EnhancedInputBackend& operator=(const EnhancedInputBackend&) = delete;

    void initialize();
    // Returns false when Unreal still owns one or more native binding objects.
    // In that case the containing DLL must remain loaded until process exit.
    [[nodiscard]] bool shutdown();
    [[nodiscard]] bool available() const { return initialized_.load(); }

    // Explicit-target API. The caller owns lifecycle policy: it decides when
    // an EnhancedInputComponent is meaningful and supplies its exact object path.
    std::pair<uint64_t, std::string> open_target(LuaSession& session, std::string component_path);
    bool close_target(LuaSession& session, uint64_t target);

    std::pair<uint64_t, std::string> subscribe(
        LuaSession& session,
        uint64_t target,
        uint64_t callback_token,
        std::string action_path,
        std::string phase_name,
        EnhancedInputABI::TriggerEvent phase,
        EnhancedInputDebugInfo debug = {});
    bool unsubscribe(LuaSession& session, uint64_t id);
    std::size_t unsubscribe_all(LuaSession& session);
    void deactivate(LuaSession& session, uint64_t id);
    void deactivate_all(LuaSession& session);

    std::vector<EnhancedInputEvent> take_events();
    std::vector<EnhancedInputTrace> take_traces();
    void trace(
        LuaSession& session,
        const EnhancedInputDebugInfo& debug,
        std::string_view stage,
        std::string_view phase = {},
        uint64_t sequence = 0,
        std::string_view reason = {});

private:
    class NativeBinding;
    struct Target
    {
        uint64_t id{};
        LuaSession* session{};
        RC::Unreal::FWeakObjectPtr component;
        std::string component_path_utf8;
    };
    struct LiveBinding
    {
        RC::Unreal::FWeakObjectPtr component;
        NativeBinding* binding{};
        uint64_t subscription{};
    };

    RC::Unreal::UObject* resolve_component(const std::wstring& path) const;
    RC::Unreal::UObject* resolve_action(const std::wstring& path) const;
    bool validate_component(RC::Unreal::UObject* component) const;
    NativeBinding* attach(
        RC::Unreal::UObject* component,
        RC::Unreal::UObject* action,
        const std::shared_ptr<EnhancedInputSubscription>& subscription);
    void detach(LiveBinding& binding);
    void collect_inactive_locked();
    bool unsubscribe_locked(LuaSession& session, uint64_t id);

    std::atomic_bool initialized_{false};
    std::atomic_uint64_t next_target_{1};
    std::atomic_uint64_t next_subscription_{1};
    std::atomic_uint32_t next_binding_handle_{0x80000000u};

    std::shared_ptr<EnhancedInputDispatchState> dispatch_state_;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, Target> targets_;
    std::unordered_map<uint64_t, std::shared_ptr<EnhancedInputSubscription>> subscriptions_;
    std::unordered_set<LuaSession*> inactive_sessions_;
    std::vector<LiveBinding> bindings_;
};
}

#endif
