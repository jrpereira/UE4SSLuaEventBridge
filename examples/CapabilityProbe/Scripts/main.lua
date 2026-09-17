if UE4SSLuaEventBridge == nil then
    print("[CapabilityProbe] UE4SSLuaEventBridge is not loaded")
    return
end

local version = UE4SSLuaEventBridge.GetVersion()
local capabilities = UE4SSLuaEventBridge.GetCapabilities()

print(string.format(
    "[CapabilityProbe] bridge=%s api=%s enhanced_input=%s helpers=%s detailed_errors=%s debug_tracing=%s target=%s",
    tostring(version),
    tostring(capabilities.api),
    tostring(capabilities.enhanced_input),
    tostring(capabilities.helpers),
    tostring(capabilities.detailed_errors),
    tostring(capabilities.debug_tracing),
    tostring(capabilities.target_ue4ss_commit)
))

if not capabilities.enhanced_input then
    print("[CapabilityProbe] Enhanced Input backend unavailable")
    return
end

print("[CapabilityProbe] capability probe passed")
