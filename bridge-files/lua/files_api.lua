-- Placeholder for the UE4SSLuaFileBridge Lua layer, so the native product
-- builds and loads before the helper layer (owned by the Lua track) is merged.
-- It follows the load contract only (FileBridgeNativeContract.hpp section 2):
-- read and clear the session id, capture and clear the natives, define the
-- global, and return the dispatcher.

local session = assert(__UE4SSLuaFileBridge_SessionId, "file bridge session id is missing")
__UE4SSLuaFileBridge_SessionId = nil

local NAMES = {
    "GetVersion", "GetCapabilities", "GetDispatchStats", "Locations", "Normalize",
    "SafePolicy", "AddPath", "ReleasePolicy", "Exists", "Stat", "List", "ReadText",
    "ReadBytes", "MakeDir", "WriteText", "Append", "Remove", "RemoveTree", "Copy",
    "Move", "StreamOpen", "StreamWrite", "StreamFlush", "StreamClose", "TailOpen",
    "TailClose",
}

local native = {}
for _, name in ipairs(NAMES) do
    local global = "UE4SSLuaFileBridge_" .. name
    native[name] = assert(_G[global], global .. " is missing")
    _G[global] = nil
end

UE4SSLuaFileBridge = {
    API_VERSION = 1,
    GetVersion = function() return native.GetVersion() end,
    Locations = function() return native.Locations(session) end,
}

return function(kind, token, text, offset, flags) end
