#include <UE4SSLuaEventBridge/EnhancedInputBackend.hpp>

#ifdef _WIN32

#include <algorithm>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#if defined(_MSC_VER)
#define UE4SSLEB_WINAPI __stdcall
#else
#define UE4SSLEB_WINAPI
#endif

extern "C" __declspec(dllimport) unsigned long UE4SSLEB_WINAPI GetCurrentThreadId();
extern "C" __declspec(dllimport) void UE4SSLEB_WINAPI OutputDebugStringA(const char* output);

#undef UE4SSLEB_WINAPI

namespace UE4SSLuaEventBridge
{
namespace ABI = EnhancedInputABI;
using RC::Unreal::FMemory;
using RC::Unreal::FWeakObjectPtr;
using RC::Unreal::UClass;
using RC::Unreal::UObject;

namespace
{
template <typename T>
T read_at(const ABI::InputActionInstanceView& view, std::size_t offset)
{
    T value{};
    std::memcpy(&value, view.storage + offset, sizeof(T));
    return value;
}

std::wstring widen_ascii(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}

UClass* object_class(UObject* object)
{
    UClass* result{};
    std::memcpy(
        &result,
        reinterpret_cast<std::byte*>(object) + ABI::uobject_class_offset,
        sizeof(result));
    return result;
}

void append_debug_text(std::string& output, std::string_view value)
{
    output.push_back('"');
    if (value.empty())
    {
        output.push_back('-');
    }
    else
    {
        for (const unsigned char character : value)
        {
            if (character == '\\' || character == '"')
            {
                output.push_back('\\');
            }
            output.push_back(character >= 0x20 ? static_cast<char>(character) : '?');
        }
    }
    output.push_back('"');
}
}

void write_debug_trace(
    const std::shared_ptr<EnhancedInputDispatchState>& dispatch_state,
    LuaSession* session,
    const EnhancedInputDebugInfo& debug,
    std::string_view stage,
    std::string_view phase,
    uint64_t sequence,
    std::string_view reason)
{
    if (!debug.enabled)
    {
        return;
    }

    try
    {
        std::string line = "[UE4SSLuaEventBridge][trace] label=";
        append_debug_text(line, debug.label);
        line += " stage=";
        append_debug_text(line, stage);
        line += " scope=" + std::to_string(debug.scope_id);
        line += " binding=" + std::to_string(debug.binding_id);
        line += " key=";
        append_debug_text(line, debug.key);
        line += " trigger=";
        append_debug_text(line, debug.trigger_name);
        line += " phase=";
        append_debug_text(line, phase);
        line += " event_seq=" + std::to_string(sequence);
        line += " thread_id=" + std::to_string(GetCurrentThreadId());
        line += " game_thread=";
        line += RC::Unreal::IsInGameThread() ? "true" : "false";
        line += " reason=";
        append_debug_text(line, reason);
        line.push_back('\n');

        // Make the record visible immediately to an attached debugger. The
        // queue below sends the same line through Lua's normal print route on
        // the bridge update thread, which places it in the UE4SS log.
        OutputDebugStringA(line.c_str());
        if (!dispatch_state || !session ||
            !dispatch_state->accepting.load(std::memory_order_acquire))
        {
            return;
        }

        std::scoped_lock lock(dispatch_state->mutex);
        if (dispatch_state->accepting.load(std::memory_order_acquire))
        {
            dispatch_state->traces.push_back({session, std::move(line)});
        }
    }
    catch (...)
    {
        // Diagnostics must never alter input delivery. The debugger fallback
        // deliberately avoids allocation and remains useful if a log device
        // itself is the failing component.
        OutputDebugStringA("[UE4SSLuaEventBridge][trace] trace_write_failed\n");
    }
}

UObject* ABI::InputActionInstanceView::source_action() const
{
    return read_at<UObject*>(*this, instance_source_action_offset);
}
ABI::TriggerEvent ABI::InputActionInstanceView::trigger_event() const
{
    return read_at<TriggerEvent>(*this, instance_trigger_event_offset);
}
double ABI::InputActionInstanceView::x() const { return read_at<double>(*this, instance_value_x_offset); }
double ABI::InputActionInstanceView::y() const { return read_at<double>(*this, instance_value_y_offset); }
double ABI::InputActionInstanceView::z() const { return read_at<double>(*this, instance_value_z_offset); }
ABI::ValueType ABI::InputActionInstanceView::value_type() const
{
    return read_at<ValueType>(*this, instance_value_type_offset);
}
float ABI::InputActionInstanceView::elapsed_processed() const
{
    return read_at<float>(*this, instance_elapsed_processed_offset);
}
float ABI::InputActionInstanceView::elapsed_triggered() const
{
    return read_at<float>(*this, instance_elapsed_triggered_offset);
}

ABI::ActionEventBinding::ActionEventBinding(const UObject* source_action, ABI::TriggerEvent trigger, uint32_t binding_handle)
    : action(source_action), event(trigger)
{
    handle = binding_handle;
}

class EnhancedInputBackend::NativeBinding final : public ABI::ActionEventBinding
{
public:
    NativeBinding(
        std::shared_ptr<EnhancedInputDispatchState> dispatch_state,
        const UObject* action,
        ABI::TriggerEvent event,
        uint32_t handle,
        std::shared_ptr<EnhancedInputSubscription> owner)
        : ActionEventBinding(action, event, handle),
          dispatch_state_(std::move(dispatch_state)),
          owner_(std::move(owner))
    {
        dispatch_state_->live_bindings.fetch_add(1, std::memory_order_relaxed);
    }

