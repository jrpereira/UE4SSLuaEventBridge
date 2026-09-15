#include <UE4SSLuaEventBridge/EnhancedInputBackend.hpp>

#ifdef _WIN32

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace UE4SSLuaEventBridge
{
namespace ABI = EnhancedInputABI;
using RC::Unreal::FMemory;
using RC::Unreal::UClass;
using RC::Unreal::UObject;
using RC::Unreal::UObjectBase;

namespace
{
EnhancedInputBackend* active_backend{};

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

UObject* object_property(UObject* object, const wchar_t* name)
{
    if (!object)
    {
        return nullptr;
    }
    auto* storage = object->GetValuePtrByPropertyNameInChain(name);
    if (!storage)
    {
        return nullptr;
    }
    UObject* result{};
    std::memcpy(&result, storage, sizeof(result));
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

struct EnhancedInputBackend::LiveBinding
{
    UObject* component{};
    NativeBinding* binding{};
    uint64_t subscription{};
};

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

class EnhancedInputBackend::CreateListener final : public RC::Unreal::FUObjectCreateListener
{
public:
    explicit CreateListener(EnhancedInputBackend& backend) : backend_(&backend) {}
    void NotifyUObjectCreated(const UObjectBase* object, int32_t) override { backend_->note_object_created(object); }
    void OnUObjectArrayShutdown() override {}

private:
    EnhancedInputBackend* backend_;
};

class EnhancedInputBackend::DeleteListener final : public RC::Unreal::FUObjectDeleteListener
{
public:
    explicit DeleteListener(EnhancedInputBackend& backend) : backend_(&backend) {}
    void NotifyUObjectDeleted(const UObjectBase* object, int32_t) override { backend_->note_object_deleted(object); }
    void OnUObjectArrayShutdown() override {}

private:
    EnhancedInputBackend* backend_;
};

EnhancedInputBackend::EnhancedInputBackend() = default;

EnhancedInputBackend::~EnhancedInputBackend() { shutdown(); }

void EnhancedInputBackend::shutdown()
{
    if (!initialized_.exchange(false))
    {
        return;
    }

    active_backend = nullptr;
    if (create_listener_)
    {
        RC::Unreal::FUObjectArray::RemoveUObjectCreateListener(create_listener_.get());
    }
    if (delete_listener_)
    {
        RC::Unreal::FUObjectArray::RemoveUObjectDeleteListener(delete_listener_.get());
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
    events_.clear();
}

void EnhancedInputBackend::initialize()
{
    if (initialized_.exchange(true))
    {
        return;
    }

    active_backend = this;
    create_listener_ = std::make_unique<CreateListener>(*this);
    delete_listener_ = std::make_unique<DeleteListener>(*this);
    RC::Unreal::FUObjectArray::AddUObjectCreateListener(create_listener_.get());
    RC::Unreal::FUObjectArray::AddUObjectDeleteListener(delete_listener_.get());
    RC::Unreal::Hook::RegisterProcessEventPostCallback([](UObject*, RC::Unreal::UFunction*, void*) {
        auto* backend = active_backend;
        if (backend && backend->work_pending_.load(std::memory_order_acquire) && RC::Unreal::IsInGameThread())
        {
            backend->process_game_thread_work();
        }
    });
}

uint64_t EnhancedInputBackend::subscribe(
    LuaSession& session,
    int32_t callback_ref,
    std::string action_path,
    std::string phase_name,
    ABI::TriggerEvent phase)
{
    auto subscription = std::make_shared<EnhancedInputSubscription>();
    subscription->id = next_subscription_.fetch_add(1);
    subscription->session = &session;
    subscription->callback_ref = callback_ref;
    subscription->action_path = widen_ascii(action_path);
    subscription->action_path_utf8 = std::move(action_path);
    subscription->phase_name = std::move(phase_name);
    subscription->phase = phase;

    {
        std::scoped_lock lock(mutex_);
        subscriptions_.emplace(subscription->id, subscription);
    }
    work_pending_.store(true, std::memory_order_release);
    return subscription->id;
}

bool EnhancedInputBackend::unsubscribe(LuaSession& session, uint64_t id)
{
    std::scoped_lock lock(mutex_);
    const auto it = subscriptions_.find(id);
    if (it == subscriptions_.end() || it->second->session != &session)
    {
        return false;
    }
    it->second->active.store(false, std::memory_order_release);
    work_pending_.store(true, std::memory_order_release);
    return true;
}

std::size_t EnhancedInputBackend::unsubscribe_all(LuaSession& session)
{
    std::size_t count{};
    std::scoped_lock lock(mutex_);
    for (auto& [id, subscription] : subscriptions_)
    {
        if (subscription->session == &session && subscription->active.exchange(false))
        {
            ++count;
        }
    }
    if (count)
    {
        work_pending_.store(true, std::memory_order_release);
    }
    return count;
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

void EnhancedInputBackend::note_object_created(const UObjectBase* object)
{
    // The create listener can run before the first Enhanced Input component or
    // requested action exists. Schedule one rescan for any newly created
    // UObject; the ProcessEvent post-hook performs the actual work after
    // construction has completed and only when work_pending_ is set.
    if (object && initialized_.load(std::memory_order_acquire))
    {
        work_pending_.store(true, std::memory_order_release);
    }
}

void EnhancedInputBackend::note_object_deleted(const UObjectBase* object)
{
    std::scoped_lock lock(mutex_);
    const auto* deleted = reinterpret_cast<const UObject*>(object);
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(), [deleted](const LiveBinding& live) {
            return live.component == deleted;
        }),
        bindings_.end());
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

std::vector<UObject*> EnhancedInputBackend::resolve_local_player_components() const
{
    std::vector<UObject*> controllers;
    RC::Unreal::UObjectGlobals::FindAllOf(L"PlayerController", controllers);

    for (auto* controller : controllers)
    {
        auto* pawn = object_property(controller, L"AcknowledgedPawn");
        if (!pawn)
        {
            pawn = object_property(controller, L"Pawn");
        }

        auto* component = object_property(pawn, L"InputComponent");
        if (validate_component(component))
        {
            return {component};
        }

        component = object_property(controller, L"InputComponent");
        if (validate_component(component))
        {
            return {component};
        }
    }

    return {};
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
    if (!validate_component(live.component))
    {
        return;
    }
    auto* array = reinterpret_cast<ABI::ActionEventBindingArray*>(
        reinterpret_cast<std::byte*>(live.component) + ABI::action_event_bindings_offset);
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

void EnhancedInputBackend::process_game_thread_work()
{
    work_pending_.store(false, std::memory_order_release);

    const auto components = resolve_local_player_components();

    std::scoped_lock lock(mutex_);

    for (auto it = bindings_.begin(); it != bindings_.end();)
    {
        const auto owner = subscriptions_.find(it->subscription);
        if (owner == subscriptions_.end() || !owner->second->active.load(std::memory_order_acquire))
        {
            detach(*it);
            it = bindings_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = subscriptions_.begin(); it != subscriptions_.end();)
    {
        const auto& subscription = it->second;
        if (!subscription->active.load(std::memory_order_acquire))
        {
            it = subscriptions_.erase(it);
            continue;
        }

        UObject* action = resolve_action(subscription->action_path);
        if (!action)
        {
            ++it;
            continue;
        }

        for (auto* component : components)
        {
            const bool already_bound = std::any_of(bindings_.begin(), bindings_.end(), [&](const LiveBinding& live) {
                return live.component == component && live.subscription == subscription->id;
            });
            if (!already_bound)
            {
                if (auto* binding = attach(component, action, subscription))
                {
                    bindings_.push_back({component, binding, subscription->id});
                }
            }
        }
        ++it;
    }
}
}

#endif
