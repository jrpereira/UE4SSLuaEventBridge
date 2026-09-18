#pragma once

#ifdef _WIN32

#include <UE4SSLuaEventBridge/UE4SSABI.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace UE4SSLuaEventBridge::EnhancedInputABI
{
using RC::Unreal::FWeakObjectPtr;
using RC::Unreal::UObject;

enum class TriggerEvent : uint8_t
{
    None = 0,
    Triggered = 1,
    Started = 2,
    Ongoing = 4,
    Canceled = 8,
    Completed = 16,
};

enum class ValueType : uint8_t
{
    Boolean = 0,
    Axis1D = 1,
    Axis2D = 2,
    Axis3D = 3,
};

struct InputActionInstanceView
{
    std::byte storage[0x60];

    [[nodiscard]] UObject* source_action() const;
    [[nodiscard]] TriggerEvent trigger_event() const;
    [[nodiscard]] double x() const;
    [[nodiscard]] double y() const;
    [[nodiscard]] double z() const;
    [[nodiscard]] ValueType value_type() const;
    [[nodiscard]] float elapsed_processed() const;
    [[nodiscard]] float elapsed_triggered() const;
};

template <typename T>
class UniquePtr
{
public:
    UniquePtr() = default;
    explicit UniquePtr(T* pointer) : pointer_(pointer) {}
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;
    UniquePtr(UniquePtr&& other) noexcept : pointer_(other.release()) {}
    UniquePtr& operator=(UniquePtr&& other) noexcept
    {
        reset(other.release());
        return *this;
    }
    ~UniquePtr() { reset(); }

    T* release() noexcept
    {
        T* result = pointer_;
        pointer_ = nullptr;
        return result;
    }
    void reset(T* pointer = nullptr) noexcept
    {
        if (pointer_)
        {
            delete pointer_;
        }
        pointer_ = pointer;
    }

private:
    T* pointer_{};
};

class InputBindingHandle
{
public:
    virtual ~InputBindingHandle() = default;

protected:
    uint32_t handle{};
};

class ActionEventBinding : public InputBindingHandle
{
public:
    ActionEventBinding(const UObject* action, TriggerEvent event, uint32_t binding_handle);
    ~ActionEventBinding() override = default;

    // IMPORTANT: this declaration order is ABI-significant. It matches
    // UE 5.5 FEnhancedInputActionEventBinding exactly:
    // Execute, Clone, SetShouldFireWithEditorScriptGuard,
    // IsBoundToObject, GetUObject.
    virtual void Execute(const InputActionInstanceView& instance) const = 0;
    virtual UniquePtr<ActionEventBinding> Clone() const = 0;
    virtual void SetShouldFireWithEditorScriptGuard(bool enabled) = 0;
    virtual bool IsBoundToObject(const void* object) const = 0;
    virtual UObject* GetUObject() const = 0;

    FWeakObjectPtr action;
    TriggerEvent event{TriggerEvent::None};
    bool consumes{};
};

struct ActionEventBindingArray
{
    ActionEventBinding** data{};
    int32_t size{};
    int32_t capacity{};
};

// Explicit experimental target profile; never infer offsets from arbitrary sizes.
#ifdef UE4SSLEB_COMPONENT_LAYOUT_128
inline constexpr std::size_t input_component_size = 0x128;
inline constexpr std::size_t enhanced_input_component_size = 0x160;
inline constexpr std::size_t action_event_bindings_offset = 0x128;
#else
inline constexpr std::size_t input_component_size = 0x140;
inline constexpr std::size_t enhanced_input_component_size = 0x178;
inline constexpr std::size_t action_event_bindings_offset = 0x140;
#endif
inline constexpr std::size_t uobject_class_offset = 0x10;
inline constexpr std::size_t instance_source_action_offset = 0x00;
inline constexpr std::size_t instance_trigger_event_offset = 0x13;
inline constexpr std::size_t instance_value_x_offset = 0x38;
inline constexpr std::size_t instance_value_y_offset = 0x40;
inline constexpr std::size_t instance_value_z_offset = 0x48;
inline constexpr std::size_t instance_value_type_offset = 0x50;
inline constexpr std::size_t instance_elapsed_processed_offset = 0x58;
inline constexpr std::size_t instance_elapsed_triggered_offset = 0x5C;

static_assert(sizeof(FWeakObjectPtr) == 0x8);
static_assert(sizeof(InputActionInstanceView) == 0x60);
static_assert(sizeof(UniquePtr<ActionEventBinding>) == sizeof(void*));
static_assert(sizeof(ActionEventBindingArray) == 0x10);
static_assert(sizeof(InputBindingHandle) == 0x10);
static_assert(instance_elapsed_triggered_offset + sizeof(float) == sizeof(InputActionInstanceView));
#ifdef _MSC_VER
static_assert(sizeof(ActionEventBinding) == 0x20);
#endif
}

#endif
