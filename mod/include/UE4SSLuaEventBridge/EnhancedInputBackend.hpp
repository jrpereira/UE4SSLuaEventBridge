#pragma once

#ifdef _WIN32

#include <UE4SSLuaEventBridge/EnhancedInputABI.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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
    LuaSession* session{};
    int32_t callback_ref{};
    std::wstring action_path;
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
    [[nodiscard]] bool available() const { return initialized_.load(); }

    uint64_t subscribe(
        LuaSession& session,
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
    struct LiveBinding;
    class NativeBinding;
    class CreateListener;
    class DeleteListener;

    void process_game_thread_work();
    void note_object_created(const RC::Unreal::UObjectBase* object);
    void note_object_deleted(const RC::Unreal::UObjectBase* object);
    RC::Unreal::UObject* resolve_action(const std::wstring& path) const;
    bool validate_component(RC::Unreal::UObject* component) const;
    NativeBinding* attach(
        RC::Unreal::UObject* component,
        RC::Unreal::UObject* action,
        const std::shared_ptr<EnhancedInputSubscription>& subscription);
    void detach(LiveBinding& binding);

    std::atomic_bool initialized_{false};
    std::atomic_bool work_pending_{false};
    std::atomic<void*> component_class_{nullptr};
    std::atomic_uint64_t next_subscription_{1};
    std::atomic_uint32_t next_binding_handle_{0x80000000u};

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<EnhancedInputSubscription>> subscriptions_;
    std::vector<LiveBinding> bindings_;
    std::vector<EnhancedInputEvent> events_;

    std::unique_ptr<CreateListener> create_listener_;
    std::unique_ptr<DeleteListener> delete_listener_;
};
}

#endif
