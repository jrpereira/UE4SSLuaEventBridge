# Enhanced Input backend

## Purpose

Translate native Enhanced Input delegate invocations into owned Lua callback
events. Enhanced Input remains responsible for trigger evaluation, including
Tap and Hold timing.

## Confirmed target layouts

The Dawnwalker UE 5.5 CXX dump confirms:

| Type | Relevant data |
|---|---|
| `FInputActionInstance` | source action, trigger event, elapsed processed time, elapsed triggered time |
| `FEnhancedActionKeyMapping` | action, key, trigger array, modifier array |
| `UInputTriggerTap` | `TapReleaseTimeThreshold` |
| `UInputTriggerHold` | `HoldTimeThreshold`, `bIsOneShot` |
| `ETriggerEvent` | `Triggered=1`, `Started=2`, `Ongoing=4`, `Canceled=8`, `Completed=16` |

## Native boundary

`UEnhancedInputComponent::BindAction` is a template/non-reflected C++ API, so
there is no exported function to call. The bridge uses the equivalent manual
binding route documented by Enhanced Input: it appends a polymorphic
`FEnhancedInputActionEventBinding` to the component's native action-event array.

The component size (`0x178`), action-event array offset (`0x140`),
`FInputActionInstance` size/fields, binding layout and virtual interface are
fixed for the Dawnwalker UE 5.5 target. Before touching the array, the backend
validates the live component's reflected size and array invariants. A mismatch
causes the attachment to fail closed.

## Binding lifecycle

1. Resolve the requested `UInputAction`.
2. Resolve the active player controller's acknowledged pawn and its
   `UEnhancedInputComponent`, with the controller component as fallback.
3. Validate both objects immediately before binding.
4. Append the requested `ETriggerEvent` binding on the Unreal game thread.
5. Store the binding handle alongside the bridge subscription.
6. Queue the event and invoke the owning Lua state from UE4SS's update thread;
   never re-enter Lua inside Unreal's input dispatcher.
7. Detach on unsubscribe, Lua stop, input-component replacement, or shutdown.
8. Re-resolve and rebind after possession/world reconstruction.

## Tap and Hold

The bridge does not calculate elapsed wall-clock time. A consumer supplies or
selects an Input Action whose triggers are `UInputTriggerTap` or
`UInputTriggerHold`; the bridge forwards the resulting native trigger event.
This avoids duplicating Enhanced Input's state machine in Lua or C++.
