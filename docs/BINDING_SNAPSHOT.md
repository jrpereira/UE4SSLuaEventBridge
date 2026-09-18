# Binding snapshot API

Binding inspection is an additive API 4 capability, `binding_snapshot=true`.
Check `GetCapabilities()` before calling it. Compatibility is pinned to
UE4SS 97b7e501, Unreal 5.5 and Windows x64/MSVC; this API does not broaden it.

## Invocation

```lua
if UE4SSLuaEventBridge.GetCapabilities().binding_snapshot then
    ExecuteInGameThread(function()
        local text, err = input:InspectBindings()
        print(text or ("binding snapshot failed: " .. tostring(err)))
    end)
end
```

input is the caller's existing Helpers.OpenInput scope. Primitive equivalent: UE4SSLuaEventBridge.InspectInputComponent(targetHandle).
Both return one multiline string on successful inspection, or nil,error for invalid target/session, stopped backend/session, off-thread call, closed scope or failure to produce the snapshot. Invalid component/array/binding states are data in a successful snapshot. No debug=true requirement. No automatic logging, cleanup/reaping, object creation, discovery, polling, rebinding or callback execution.

## Schema 1

Header: target; game_thread=1; component_valid; component_index/serial; array_readable; array_invariants; array_size/capacity; entries_readable.
Per subscription: subscription; scope; binding (same IDs as debug traces); active; member; metadata_readable; expected_action_index/serial/valid; expected_trigger; expected_handle.
When metadata is readable: action_index/serial; action_valid; action_matches; trigger; trigger_matches; handle; handle_matches.
Footer: snapshot_end reported=N truncated=0/1. Numeric boolean fields are 0/1. action_valid is unknown on identity mismatch: an untrusted copied action index is never passed to object resolution.
Trigger enums: Triggered=1, Started=2, Ongoing=4, Canceled=8, Completed=16. No event_seq because this is not an input event.
Maximum 256 owned subscription rows (roughly <128 KiB text); original native array capacity must be below 65536, with valid size/capacity/pointer alignment before bounded copying. Readable=false means downstream conclusions are unavailable; member=0 with entries_readable=0 does not establish absence. Likewise metadata fields are omitted if not readable. Row order is unspecified.

## Safety and limits

Uses the pinned object-array resolver for bridge-recorded weak references. Copies object-item fields, native array and metadata with ReadProcessMemory, validating full read length. Never dereferences array entries; only reads metadata at addresses recorded during original attachment after pointer membership is established. Does not invoke virtual methods on diagnostic binding pointers. Expected action identity and native handle are captured at original attachment.
The component class/layout was verified at OpenInput/bind; diagnostic reads only the existing ABI-pinned array offset after resolving the recorded weak reference. This is not a compatibility detector for other engine versions.
Original pointer membership and matching metadata do not prove engine input-stack participation, action evaluation, or delegate invocation. Engine-created clones are not individually tracked by this diagnostic. Arbitrary native memory corruption and allocator address reuse cannot be comprehensively certified by a snapshot. Snapshot observes stored subscriptions with original live-binding records; it does not enumerate foreign bindings.

## Interpreting a snapshot

Capture on demand in the game thread and retain the complete header, rows and
footer together. Check component weak identity and array readability before
interpreting membership or action/trigger/handle matches. A truncated snapshot
does not describe all subscriptions. Debug observers can add subscriptions beyond
the bindings a caller explicitly requested.

An intact snapshot establishes stored binding state only. Investigating missing
input also requires independent evidence of action evaluation and input routing;
absence of a bridge callback alone does not identify where an event was lost.
The native fixture tests cover bounds, identity and read gates, and Lua fixtures
cover API forwarding and failure handling. They do not certify a live Unreal
object layout or replace integration testing for the target build.