    ~NativeBinding() override
    {
        dispatch_state_->live_bindings.fetch_sub(1, std::memory_order_release);
    }

    void Execute(const ABI::InputActionInstanceView& instance) const override
    {
        const auto sequence =
            dispatch_state_->next_event_sequence.fetch_add(1, std::memory_order_relaxed);
        write_debug_trace(
            dispatch_state_, owner_->session, owner_->debug,
            "enhanced_input_event", owner_->phase_name, sequence);
        write_debug_trace(
            dispatch_state_, owner_->session, owner_->debug,
            "native_delegate_entered", owner_->phase_name, sequence);

        if (!owner_->active.load(std::memory_order_acquire))
        {
            write_debug_trace(
                dispatch_state_, owner_->session, owner_->debug,
                "event_rejected", owner_->phase_name, sequence, "binding_inactive");
            return;
        }
        if (!dispatch_state_->accepting.load(std::memory_order_acquire))
        {
            write_debug_trace(
                dispatch_state_, owner_->session, owner_->debug,
                "event_rejected", owner_->phase_name, sequence, "dispatch_stopped");
            return;
        }

        try
        {
            EnhancedInputEvent queued{};
            queued.subscription = owner_->id;
            queued.sequence = sequence;
            queued.owner = owner_;
            queued.elapsed_processed = instance.elapsed_processed();
            queued.elapsed_triggered = instance.elapsed_triggered();
            queued.x = instance.x();
            queued.y = instance.y();
            queued.z = instance.z();
            queued.value_type = instance.value_type();

            bool accepted{};
            std::string_view rejection_reason;
            {
                std::scoped_lock lock(dispatch_state_->mutex);
                if (!owner_->active.load(std::memory_order_acquire))
                {
                    rejection_reason = "binding_inactive";
                }
                else if (!dispatch_state_->accepting.load(std::memory_order_acquire))
                {
                    rejection_reason = "dispatch_stopped";
                }
                else
                {
                    dispatch_state_->events.push_back(std::move(queued));
                    accepted = true;
                }
            }
            if (accepted)
            {
                write_debug_trace(
                    dispatch_state_, owner_->session, owner_->debug,
                    "event_queued", owner_->phase_name, sequence);
            }
            else
            {
                write_debug_trace(
                    dispatch_state_, owner_->session, owner_->debug,
                    "event_rejected", owner_->phase_name, sequence, rejection_reason);
            }
        }
        catch (...)
        {
            // Never unwind through Unreal's input dispatcher. An allocation
            // failure disables this subscription until game-thread cleanup.
            owner_->active.store(false, std::memory_order_release);
            write_debug_trace(
                dispatch_state_, owner_->session, owner_->debug,
                "event_rejected", owner_->phase_name, sequence, "queue_exception");
        }
    }

