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

function UE4SSLuaEventBridge_GetVersion() return "0.3.1" end
function UE4SSLuaEventBridge_GetCapabilities()
    return 3, true, true, true, true, true, true, "97b7e501"
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
    nativeTokens[nextHandle] = args[#args]
    return nextHandle
end
function UE4SSLuaEventBridge_Unbind(_session, handle)
    if nativeTokens[handle] == nil then return false end
    nativeTokens[handle] = nil
    counters.native_unbound = counters.native_unbound + 1
    return true
end
function UE4SSLuaEventBridge_UnbindAll(_session)
    local count = 0
    for handle in pairs(nativeTokens) do
        nativeTokens[handle] = nil
        count = count + 1
    end
    return count, true
end

local dispatch = assert(loadfile("mod/lua/bridge_api.lua"))()
local bridge = UE4SSLuaEventBridge
local Helpers = bridge.Helpers
local Trigger = Helpers.Trigger

expect(bridge.API_VERSION == 3)
expect(bridge.GetCapabilities().helpers == true)

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
    0.1, 0.0, 1.0, 0.0, 0.0, 0)
expect(tapEvent.key == "F10")
expect(tapEvent.trigger == Trigger.Tap)

dispatch(nativeTokens[holdHandle], holdHandle, "/Engine/Transient.Hold", "Triggered",
    0.75, 0.25, 1.0, 0.0, 0.0, 0)
expect(holdEvent.trigger == Trigger.Hold)
expect(holdEvent.elapsed_processed == 0.75)

local removed, removeError = scope:Unbind(tapHandle)
expect(removed, removeError)
expect(counters.contexts_removed == 1)

failNextBind = true
local failedHandle, failedError = scope:Bind("F11", Trigger.Tap, function() end)
expect(failedHandle == nil and failedError == "injected bind failure")
expect(counters.contexts_added == 3)
expect(counters.contexts_removed == 2, "failed Bind must roll back its context")

local badHandle = assert(scope:Bind("F12", Trigger.Tap, function()
    error("injected callback failure")
end))
local callbackSucceeded = pcall(dispatch,
    nativeTokens[badHandle], badHandle, "/Engine/Transient.Bad", "Triggered",
    0.1, 0.0, 1.0, 0.0, 0.0, 0)
expect(not callbackSucceeded)
-- The native update catch deactivates and reaps a failed callback before a
-- later explicit Unbind. Model the resulting already-absent native handle.
nativeTokens[badHandle] = nil
expect(scope:Unbind(badHandle) == true,
    "helper cleanup must tolerate a native callback-failure reap")

local closed, closeError = scope:Close()
expect(closed, closeError)
expect(counters.contexts_removed == 4)
expect(counters.targets_closed == 1)
expect(scope:Close() == true, "Close must be idempotent")

local secondScope = assert(Helpers.OpenInput({
    component_path = "/Game/Test.Component",
    subsystem_path = subsystemPath,
}))
assert(secondScope:Bind("F12", Trigger.Tap, function() end))
expect(bridge.UnbindAll() == 1, "UnbindAll must count helper subscriptions")
expect(counters.targets_closed == 2)

print("Lua helper tests passed")
