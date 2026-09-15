if UE4SSLuaEventBridge == nil then
    print("[CapabilityProbe] UE4SSLuaEventBridge is not loaded")
    return
end

local version = UE4SSLuaEventBridge.GetVersion()
local capabilities = UE4SSLuaEventBridge.GetCapabilities()

print(string.format(
    "[CapabilityProbe] bridge=%s api=%s enhanced_input=%s target=%s",
    tostring(version),
    tostring(capabilities.api),
    tostring(capabilities.enhanced_input),
    tostring(capabilities.target_ue4ss_commit)
))

if not capabilities.enhanced_input then
    print("[CapabilityProbe] Enhanced Input backend unavailable")
    return
end

-- Subscriptions are valid before their action is loaded. This deliberately
-- nonexistent action verifies that the calling Lua thread resolves to the
-- session registered by on_lua_start without depending on a specific game.
local handle, err = UE4SSLuaEventBridge.BindAction(
    "/UE4SSLuaEventBridge/Probe/IA_SessionAlias.IA_SessionAlias",
    "Triggered",
    function() end
)

if handle == nil then
    print("[CapabilityProbe] BindAction failed: " .. tostring(err))
    return
end

local removed = UE4SSLuaEventBridge.Unbind(handle)
print(string.format(
    "[CapabilityProbe] BindAction session probe passed: handle=%s unbound=%s",
    tostring(handle),
    tostring(removed)
))
