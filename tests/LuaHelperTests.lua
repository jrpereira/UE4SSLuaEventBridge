local function expect(condition, message)
    if not condition then error(message or "expectation failed", 2) end
end

local counters = {
    targets_opened = 0,
    targets_closed = 0,
    contexts_added = 0,
    contexts_removed = 0,
    native_unbound = 0,
}
local nextObject = 0
local nextHandle = 0
local nativeTokens = {}
local nativeBindings = {}
local scopeTraces = {}
local failNextBind = false
local gameThread = true

local function object(kind, path)
    local value = { kind = kind, path = path, valid = true }
    function value:IsValid() return self.valid end
    function value:IsA(classPath)
        return classPath == "/Script/EnhancedInput." .. self.kind
            or (self.kind == "Class" and classPath == "/Script/CoreUObject.Class")
    end
    function value:GetFullName() return self.kind .. " " .. self.path end
    return value
end

local subsystemPath = "/Engine/Transient.TestSubsystem"
local subsystem = object("EnhancedInputLocalPlayerSubsystem", subsystemPath)
subsystem.active = {}
function subsystem:AddMappingContext(context, priority, _options)
    context.priority = priority
    self.active[context] = true
    counters.contexts_added = counters.contexts_added + 1
end
function subsystem:RemoveMappingContext(context, _options)
    if self.active[context] then
        self.active[context] = nil
        counters.contexts_removed = counters.contexts_removed + 1
    end
end

local classes = {}
for _, name in ipairs({
    "InputMappingContext",
    "InputAction",
    "InputTriggerTap",
    "InputTriggerHold",
}) do
    classes["/Script/EnhancedInput." .. name] = object("Class", "/Script/EnhancedInput." .. name)
    classes["/Script/EnhancedInput." .. name].class_name = name
end

function StaticFindObject(path)
    if path == subsystemPath then return subsystem end
    return classes[path] or object("Missing", path)
end

function StaticConstructObject(class, outer, _name, flags)
    expect(flags == 0x40, "generated objects must be transient")
    nextObject = nextObject + 1
    local generated = object(class.class_name, "/Engine/Transient.Generated_" .. nextObject)
    generated.outer = outer
    if class.class_name == "InputMappingContext" then
        function generated:MapKey(action, key)
            self.action = action
            self.key = key
        end
    end
    return generated
end

function FName(value) return { value = value } end

__UE4SSLuaEventBridge_SessionId = 17

function UE4SSLuaEventBridge_GetVersion() return "0.3.3" end
function UE4SSLuaEventBridge_GetCapabilities()
    return 4, true, true, true, true, true, true, true, true, "97b7e501"
end
function UE4SSLuaEventBridge_IsInGameThread() return gameThread end
function UE4SSLuaEventBridge_OpenInputComponent(_session, ...)
    counters.targets_opened = counters.targets_opened + 1
    return counters.targets_opened
end
function UE4SSLuaEventBridge_CloseInputComponent(_session, _target)
    counters.targets_closed = counters.targets_closed + 1
    return true
end
function UE4SSLuaEventBridge_BindAction(...)
    local args = { ... }
    if failNextBind then
        failNextBind = false
        return nil, "injected bind failure"
    end
    nextHandle = nextHandle + 1
    nativeTokens[nextHandle] = args[69]
    nativeBindings[nextHandle] = {
        phase = args[68],
        debug = args[70] == 1,
        scope_id = args[71],
        binding_id = args[72],
        primary = args[73] == 1,
        trigger_kind = args[74],
    }
    return nextHandle
end
function UE4SSLuaEventBridge_Unbind(_session, handle)
    if nativeTokens[handle] == nil then return false, "unknown subscription handle" end
    nativeTokens[handle] = nil
    nativeBindings[handle] = nil
    counters.native_unbound = counters.native_unbound + 1
    return true
end
function UE4SSLuaEventBridge_UnbindAll(_session)
    local count = 0
    for handle in pairs(nativeTokens) do
        nativeTokens[handle] = nil
        nativeBindings[handle] = nil
        count = count + 1
    end
    return count, true