    UObject* GetUObject() const override { return nullptr; }
    bool IsBoundToObject(const void*) const override { return false; }
    void SetShouldFireWithEditorScriptGuard(bool) override {}

    ABI::UniquePtr<ABI::ActionEventBinding> Clone() const override
    {
        // A clone is owned exclusively by Unreal and cannot be found through
        // the bridge's original component/binding pair. Remember that one has
        // existed so an off-thread DLL unload can conservatively retain code.
        dispatch_state_->clone_created.store(true, std::memory_order_release);
        void* memory = FMemory::Malloc(sizeof(NativeBinding), alignof(NativeBinding));
        if (!memory)
        {
            return {};
        }
        return ABI::UniquePtr<ABI::ActionEventBinding>(
            ::new (memory) NativeBinding(dispatch_state_, action.Get(), event, handle, owner_));
    }

    static void operator delete(void* memory) noexcept { FMemory::Free(memory); }
    static void operator delete(void* memory, std::size_t) noexcept { FMemory::Free(memory); }

private:
    std::shared_ptr<EnhancedInputDispatchState> dispatch_state_;
    std::shared_ptr<EnhancedInputSubscription> owner_;
};

EnhancedInputBackend::EnhancedInputBackend()
    : dispatch_state_(std::make_shared<EnhancedInputDispatchState>())
{
}

EnhancedInputBackend::~EnhancedInputBackend() { (void)shutdown(); }

void EnhancedInputBackend::initialize()
{
    dispatch_state_->accepting.store(true, std::memory_order_release);
    initialized_.store(true, std::memory_order_release);
}

bool EnhancedInputBackend::shutdown()
{
    if (!initialized_.exchange(false, std::memory_order_acq_rel))
    {
        return dispatch_state_->live_bindings.load(std::memory_order_acquire) == 0;
    }

    dispatch_state_->accepting.store(false, std::memory_order_release);

    std::scoped_lock lock(mutex_);
    const bool on_game_thread = RC::Unreal::IsInGameThread();
    const bool requires_module_pin =
        !on_game_thread &&
        (!bindings_.empty() || dispatch_state_->clone_created.load(std::memory_order_acquire));
    for (auto& [id, subscription] : subscriptions_)
    {
        subscription->active.store(false, std::memory_order_release);
    }
    if (on_game_thread)
    {
        for (auto& binding : bindings_)
        {
            detach(binding);
        }
    }
    bindings_.clear();
    subscriptions_.clear();
    targets_.clear();
    inactive_sessions_.clear();

    {
        std::scoped_lock event_lock(dispatch_state_->mutex);
        dispatch_state_->events.clear();
        dispatch_state_->traces.clear();
    }
    return !requires_module_pin &&
           dispatch_state_->live_bindings.load(std::memory_order_acquire) == 0;
}

UObject* EnhancedInputBackend::resolve_component(const std::wstring& path) const
{
    const auto find_exact = [&](const wchar_t* class_name) -> UObject* {
        std::vector<UObject*> objects;
        RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
        for (auto* object : objects)
        {
            if (object && object->GetPathName() == path && validate_component(object))
            {
                return object;
            }
        }
        return nullptr;
    };

    if (auto* component = find_exact(L"EnhancedInputComponent"))
    {
        return component;
    }
    return find_exact(L"InputComponent");
}

UObject* EnhancedInputBackend::resolve_action(const std::wstring& path) const
{
    std::vector<UObject*> actions;
    RC::Unreal::UObjectGlobals::FindAllOf(L"InputAction", actions);
    for (auto* action : actions)
    {
        if (action && action->GetPathName() == path)
        {
            return action;
        }
    }
    return nullptr;
}

bool EnhancedInputBackend::validate_component(UObject* component) const
{
    if (!component)
    {
        return false;
    }
    auto* component_type = object_class(component);
    if (!component_type || component_type->GetPropertiesSize() != static_cast<int32_t>(ABI::enhanced_input_component_size))
    {
        return false;
    }
    const auto* bindings = reinterpret_cast<const ABI::ActionEventBindingArray*>(
        reinterpret_cast<const std::byte*>(component) + ABI::action_event_bindings_offset);
    return bindings->size >= 0 && bindings->capacity >= bindings->size && bindings->capacity < 65536 &&
           (bindings->capacity == 0 ||
            (bindings->data != nullptr &&
             reinterpret_cast<std::uintptr_t>(bindings->data) % alignof(void*) == 0));
}

std::pair<uint64_t, std::string> EnhancedInputBackend::open_target(LuaSession& session, std::string component_path)
{
    if (!initialized_.load(std::memory_order_acquire))
    {
        return {0, "Enhanced Input backend is not initialized"};
    }

    {
        std::scoped_lock lock(mutex_);
        collect_inactive_locked();
    }

    auto* component = resolve_component(widen_ascii(component_path));
    if (!component)
    {
        return {0, "EnhancedInputComponent was not found at object path: " + component_path};
    }

    Target target{};
    target.id = next_target_.fetch_add(1);
    target.session = &session;
    target.component.assign(component);
    target.component_path_utf8 = std::move(component_path);

    const auto id = target.id;
    std::scoped_lock lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire))
    {
        return {0, "Enhanced Input backend shut down during component lookup"};
    }
    const bool inserted = targets_.emplace(id, std::move(target)).second;
    if (!inserted)
    {
        return {0, "Enhanced Input target handle collision"};
    }
    return {id, {}};
}

