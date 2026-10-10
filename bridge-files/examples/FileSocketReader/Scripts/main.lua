-- FileSocketReader: the other half of a file socket. Tails the outbox that
-- FileSocketWriter appends to and logs each line as it arrives. Install both
-- folders under Mods/ and enable them with the file bridge.
--
-- Status: untested in the game until the file bridge's first in-game
-- acceptance run (G3).

local TAG = "[FileSocketReader] "
local EXPECTED = 10

-- Another mod's folder is reached through the game root, which the safe set
-- can read. Tail needs read access only.
local OUTBOX = "game/Dawnwalker/Binaries/Win64/ue4ss/Mods/FileSocketWriter/outbox.txt"

local function log(text)
    print(TAG .. text .. "\n")
end

if type(UE4SSLuaFileBridge) ~= "table" then
    log("UE4SSLuaFileBridge is not loaded; enable 0_ModCore_UE4SSLuaFileBridge")
    return
end

local files = UE4SSLuaFileBridge()
local received = 0
local subscription

-- The file may not exist yet: Tail waits for it as long as its folder exists.
-- from = "start" also picks up lines written before this mod started.
local code, message
subscription, code, message = files.Tail(OUTBOX, function(line, info)
    -- Runs from the bridge's update callback, not the game thread. Schedule
    -- Unreal work with ExecuteInGameThread.
    received = received + 1
    local note = info.reset and " (file was restarted)" or ""
    log("received '" .. line .. "' at offset " .. info.offset .. note)
    if received >= EXPECTED then
        subscription:Close()
        log("got " .. received .. " lines; closed")
    end
end, { from = "start" })

if subscription == nil then
    log("Tail failed: " .. tostring(code) .. " " .. tostring(message))
    return
end
log("tailing " .. subscription.path)

-- If the bridge closes the subscription itself (for example the folder is
-- removed), it logs the reason and sets subscription.closed; there is no
-- callback. Check it when it matters:
--   if subscription.closed then ... end
