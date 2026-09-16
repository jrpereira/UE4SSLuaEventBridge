#pragma once

#ifdef _WIN32

#include <UE4SSLuaEventBridge/EnhancedInputABI.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace UE4SSLuaEventBridge
{
struct LuaSession;

struct EnhancedInputEvent
{
    uint64_t subscription{};
    std::shared_ptr<struct EnhancedInputSubscription> owner;
    float elapsed_processed{};
    float elapsed_triggered{};
    double x{};
    double y{};
    double z{};
    EnhancedInputABI::ValueType value_type{EnhancedInputABI::ValueType::Boolean};
};

struct EnhancedInputSubscription
{
    uint64_t id{};
    uint64_t target{};
    LuaSession* session{};
    int32_t callback_ref{};
    std::string action_path_utf8;
    std::string phase_name;
    EnhancedInputABI::TriggerEvent phase{EnhancedInputABI::TriggerEvent::None};
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
    void shutdown();
    [[nodiscard]] bool available() const { return initialized_.load(); }

    // Explicit-target API. The caller owns lifecycle policy: it decides when
    // an EnhancedInputComponent is meaningful and supplies its exact object path.
    std::pair<uint64_t, std::string> open_target(LuaSession& session, std::string component_path);
    bool close_target(LuaSession& session, uint64_t target);

    std::pair<uint64_t, std::string> subscribe(
        LuaSession& session,
        uint64_t target,
        int32_t callback_ref,
        std::string action_path,
        std::string phase_name,
        EnhancedInputABI::TriggerEvent phase);
    bool unsubscribe(LuaSession& session, uint64_t id);
    std::size_t unsubscribe_all(LuaSession& session);

    std::vector<EnhancedInputEvent> take_events();
    void enqueue(
        const std::shared_ptr<EnhancedInputSubscription>& owner,
        const EnhancedInputABI::InputActionInstanceView& instance);

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
    bool unsubscribe_locked(LuaSession& session, uint64_t id);

    std::atomic_bool initialized_{false};
    std::atomic_uint64_t next_target_{1};
    std::atomic_uint64_t next_subscription_{1};
    std::atomic_uint32_t next_binding_handle_{0x80000000u};

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, Target> targets_;
    std::unordered_map<uint64_t, std::shared_ptr<EnhancedInputSubscription>> subscriptions_;
    std::vector<LiveBinding> bindings_;
    std::vector<EnhancedInputEvent> events_;
};
}

#endif
