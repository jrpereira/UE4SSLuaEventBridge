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

