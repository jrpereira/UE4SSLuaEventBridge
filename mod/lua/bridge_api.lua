local __session = assert(__UE4SSLuaEventBridge_SessionId, "bridge session id is missing")
__UE4SSLuaEventBridge_SessionId = nil

local __callbacks = {}
local __bindings = {}
local __nextCallbackToken = 0

local function __forget(handle)
    local binding = __bindings[handle]
    if binding ~= nil then
        __callbacks[binding.token] = nil
        __bindings[handle] = nil
    end
end

local function __forgetTarget(target)
    local handles = {}
    for handle, binding in pairs(__bindings) do
        if binding.target == target then
            handles[#handles + 1] = handle
        end
    end
    for _, handle in ipairs(handles) do
        __forget(handle)
    end
end

local function __clearCallbacks()
    __callbacks = {}
    __bindings = {}
end

local function __traceback(message)
    if debug ~= nil and debug.traceback ~= nil then
        return debug.traceback(message, 2)
    end
    return tostring(message)
end

local function __dispatch(
    token, handle, sourceAction, phaseName, elapsed, triggered, x, y, z, valueType)
    local callback = __callbacks[token]
    if callback == nil then return end

    local ok, callbackError = xpcall(callback, __traceback, {
        subscription = handle,
        source_type = "enhanced_input",
        action = sourceAction,
        phase = phaseName,
        elapsed_processed = elapsed,
        elapsed_triggered = triggered,
        value = { x = x, y = y, z = z, type = valueType },
    })
    if not ok then
        __forget(handle)
        error(callbackError, 0)
    end
end

local function __packPath(path)
    assert(type(path) == "string", "object path must be a string")
    assert(#path > 0 and #path <= 512, "object path must be 1..512 bytes")
    local words = {}
    for i = 1, 64 do words[i] = 0 end
    for i = 1, #path do
        local word = ((i - 1) // 8) + 1
        local shift = ((i - 1) % 8) * 8
        words[word] = words[word] | (string.byte(path, i) << shift)
    end
    local args = { #path }
    for i = 1, 64 do args[#args + 1] = words[i] end
    return args
end

local bridge = {
    API_VERSION = 3,
    GetVersion = UE4SSLuaEventBridge_GetVersion,
    GetCapabilities = function()
        local api, enhancedInput, explicitTarget, helpers, dynamicInput,
            triggerTap, triggerHold, target = UE4SSLuaEventBridge_GetCapabilities()
        return {
            api = api,
            enhanced_input = enhancedInput,
            explicit_target = explicitTarget,
            helpers = helpers,
            dynamic_input = dynamicInput,
            trigger_tap = triggerTap,
            trigger_hold = triggerHold,
            target_ue4ss_commit = target,
        }
    end,
    OpenInputComponent = function(componentPath)
        local packed = __packPath(componentPath)
        local args = { __session }
        for i = 1, #packed do args[#args + 1] = packed[i] end
        return UE4SSLuaEventBridge_OpenInputComponent(table.unpack(args, 1, #args))
    end,
    CloseInputComponent = function(targetHandle)
        local closed = UE4SSLuaEventBridge_CloseInputComponent(__session, targetHandle)
        if closed then
            __forgetTarget(targetHandle)
        end
        return closed
    end,
    BindAction = function(targetHandle, action, event, callback)
        assert(type(targetHandle) == "number", "targetHandle must come from OpenInputComponent")
        assert(type(action) == "string", "action must be an object path")
        assert(type(event) == "string", "event must be an ETriggerEvent name")
        assert(type(callback) == "function", "callback must be a function")

        local phases = {
            Triggered = 1,
            Started = 2,
            Ongoing = 3,
            Canceled = 4,
            Completed = 5,
        }
        local phase = phases[event]
        assert(phase ~= nil, "unsupported ETriggerEvent name")

        local packed = __packPath(action)
        local args = { __session, targetHandle }
        for i = 1, #packed do args[#args + 1] = packed[i] end
        args[#args + 1] = phase

        __nextCallbackToken = __nextCallbackToken + 1
        local token = __nextCallbackToken
        __callbacks[token] = callback
        args[#args + 1] = token

        local handle, bindError =
            UE4SSLuaEventBridge_BindAction(table.unpack(args, 1, #args))
        if handle == nil then
            __callbacks[token] = nil
            return nil, bindError
        end
        __bindings[handle] = { token = token, target = targetHandle }
        return handle
    end,
    Unbind = function(handle)
        local removed = UE4SSLuaEventBridge_Unbind(__session, handle)
        if removed then
            __forget(handle)
        end
        return removed
    end,
}

bridge.SubscribeEnhancedInput = function(spec, callback)
    assert(type(spec) == "table", "spec must be a table")
    assert(type(spec.target) == "number", "spec.target must come from OpenInputComponent")
    return bridge.BindAction(spec.target, spec.action, spec.event, callback)
end

bridge.Unsubscribe = bridge.Unbind

local __tap = {}
local __hold = {}
local __triggerValues = { Tap = __tap, Hold = __hold }
local Trigger = setmetatable({}, {
    __index = __triggerValues,
    __newindex = function()
        error("Helpers.Trigger is read-only", 2)
    end,
    __pairs = function()
        return next, __triggerValues, nil
    end,
})

local __scopeMethods = {}
local __scopeMetatable = {
    __index = __scopeMethods,
    __newindex = function()
        error("input scopes are read-only", 2)
    end,
}
local __scopeStates = setmetatable({}, { __mode = "k" })
local __inputScopes = {}
local __classCache = {}

local function __validObject(object)
    if object == nil then return false end
    local ok, valid = pcall(function() return object:IsValid() end)
    return ok and valid
end

local function __findObject(path, classPath, label)
    local ok, object = pcall(StaticFindObject, path)
    if not ok then
        return nil, label .. " lookup failed: " .. tostring(object)
    end
    if not __validObject(object) then
        return nil, label .. " did not resolve to a live object: " .. path
    end

    local classOk, isExpectedClass = pcall(function() return object:IsA(classPath) end)
    if not classOk or not isExpectedClass then
        return nil, label .. " is not a " .. classPath .. ": " .. path
    end
    return object
end

local function __getClass(name)
    local cached = __classCache[name]
    if __validObject(cached) then return cached end

    local path = "/Script/EnhancedInput." .. name
    local ok, class = pcall(StaticFindObject, path)
    if not ok then
        return nil, "class lookup failed for " .. path .. ": " .. tostring(class)
    end
    if not __validObject(class) then
        return nil, "Enhanced Input class is unavailable: " .. path
    end
    __classCache[name] = class
    return class
end

local function __construct(name, outer)
    local class, classError = __getClass(name)
    if class == nil then return nil, classError end

    local ok, object = pcall(StaticConstructObject, class, outer, 0, 0x40)
    if not ok then
        return nil, "failed to create transient " .. name .. ": " .. tostring(object)
    end
    if not __validObject(object) then
        return nil, "failed to create transient " .. name
    end
    return object
end

local function __objectPath(object)
    local ok, fullName = pcall(function() return object:GetFullName() end)
    if not ok or type(fullName) ~= "string" then
        return nil, "failed to read generated Input Action path"
    end
    local path = string.match(fullName, "^%S+ (.+)$")
    if path == nil or #path == 0 then
        return nil, "unexpected generated Input Action name: " .. fullName
    end
    return path
end

local function __removeContext(state, record)
    if record.context == nil then return true end
    if not __validObject(state.subsystem) or not __validObject(record.context) then
        record.context = nil
        record.action = nil
        record.trigger_object = nil
        return true
    end

    local ok, removeError = pcall(function()
        state.subsystem:RemoveMappingContext(record.context, {})
    end)
    if not ok then
        return false, "failed to remove private Input Mapping Context: " .. tostring(removeError)
    end
    record.context = nil
    record.action = nil
    record.trigger_object = nil
    return true
end

local function __removeRecord(state, record)
    if record.native_handle ~= nil then
        if not bridge.Unbind(record.native_handle) and not record.callback_failed then
            return false, "failed to unbind native action; run cleanup on the Unreal game thread"
        end
        record.native_handle = nil
    end
    return __removeContext(state, record)
end

function __scopeMethods:Bind(key, trigger, callback, options)
    local state = __scopeStates[self]
    assert(state ~= nil, "invalid input scope")
    if state.closed then return nil, "input scope is closed" end
    assert(type(key) == "string" and #key > 0, "key must be a non-empty Unreal key name")
    assert(trigger == __tap or trigger == __hold, "unsupported Helpers.Trigger constant")
    assert(type(callback) == "function", "callback must be a function")
    assert(options == nil or type(options) == "table", "options must be a table")
    options = options or {}

    local supportedOptions = {
        threshold_seconds = true,
        one_shot = true,
        consume_input = true,
        trigger_when_paused = true,
    }
    for name in pairs(options) do
        assert(supportedOptions[name], "unsupported Bind option: " .. tostring(name))
    end
    if trigger == __tap then
        assert(options.one_shot == nil, "one_shot is only supported by Trigger.Hold")
    end

    if options.threshold_seconds ~= nil then
        assert(type(options.threshold_seconds) == "number"
            and options.threshold_seconds == options.threshold_seconds
            and options.threshold_seconds >= 0
            and options.threshold_seconds < math.huge,
            "threshold_seconds must be a non-negative number")
    end
    if options.one_shot ~= nil then
        assert(type(options.one_shot) == "boolean", "one_shot must be a boolean")
    end
    if options.consume_input ~= nil then
        assert(type(options.consume_input) == "boolean", "consume_input must be a boolean")
    end
    if options.trigger_when_paused ~= nil then
        assert(type(options.trigger_when_paused) == "boolean", "trigger_when_paused must be a boolean")
    end

    local context, createError = __construct("InputMappingContext", state.subsystem)
    if context == nil then return nil, createError end
    local action
    action, createError = __construct("InputAction", context)
    if action == nil then return nil, createError end
    local triggerObject
    local triggerClass = trigger == __tap and "InputTriggerTap" or "InputTriggerHold"
    triggerObject, createError = __construct(triggerClass, action)
    if triggerObject == nil then return nil, createError end

    local record = {
        context = context,
        action = action,
        trigger_object = triggerObject,
        native_handle = nil,
        callback_failed = false,
    }

    local configured, configureError = pcall(function()
        action.ValueType = 0
        action.bConsumeInput = options.consume_input == true
        if options.trigger_when_paused ~= nil then
            action.bTriggerWhenPaused = options.trigger_when_paused
        end

        if trigger == __tap then
            if options.threshold_seconds ~= nil then
                triggerObject.TapReleaseTimeThreshold = options.threshold_seconds
            end
        else
            if options.threshold_seconds ~= nil then
                triggerObject.HoldTimeThreshold = options.threshold_seconds
            end
            if options.one_shot ~= nil then
                triggerObject.bIsOneShot = options.one_shot
            end
        end
        action.Triggers = { triggerObject }
        context:MapKey(action, { KeyName = FName(key) })
        state.subsystem:AddMappingContext(context, state.mapping_priority, {})
    end)
    if not configured then
        local removed, removeError = __removeContext(state, record)
        if not removed then state.orphans[#state.orphans + 1] = record end
        local message = "failed to configure generated Enhanced Input binding: "
            .. tostring(configureError)
        return nil, message .. (removeError and "; " .. removeError or "")
    end

    local actionPath, pathError = __objectPath(action)
    if actionPath == nil then
        local removed, removeError = __removeContext(state, record)
        if not removed then state.orphans[#state.orphans + 1] = record end
        return nil, pathError .. (removeError and "; " .. removeError or "")
    end

    local handle, bindError = bridge.BindAction(state.target, actionPath, "Triggered", function(event)
        event.key = key
        event.trigger = trigger
        local callbackOk, callbackError = xpcall(callback, __traceback, event)
        if not callbackOk then
            record.callback_failed = true
            error(callbackError, 0)
        end
    end)
    if handle == nil then
        local removed, removeError = __removeContext(state, record)
        if not removed then state.orphans[#state.orphans + 1] = record end
        return nil, bindError .. (removeError and "; " .. removeError or "")
    end

    record.native_handle = handle
    state.bindings[handle] = record
    return handle
end

function __scopeMethods:Unbind(handle)
    local state = __scopeStates[self]
    assert(state ~= nil, "invalid input scope")
    assert(type(handle) == "number", "handle must come from input:Bind")
    if state.closed then return false, "input scope is closed" end

    local record = state.bindings[handle]
    if record == nil then return false, "binding is not owned by this input scope" end
    local removed, removeError = __removeRecord(state, record)
    if not removed then return false, removeError end
    state.bindings[handle] = nil
    return true
end

function __scopeMethods:Close()
    local state = __scopeStates[self]
    assert(state ~= nil, "invalid input scope")
    if state.closed then return true end

    local errors = {}
    local handles = {}
    for handle in pairs(state.bindings) do handles[#handles + 1] = handle end
    for _, handle in ipairs(handles) do
        local record = state.bindings[handle]
        local removed, removeError = __removeRecord(state, record)
        if removed then
            state.bindings[handle] = nil
        else
            errors[#errors + 1] = removeError
        end
    end

    local remainingOrphans = {}
    for _, record in ipairs(state.orphans) do
        local removed, removeError = __removeContext(state, record)
        if not removed then
            remainingOrphans[#remainingOrphans + 1] = record
            errors[#errors + 1] = removeError
        end
    end
    state.orphans = remainingOrphans

    if #errors > 0 then return false, table.concat(errors, "; ") end
    if not bridge.CloseInputComponent(state.target) then
        return false, "failed to close Input Component; run cleanup on the Unreal game thread"
    end

    state.closed = true
    state.target = nil
    state.subsystem = nil
    __inputScopes[self] = nil
    return true
end

local Helpers = { Trigger = Trigger }

function Helpers.OpenInput(options)
    assert(type(options) == "table", "OpenInput expects an options table")
    assert(type(options.component_path) == "string" and #options.component_path > 0,
        "component_path must be an exact live UEnhancedInputComponent object path")
    assert(type(options.subsystem_path) == "string" and #options.subsystem_path > 0,
        "subsystem_path must be an exact live UEnhancedInputLocalPlayerSubsystem object path")

    local mappingPriority = options.mapping_priority
    if mappingPriority == nil then mappingPriority = 0 end
    assert(type(mappingPriority) == "number" and mappingPriority % 1 == 0
        and mappingPriority >= -2147483648 and mappingPriority <= 2147483647,
        "mapping_priority must be a 32-bit integer")

    local target, openError = bridge.OpenInputComponent(options.component_path)
    if target == nil then return nil, openError end

    local subsystem, subsystemError = __findObject(
        options.subsystem_path,
        "/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem",
        "subsystem_path")
    if subsystem == nil then
        local closed = bridge.CloseInputComponent(target)
        if not closed then subsystemError = subsystemError .. "; failed to close opened component" end
        return nil, subsystemError
    end

    local scope = setmetatable({}, __scopeMetatable)
    __scopeStates[scope] = {
        target = target,
        subsystem = subsystem,
        mapping_priority = mappingPriority,
        bindings = {},
        orphans = {},
        closed = false,
    }
    __inputScopes[scope] = true
    return scope
end

bridge.Helpers = Helpers

local function __closeHelperScopes()
    local scopes = {}
    for scope in pairs(__inputScopes) do scopes[#scopes + 1] = scope end
    local records = {}
    for _, scope in ipairs(scopes) do
        local state = __scopeStates[scope]
        for _, record in pairs(state.bindings) do
            if record.native_handle ~= nil then records[#records + 1] = record end
        end
    end
    local errors = {}
    for _, scope in ipairs(scopes) do
        local closed, closeError = scope:Close()
        if not closed then errors[#errors + 1] = closeError end
    end
    local nativeRemoved = 0
    for _, record in ipairs(records) do
        if record.native_handle == nil then nativeRemoved = nativeRemoved + 1 end
    end
    if #errors > 0 then return false, table.concat(errors, "; "), nativeRemoved end
    return true, nil, nativeRemoved
end

bridge.UnbindAll = function()
    local _helpersClosed, _helperError, helperCount = __closeHelperScopes()
    local count, completed = UE4SSLuaEventBridge_UnbindAll(__session)
    if completed then __clearCallbacks() end
    return helperCount + count
end

bridge.UnsubscribeAll = bridge.UnbindAll

-- Used by the native Lua-stop path only when shutdown occurs on the game thread.
__UE4SSLuaEventBridge_CloseHelperScopes = __closeHelperScopes
UE4SSLuaEventBridge = bridge

return __dispatch
