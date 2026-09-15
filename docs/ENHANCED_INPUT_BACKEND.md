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

## Missing native boundary

`UEnhancedInputComponent::BindAction` is not a reflected `UFunction`, so it is
correctly absent from the CXX dump. The dump cannot provide a callable address
for it. A safe backend therefore needs one of these verified implementations:

1. A native bind function resolved for the Dawnwalker UE 5.5 executable, with
   an address signature and strict validation; or
2. A verified layout for `UEnhancedInputComponent`'s native event-binding
   storage plus the correct UE delegate construction ABI.

The backend must fail closed when validation fails. Guessing an address or
writing an assumed private array layout risks an immediate native crash.

## Binding lifecycle

1. Resolve the requested `UInputAction`.
2. Resolve the active local player's `UEnhancedInputComponent`.
3. Validate both objects immediately before binding.
4. Bind the requested `ETriggerEvent` to a native thunk.
5. Store the binding handle alongside the bridge subscription.
6. Invoke the owning Lua state only on the game thread.
7. Detach on unsubscribe, Lua stop, input-component replacement, or shutdown.
8. Re-resolve and rebind after possession/world reconstruction.

## Tap and Hold

The bridge does not calculate elapsed wall-clock time. A consumer supplies or
selects an Input Action whose triggers are `UInputTriggerTap` or
`UInputTriggerHold`; the bridge forwards the resulting native trigger event.
This avoids duplicating Enhanced Input's state machine in Lua or C++.