end
function UE4SSLuaEventBridge_TraceScope(_session, scopeId, stage, ...)
    scopeTraces[#scopeTraces + 1] = { scope_id = scopeId, stage = stage }
end

local dispatch = assert(loadfile("mod/lua/bridge_api.lua"))()
local bridge = UE4SSLuaEventBridge
local Helpers = bridge.Helpers
local Trigger = Helpers.Trigger

expect(bridge.API_VERSION == 4)
expect(bridge.GetCapabilities().helpers == true)
expect(bridge.GetCapabilities().detailed_errors == true)
expect(bridge.GetCapabilities().debug_tracing == true)

local invalidTarget, invalidTargetError = bridge.CloseInputComponent("bad")
expect(not invalidTarget and string.find(invalidTargetError, "positive integer", 1, true))
local invalidBind, invalidBindError = bridge.BindAction(1, "", "Triggered", function() end)
expect(invalidBind == nil and string.find(invalidBindError, "1..512", 1, true))
local missingRemoved, missingRemoveError = bridge.Unbind(999)
expect(not missingRemoved and missingRemoveError == "unknown subscription handle")

local readOnly = pcall(function() Trigger.Tap = "changed" end)
expect(not readOnly, "Trigger constants must be read-only")

gameThread = false
local offThreadScope, openThreadError = Helpers.OpenInput({
    component_path = "/Game/Test.Component",
    subsystem_path = subsystemPath,
})
expect(offThreadScope == nil and string.find(openThreadError, "game thread", 1, true))
expect(counters.targets_opened == 0,
    "off-thread OpenInput must not resolve or retain an Input Component")
gameThread = true

local scope, openError = Helpers.OpenInput({
    component_path = "/Game/Test.Component",
    subsystem_path = subsystemPath,
})
expect(scope ~= nil, openError)

gameThread = false
local objectsBeforeOffThreadBind = nextObject
local offThreadHandle, offThreadError = scope:Bind("F9", Trigger.Tap, function() end)
expect(offThreadHandle == nil and string.find(offThreadError, "game thread", 1, true))
expect(nextObject == objectsBeforeOffThreadBind,
    "off-thread Bind must not construct Unreal objects")
gameThread = true

local tapEvent
local tapHandle, tapError = scope:Bind("F10", Trigger.Tap, function(event)
    tapEvent = event
end, { threshold_seconds = 0.2 })
expect(tapHandle ~= nil, tapError)
expect(nativeBindings[tapHandle].debug == false,
    "debug-disabled scopes must not attach diagnostic metadata")

local holdEvent
local holdHandle, holdError = scope:Bind("F10", Trigger.Hold, function(event)
    holdEvent = event
end, { threshold_seconds = 0.5, one_shot = true })
expect(holdHandle ~= nil, holdError)
expect(counters.contexts_added == 2)

gameThread = false
local removedOffThread, unbindThreadError = scope:Unbind(tapHandle)
expect(not removedOffThread and string.find(unbindThreadError, "game thread", 1, true))
expect(counters.native_unbound == 0 and counters.contexts_removed == 0,
    "off-thread Unbind must not mutate native bindings or mapping contexts")
local closedOffThread, closeThreadError = scope:Close()
expect(not closedOffThread and string.find(closeThreadError, "game thread", 1, true))
expect(counters.native_unbound == 0 and counters.contexts_removed == 0,
    "off-thread Close must not mutate native bindings or mapping contexts")
gameThread = true

dispatch(nativeTokens[tapHandle], tapHandle, "/Engine/Transient.Tap", "Triggered",
    0.1, 0.0, 1.0, 0.0, 0.0, 0, 41)
expect(tapEvent.key == "F10")
expect(tapEvent.trigger == Trigger.Tap)
expect(tapEvent.sequence == 41)

dispatch(nativeTokens[holdHandle], holdHandle, "/Engine/Transient.Hold", "Triggered",
    0.75, 0.25, 1.0, 0.0, 0.0, 0, 42)
expect(holdEvent.trigger == Trigger.Hold)
expect(holdEvent.elapsed_processed == 0.75)

local removed, removeError = scope:Unbind(tapHandle)
expect(removed, removeError)
expect(counters.contexts_removed == 1)

failNextBind = true
local failedHandle, failedError = scope:Bind("F11", Trigger.Tap, function() end)
expect(failedHandle == nil and failedError == "injected bind failure")
expect(counters.contexts_added == 2)
expect(counters.contexts_removed == 1,
    "failed native Bind must not publish its mapping context")

local badHandle = assert(scope:Bind("F12", Trigger.Tap, function()
    error("injected callback failure")
end))
local callbackSucceeded = pcall(dispatch,
    nativeTokens[badHandle], badHandle, "/Engine/Transient.Bad", "Triggered",
    0.1, 0.0, 1.0, 0.0, 0.0, 0, 43)
expect(not callbackSucceeded)
-- The native update catch deactivates and reaps a failed callback before a
-- later explicit Unbind. Model the resulting already-absent native handle.
nativeTokens[badHandle] = nil
expect(scope:Unbind(badHandle) == true,
    "helper cleanup must tolerate a native callback-failure reap")

local closed, closeError = scope:Close()
expect(closed, closeError)
expect(counters.contexts_removed == 3)
expect(counters.targets_closed == 1)
expect(scope:Close() == true, "Close must be idempotent")

local secondScope = assert(Helpers.OpenInput({
    component_path = "/Game/Test.Component",
    subsystem_path = subsystemPath,
}))
assert(secondScope:Bind("F12", Trigger.Tap, function() end))
local allCount, allCompleted, allError = bridge.UnbindAll()
expect(allCount == 1 and allCompleted and allError == nil,
    "UnbindAll must count helper subscriptions and report completion")
expect(counters.targets_closed == 2)

local debugScope = assert(Helpers.OpenInput({
    component_path = "/Game/Test.Component",
    subsystem_path = subsystemPath,
    debug = true,
    debug_label = "HelperTests",
}))
expect(#scopeTraces == 1 and scopeTraces[1].stage == 1,
    "debug OpenInput must emit scope_opened")

local handlesBeforeDebugBind = nextHandle
local debugEvent
local debugHandle = assert(debugScope:Bind("F10", Trigger.Hold, function(event)
    debugEvent = event
end))
expect(nextHandle - handlesBeforeDebugBind == 4,
    "debug binding must observe Triggered, Started, Completed, and Canceled")
expect(nativeBindings[debugHandle].phase == 1 and nativeBindings[debugHandle].primary,
    "Triggered observer must be the logical primary binding")
local debugScopeId = nativeBindings[debugHandle].scope_id
local debugBindingId = nativeBindings[debugHandle].binding_id
expect(nativeBindings[debugHandle + 1].phase == 2)
expect(nativeBindings[debugHandle + 2].phase == 5)
expect(nativeBindings[debugHandle + 3].phase == 4)
for handle = debugHandle, debugHandle + 3 do
    expect(nativeBindings[handle].debug == true)
    expect(nativeBindings[handle].scope_id == debugScopeId)
    expect(nativeBindings[handle].binding_id == debugBindingId)
    expect(nativeBindings[handle].trigger_kind == 2)
end

dispatch(nativeTokens[debugHandle], debugHandle, "/Engine/Transient.DebugHold", "Triggered",
    1.0, 0.5, 1.0, 0.0, 0.0, 0, 99)
expect(debugEvent.sequence == 99)
expect(debugEvent.scope_id == debugScopeId)
expect(debugEvent.binding_id == debugBindingId)

local nativeUnboundBeforeDebug = counters.native_unbound
expect(debugScope:Unbind(debugHandle) == true)
expect(counters.native_unbound - nativeUnboundBeforeDebug == 4,
    "debug Unbind must remove every phase observer")
expect(debugScope:Close() == true)
expect(#scopeTraces == 2 and scopeTraces[2].stage == 2,
    "debug Close must emit scope_closed")

print("Lua helper tests passed")