bool EnhancedInputBackend::close_target(LuaSession& session, uint64_t target)
{
    std::scoped_lock lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire))
    {
        return false;
    }
    collect_inactive_locked();
    const auto found = targets_.find(target);
    if (found == targets_.end() || found->second.session != &session)
    {
        return false;
    }

    std::vector<uint64_t> subscriptions;
    for (const auto& [id, subscription] : subscriptions_)
    {
        if (subscription->session == &session && subscription->target == target)
        {
            subscriptions.push_back(id);
        }
    }
    for (const auto id : subscriptions)
    {
        unsubscribe_locked(session, id);
    }
    targets_.erase(found);
    return true;
}

std::pair<uint64_t, std::string> EnhancedInputBackend::subscribe(
    LuaSession& session,
    uint64_t target_id,
    uint64_t callback_token,
    std::string action_path,
    std::string phase_name,
    ABI::TriggerEvent phase,
    EnhancedInputDebugInfo debug)
{
    std::scoped_lock lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire))
    {
        return {0, "Enhanced Input backend is not initialized"};
    }
    collect_inactive_locked();

    const auto target_it = targets_.find(target_id);
    if (target_it == targets_.end() || target_it->second.session != &session)
    {
        return {0, "Enhanced Input target handle is unknown or belongs to another Lua session: " +
                       std::to_string(target_id)};
    }

    auto* component = target_it->second.component.Get();
    if (!validate_component(component))
    {
        return {0, "Enhanced Input target is no longer valid"};
    }

    auto* action = resolve_action(widen_ascii(action_path));
    if (!action)
    {
        return {0, "InputAction was not found at object path: " + action_path};
    }

    auto subscription = std::make_shared<EnhancedInputSubscription>();
    subscription->id = next_subscription_.fetch_add(1);
    subscription->target = target_id;
    subscription->session = &session;
    subscription->callback_token = callback_token;
    subscription->action_path_utf8 = std::move(action_path);
    subscription->phase_name = std::move(phase_name);
    subscription->phase = phase;
    subscription->debug = std::move(debug);

    // Complete all potentially allocating bookkeeping before publishing the
    // polymorphic binding into Unreal's array. After reserve succeeds, the
    // final LiveBinding insertion cannot strand an untracked native object.
    bindings_.reserve(bindings_.size() + 1);
    const auto id = subscription->id;
    const auto [subscription_it, inserted] = subscriptions_.emplace(id, subscription);
    if (!inserted)
    {
        return {0, "Enhanced Input subscription handle collision"};
    }

    auto* binding = attach(component, action, subscription);
    if (!binding)
    {
        subscriptions_.erase(subscription_it);
        return {0, "Enhanced Input native binding creation failed"};
    }

    LiveBinding live{};
    live.component.assign(component);
    live.binding = binding;
    live.subscription = id;
    bindings_.push_back(std::move(live));
    if (subscription->debug.primary)
    {
        write_debug_trace(
            dispatch_state_, subscription->session, subscription->debug,
            "binding_created", subscription->phase_name);
    }
    return {id, {}};
}

