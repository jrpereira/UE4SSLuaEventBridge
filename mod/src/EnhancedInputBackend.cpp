#include <UE4SSLuaEventBridge/EnhancedInputBackend.hpp>

#ifdef _WIN32

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

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
    std::memcpy(&result, reinterpret_cast<std::byte*>(object) + 0x10, sizeof(result));
    return result;
}
}

UObject* ABI::InputActionInstanceView::source_action() const { return read_at<UObject*>(*this, 0x00); }
ABI::TriggerEvent ABI::InputActionInstanceView::trigger_event() const { return read_at<TriggerEvent>(*this, 0x13); }
double ABI::InputActionInstanceView::x() const { return read_at<double>(*this, 0x38); }
double ABI::InputActionInstanceView::y() const { return read_at<double>(*this, 0x40); }
double ABI::InputActionInstanceView::z() const { return read_at<double>(*this, 0x48); }
ABI::ValueType ABI::InputActionInstanceView::value_type() const { return read_at<ValueType>(*this, 0x50); }
float ABI::InputActionInstanceView::elapsed_processed() const { return read_at<float>(*this, 0x58); }
float ABI::InputActionInstanceView::elapsed_triggered() const { return read_at<float>(*this, 0x5C); }

ABI::ActionEventBinding::ActionEventBinding(const UObject* source_action, ABI::TriggerEvent trigger, uint32_t binding_handle)
    : action(source_action), event(trigger)
{
    handle = binding_handle;
}

class EnhancedInputBackend::NativeBinding final : public ABI::ActionEventBinding
{
public:
    NativeBinding(
        EnhancedInputBackend& backend,
        const UObject* action,
        ABI::TriggerEvent event,
        uint32_t handle,
        std::shared_ptr<EnhancedInputSubscription> owner)
        : ActionEventBinding(action, event, handle), backend_(&backend), owner_(std::move(owner))
    {
    }

    void Execute(const ABI::InputActionInstanceView& instance) const override
    {
        if (owner_->active.load(std::memory_order_acquire))
        {
            backend_->enqueue(owner_, instance);
        }
    }

    UObject* GetUObject() const override { return nullptr; }
    bool IsBoundToObject(const void*) const override { return false; }
    void SetShouldFireWithEditorScriptGuard(bool) override {}

    ABI::UniquePtr<ABI::ActionEventBinding> Clone() const override
    {
        void* memory = FMemory::Malloc(sizeof(NativeBinding), alignof(NativeBinding));
        if (!memory)
        {
            return {};
        }
        return ABI::UniquePtr<ABI::ActionEventBinding>(
            ::new (memory) NativeBinding(*backend_, action.Get(), event, handle, owner_));
    }

    static void operator delete(void* memory) noexcept { FMemory::Free(memory); }
    static void operator delete(void* memory, std::size_t) noexcept { FMemory::Free(memory); }

private:
    EnhancedInputBackend* backend_{};
    std::shared_ptr<EnhancedInputSubscription> owner_;
};

EnhancedInputBackend::EnhancedInputBackend() = default;
EnhancedInputBackend::~EnhancedInputBackend() { shutdown(); }

void EnhancedInputBackend::initialize()
{
    initialized_.store(true, std::memory_order_release);
}

void EnhancedInputBackend::shutdown()
{
    if (!initialized_.exchange(false))
    {
        return;
    }

    std::scoped_lock lock(mutex_);
    for (auto& [id, subscription] : subscriptions_)
    {
        subscription->active.store(false, std::memory_order_release);
    }
    for (auto& binding : bindings_)
    {
        detach(binding);
    }
    bindings_.clear();
    subscriptions_.clear();
    targets_.clear();
    events_.clear();
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
           (bindings->capacity == 0 || bindings->data != nullptr);
}

std::pair<uint64_t, std::string> EnhancedInputBackend::open_target(LuaSession& session, std::string component_path)
{
    if (!initialized_.load(std::memory_order_acquire))
    {
        return {0, "Enhanced Input backend is not initialized"};
    }

    auto* component = resolve_component(widen_ascii(component_path));
    if (!component)
    {
        return {0, "EnhancedInputComponent was not found at the supplied object path"};
    }

    Target target{};
    target.id = next_target_.fetch_add(1);
    target.session = &session;
    target.component.assign(component);
    target.component_path_utf8 = std::move(component_path);

    const auto id = target.id;
    std::scoped_lock lock(mutex_);
    targets_.emplace(id, std::move(target));
    return {id, {}};
}

bool EnhancedInputBackend::close_target(LuaSession& session, uint64_t target)
{
    std::scoped_lock lock(mutex_);
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
    int32_t callback_ref,
    std::string action_path,
    std::string phase_name,
    ABI::TriggerEvent phase)
{
    std::scoped_lock lock(mutex_);

    const auto target_it = targets_.find(target_id);
    if (target_it == targets_.end() || target_it->second.session != &session)
    {
        return {0, "invalid Enhanced Input target handle"};
    }

    auto* component = target_it->second.component.Get();
    if (!validate_component(component))
    {
        return {0, "Enhanced Input target is no longer valid"};
    }

    auto* action = resolve_action(widen_ascii(action_path));
    if (!action)
    {
        return {0, "InputAction was not found at the supplied object path"};
    }

    auto subscription = std::make_shared<EnhancedInputSubscription>();
    subscription->id = next_subscription_.fetch_add(1);
    subscription->target = target_id;
    subscription->session = &session;
    subscription->callback_ref = callback_ref;
    subscription->action_path_utf8 = std::move(action_path);
    subscription->phase_name = std::move(phase_name);
    subscription->phase = phase;

    auto* binding = attach(component, action, subscription);
    if (!binding)
    {
        return {0, "Enhanced Input native binding creation failed"};
    }

    const auto id = subscription->id;
    subscriptions_.emplace(id, subscription);
    LiveBinding live{};
    live.component.assign(component);
    live.binding = binding;
    live.subscription = id;
    bindings_.push_back(std::move(live));
    return {id, {}};
}

bool EnhancedInputBackend::unsubscribe_locked(LuaSession& session, uint64_t id)
{
    const auto it = subscriptions_.find(id);
    if (it == subscriptions_.end() || it->second->session != &session)
    {
        return false;
    }

    it->second->active.store(false, std::memory_order_release);
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
    return true;
}

bool EnhancedInputBackend::unsubscribe(LuaSession& session, uint64_t id)
{
    std::scoped_lock lock(mutex_);
    return unsubscribe_locked(session, id);
}

std::size_t EnhancedInputBackend::unsubscribe_all(LuaSession& session)
{
    std::scoped_lock lock(mutex_);
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

std::vector<EnhancedInputEvent> EnhancedInputBackend::take_events()
{
    std::scoped_lock lock(mutex_);
    std::vector<EnhancedInputEvent> result;
    result.swap(events_);
    return result;
}

void EnhancedInputBackend::enqueue(
    const std::shared_ptr<EnhancedInputSubscription>& owner,
    const ABI::InputActionInstanceView& instance)
{
    EnhancedInputEvent event{};
    event.subscription = owner->id;
    event.owner = owner;
    event.elapsed_processed = instance.elapsed_processed();
    event.elapsed_triggered = instance.elapsed_triggered();
    event.x = instance.x();
    event.y = instance.y();
    event.z = instance.z();
    event.value_type = instance.value_type();

    std::scoped_lock lock(mutex_);
    events_.push_back(std::move(event));
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
        const int32_t new_capacity = array->capacity == 0 ? 4 : array->capacity + std::max(4, array->capacity / 2);
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
        *this,
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
