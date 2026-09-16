# Enhanced Input Tap/Hold sample

Copy this directory to `Mods/EnhancedInputTapHold`, replace the two live
object paths at the top of `Scripts/main.lua`, and enable both this Lua mod and
`UE4SSLuaEventBridge`.

The sample creates two independent generated Enhanced Input bindings for
`F10`. Unreal's Tap and Hold triggers classify the input. Lua only logs the
result and uses Enhanced Input's `elapsed_processed` value for the reported
hold duration.

Call `EnhancedInputTapHold_Shutdown()` from the mod's normal reload or shutdown
path to perform explicit game-thread cleanup.