bool EnhancedInputBackend::unsubscribe_locked(LuaSession& session, uint64_t id)
{
    const auto it = subscriptions_.find(id);
    if (it == subscriptions_.end() || it->second->session != &session)
    {
        return false;
    }

    auto subscription = it->second;
    subscription->active.store(false, std::memory_order_release);
    for (auto binding_it = bindings_.begin(); binding_it != bindings_.end();)
    {
        if (binding_it->subscription == id)
        {
            detach(*binding_it);
            binding_it = bindings_.erase(binding_it);
        }
        else
        {
            ++binding_it;
        }
    }
    subscriptions_.erase(it);
    if (subscription->debug.primary)
    {
        write_debug_trace(
            dispatch_state_, subscription->session, subscription->debug,
            "binding_removed", subscription->phase_name);
    }
    return true;
}

bool EnhancedInputBackend::unsubscribe(LuaSession& session, uint64_t id)
{
    std::scoped_lock lock(mutex_);
    collect_inactive_locked();
    return unsubscribe_locked(session, id);
}

std::size_t EnhancedInputBackend::unsubscribe_all(LuaSession& session)
{
    std::scoped_lock lock(mutex_);
    collect_inactive_locked();
    std::vector<uint64_t> ids;
    for (const auto& [id, subscription] : subscriptions_)
    {
        if (subscription->session == &session)
        {
            ids.push_back(id);
        }
    }
    for (const auto id : ids)
    {
        unsubscribe_locked(session, id);
    }

    for (auto it = targets_.begin(); it != targets_.end();)
    {
        if (it->second.session == &session)
        {
            it = targets_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return ids.size();
}

void EnhancedInputBackend::deactivate(LuaSession& session, uint64_t id)
{
    {
        std::scoped_lock lock(mutex_);
        const auto found = subscriptions_.find(id);
        if (found == subscriptions_.end() || found->second->session != &session)
        {
            return;
        }
        found->second->active.store(false, std::memory_order_release);
    }

    std::scoped_lock event_lock(dispatch_state_->mutex);
    std::erase_if(dispatch_state_->events, [&](const EnhancedInputEvent& event) {
        return event.subscription == id;
    });
}

void EnhancedInputBackend::deactivate_all(LuaSession& session)
{
    {
        std::scoped_lock lock(mutex_);
        inactive_sessions_.insert(&session);
        for (auto& [id, subscription] : subscriptions_)
        {
            if (subscription->session == &session)
            {
                subscription->active.store(false, std::memory_order_release);
            }
        }
    }

    std::scoped_lock event_lock(dispatch_state_->mutex);
    std::erase_if(dispatch_state_->events, [&](const EnhancedInputEvent& event) {
        return event.owner && event.owner->session == &session;
    });
}

void EnhancedInputBackend::collect_inactive_locked()
{
    std::vector<uint64_t> ids;
    for (const auto& [id, subscription] : subscriptions_)
    {
        if (!subscription->active.load(std::memory_order_acquire) ||
            !subscription->session ||
            inactive_sessions_.contains(subscription->session))
        {
            ids.push_back(id);
        }
    }

    for (const auto id : ids)
    {
        const auto found = subscriptions_.find(id);
        if (found != subscriptions_.end() && found->second->session)
        {
            unsubscribe_locked(*found->second->session, id);
        }
    }

    for (auto it = targets_.begin(); it != targets_.end();)
    {
        if (!it->second.session || inactive_sessions_.contains(it->second.session))
        {
            it = targets_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::vector<EnhancedInputEvent> EnhancedInputBackend::take_events()
{
    std::vector<EnhancedInputEvent> result;
    {
        std::scoped_lock lock(dispatch_state_->mutex);
        result.swap(dispatch_state_->events);
    }
    for (const auto& event : result)
    {
        if (event.owner)
        {
            write_debug_trace(
                dispatch_state_,
                event.owner->session,
                event.owner->debug,
                "event_dequeued",
                event.owner->phase_name,
                event.sequence);
        }
    }
    return result;
}

std::vector<EnhancedInputTrace> EnhancedInputBackend::take_traces()
{
    std::scoped_lock lock(dispatch_state_->mutex);
    std::vector<EnhancedInputTrace> result;
    result.swap(dispatch_state_->traces);
    return result;
}

void EnhancedInputBackend::trace(
    LuaSession& session,
    const EnhancedInputDebugInfo& debug,
    std::string_view stage,
    std::string_view phase,
    uint64_t sequence,
    std::string_view reason)
{
    write_debug_trace(
        dispatch_state_, &session, debug, stage, phase, sequence, reason);
}

EnhancedInputBackend::NativeBinding* EnhancedInputBackend::attach(
    UObject* component,
    UObject* action,
    const std::shared_ptr<EnhancedInputSubscription>& subscription)
{
    if (!validate_component(component) || !action)
    {
        return nullptr;
    }

    auto* array = reinterpret_cast<ABI::ActionEventBindingArray*>(
        reinterpret_cast<std::byte*>(component) + ABI::action_event_bindings_offset);
    if (array->size == array->capacity)
    {
        constexpr int32_t maximum_capacity = 65535;
        if (array->capacity >= maximum_capacity)
        {
            return nullptr;
        }
        const int32_t requested_capacity =
            array->capacity == 0 ? 4 : array->capacity + std::max(4, array->capacity / 2);
        const int32_t new_capacity = std::min(requested_capacity, maximum_capacity);
        void* resized = FMemory::Realloc(array->data, static_cast<std::size_t>(new_capacity) * sizeof(void*), alignof(void*));
        if (!resized)
        {
            return nullptr;
        }
        array->data = static_cast<ABI::ActionEventBinding**>(resized);
        array->capacity = new_capacity;
    }

    void* memory = FMemory::Malloc(sizeof(NativeBinding), alignof(NativeBinding));
    if (!memory)
    {
        return nullptr;
    }
    auto* binding = ::new (memory) NativeBinding(
        dispatch_state_,
        action,
        subscription->phase,
        next_binding_handle_.fetch_add(1),
        subscription);
    array->data[array->size++] = binding;
    return binding;
}

void EnhancedInputBackend::detach(LiveBinding& live)
{
    auto* component = live.component.Get();
    if (!validate_component(component))
    {
        // If the owning component has already been destroyed, Unreal has also
        // destroyed the binding array. Do not dereference the stale binding.
        return;
    }

    auto* array = reinterpret_cast<ABI::ActionEventBindingArray*>(
        reinterpret_cast<std::byte*>(component) + ABI::action_event_bindings_offset);
    for (int32_t index = 0; index < array->size; ++index)
    {
        if (array->data[index] != live.binding)
        {
            continue;
        }
        std::memmove(
            array->data + index,
            array->data + index + 1,
            static_cast<std::size_t>(array->size - index - 1) * sizeof(void*));
        --array->size;
        delete live.binding;
        return;
    }
}
}

#endif
