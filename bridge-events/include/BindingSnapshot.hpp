#pragma once
#include <cstdint>
#include <span>
#include <algorithm>
namespace UE4SSLuaEventBridge {
inline bool snapshot_array_valid(std::uintptr_t data, int32_t size, int32_t capacity) {
    return size >= 0 && capacity >= size && capacity < 65536 &&
        (capacity == 0 || (data != 0 && data % alignof(void*) == 0));
}
template<class Read, class Action, class Event, class Handle>
inline bool snapshot_read_metadata(bool member, Read read, const void* action_address, const void* event_address,
    const void* handle_address, Action& action, Event& event, Handle& handle) {
    return member && read(action_address, &action, sizeof(action)) &&
        read(event_address, &event, sizeof(event)) && read(handle_address, &handle, sizeof(handle));
}
inline bool snapshot_contains(std::span<const std::uintptr_t> entries, std::uintptr_t binding) {
    return binding != 0 && std::find(entries.begin(), entries.end(), binding) != entries.end();
}
}
