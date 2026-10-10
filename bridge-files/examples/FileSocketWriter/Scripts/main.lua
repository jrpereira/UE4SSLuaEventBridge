-- FileSocketWriter: one half of a file socket. Holds Mods/FileSocketWriter/
-- outbox.txt open and appends a line every second, ten times. Install it next
-- to FileSocketReader, which tails the same file.
--
-- Status: untested in the game until the file bridge's first in-game
-- acceptance run (G3).

local TAG = "[FileSocketWriter] "
local LINES = 10
local INTERVAL_MS = 1000

local function log(text)
    print(TAG .. text .. "\n")
end

if type(UE4SSLuaFileBridge) ~= "table" then
    log("UE4SSLuaFileBridge is not loaded; enable 0_ModCore_UE4SSLuaFileBridge")
    return
end

local files = UE4SSLuaFileBridge() -- the safe set allows writing in this mod's folder

-- Start a fresh file each run. Every Write is handed to the operating system at
-- once (flush = "write" by default), so the reader sees each line immediately.
local outbox, code, message = files.Open("mod/outbox.txt", { append = false })
if outbox == nil then
    log("Open failed: " .. tostring(code) .. " " .. tostring(message))
    return
end
log("writing to " .. outbox.path)

local sent = 0
LoopAsync(INTERVAL_MS, function()
    sent = sent + 1
    local ok, writeCode, writeMessage = outbox:Write("tick ", sent, "\n")
    if ok == nil then
        log("Write failed: " .. tostring(writeCode) .. " " .. tostring(writeMessage))
        outbox:Close()
        return true -- stop the loop
    end
    if sent >= LINES then
        outbox:Close()
        log("sent " .. sent .. " lines; closed")
        return true
    end
    return false
end)
