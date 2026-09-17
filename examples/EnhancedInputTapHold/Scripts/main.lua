local Helpers = UE4SSLuaEventBridge.Helpers
local Trigger = Helpers.Trigger

-- Replace this with the exact live object path of the
-- UEnhancedInputComponent that should receive the generated action bindings.
-- This is an illustrative object path, not an asset path. Use UE4SS Live View
-- or an object dump after the player and input component exist.
local COMPONENT_PATH =
    "/Game/Maps/L_Example.L_Example:PersistentLevel."
    .. "BP_ExampleCharacter_C_0.EnhancedInputComponent"

-- Replace this with the exact live object path of the matching
-- UEnhancedInputLocalPlayerSubsystem. OpenInput performs no LocalPlayer or
-- subsystem discovery. Transient object suffixes commonly change per run.
local ENHANCED_INPUT_SUBSYSTEM_PATH =
    "/Engine/Transient.GameEngine_0:LocalPlayer_0."
    .. "EnhancedInputLocalPlayerSubsystem_0"

-- Change this only if the generated mappings must outrank or trail other
-- Enhanced Input Mapping Contexts. Zero is a reasonable neutral default.
local MAPPING_PRIORITY = 0

-- Set this to true while diagnosing a missing callback. The bridge will log
-- the generated action's Started, Triggered, Completed, and Canceled phases,
-- native queue delivery, and Lua invocation. It does not trace raw key input.
local DEBUG_BRIDGE = false

-- This short label appears on every bridge trace line. Change it to the name
-- of the mod using this sample. Debug labels are limited to 64 bytes.
local DEBUG_LABEL = "EnhancedInputTapHold"

local input = nil

local function shutdown()
    if input == nil then return true end

    local closed, closeError = input:Close()
    if not closed then
        print("[EnhancedInputTapHold] shutdown failed: " .. closeError .. "\n")
        return false
    end

    input = nil
    print("[EnhancedInputTapHold] shut down\n")
    return true
end

local function initialize()
    local openError
    input, openError = Helpers.OpenInput({
        component_path = COMPONENT_PATH,
        subsystem_path = ENHANCED_INPUT_SUBSYSTEM_PATH,
        mapping_priority = MAPPING_PRIORITY,
        debug = DEBUG_BRIDGE,
        debug_label = DEBUG_LABEL,
    })
    if input == nil then
        print("[EnhancedInputTapHold] initialization failed: " .. openError .. "\n")
        return
    end

    local tapHandle, tapError = input:Bind("F10", Trigger.Tap, function(_event)
        print("[EnhancedInputTapHold] F10 tapped\n")
    end, {
        -- Enhanced Input classifies a release within this interval as a Tap.
        threshold_seconds = 0.2,
    })
    if tapHandle == nil then
        print("[EnhancedInputTapHold] Tap binding failed: " .. tapError .. "\n")
        shutdown()
        return
    end

    local holdHandle, holdError = input:Bind("F10", Trigger.Hold, function(event)
        -- This duration is reported by Enhanced Input. The sample does not
        -- run a Lua timer or infer Tap-versus-Hold from key state.
        print(string.format(
            "[EnhancedInputTapHold] F10 held for %.0f ms\n",
            event.elapsed_processed * 1000))
    end, {
        threshold_seconds = 0.5,
        one_shot = true,
    })
    if holdHandle == nil then
        print("[EnhancedInputTapHold] Hold binding failed: " .. holdError .. "\n")
        shutdown()
        return
    end

    print("[EnhancedInputTapHold] initialized\n")
end

ExecuteInGameThread(initialize)

-- Call this from the mod's reload/shutdown path while its Lua state is still
-- alive. Close is idempotent and removes both private mapping contexts, their
-- generated actions/triggers, native callbacks, and the component target.
EnhancedInputTapHold_Shutdown = function()
    ExecuteInGameThread(shutdown)
end
