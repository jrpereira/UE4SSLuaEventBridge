-- FileRoundTrip: writes, reads, lists and removes files under this mod's own
-- folder, then shows three refusals. Copy this folder to Mods/FileRoundTrip,
-- enable it and the file bridge, and read the [FileRoundTrip] lines in
-- UE4SS.log.
--
-- Status: untested in the game until the file bridge's first in-game
-- acceptance run (G3).

local TAG = "[FileRoundTrip] "

local function log(text)
    print(TAG .. text .. "\n")
end

-- Logs one step. Bridge operations return their value, or nil, code, message.
local function step(label, value, code, message)
    if value == nil then
        log(label .. ": FAILED " .. tostring(code) .. " " .. tostring(message))
    else
        log(label .. ": ok")
    end
    return value
end

-- Logs a call that is expected to be refused with a specific code.
local function refused(label, expected, value, code, message)
    if value == nil and code == expected then
        log(label .. ": refused as expected (" .. code .. ") " .. tostring(message))
    else
        log(label .. ": UNEXPECTED result " .. tostring(value) .. " " .. tostring(code))
    end
end

local function run()
    if type(UE4SSLuaFileBridge) ~= "table" then
        log("UE4SSLuaFileBridge is not loaded; enable 0_ModCore_UE4SSLuaFileBridge")
        return
    end
    log("bridge " .. UE4SSLuaFileBridge.GetVersion())

    -- The safe set: read-only on the game and user roots, read-write (without
    -- delete) on this mod's folder and temp.
    local safe = UE4SSLuaFileBridge()

    -- Allow deleting inside one subfolder only. The mode is inherited (rw from
    -- the mod folder). A bad option here raises an error at this line.
    local files = safe.AddPath("mod/roundtrip", { delete = true })

    local folder = "mod/roundtrip"
    local plain = files.Join(folder, "hello.txt")
    local unicode = files.Join(folder, "café-ñ.txt") -- a non-ASCII file name
    local content = "Hello from FileRoundTrip\nolá, ünïcode\n"

    step("MakeDir " .. folder, files.MakeDir(folder))
    step("WriteText " .. plain, files.WriteText(plain, content))      -- atomic by default
    step("WriteText " .. unicode, files.WriteText(unicode, content))
    step("Append " .. plain, files.Append(plain, "one more line\n"))

    local text = step("ReadText " .. plain, files.ReadText(plain))
    if text ~= nil then
        log("read back " .. #text .. " bytes; matches: "
            .. tostring(text == content .. "one more line\n"))
    end

    local info = step("Stat " .. unicode, files.Stat(unicode))
    if info ~= nil then
        log("  type=" .. info.type .. " size=" .. info.size)
    end

    local entries = step("List " .. folder, files.List(folder))
    if entries ~= nil then
        for _, entry in ipairs(entries) do
            log("  " .. entry.name .. " (" .. entry.type .. ", " .. entry.size .. " bytes)")
        end
    end

    log("absolute path: " .. tostring(files.Path("mod", "roundtrip", "hello.txt")))

    -- Refusals.
    -- 1. The safe set has no delete on the mod folder itself.
    refused("Remove without a delete grant", "denied", safe.Remove(plain))
    -- 2. Saves are read-only until granted.
    refused("WriteText to savegames", "denied",
        safe.WriteText("savegames/FileRoundTrip.sav", "not a save"))
    -- 3. Absolute paths outside the game and user roots are never reachable.
    refused("ReadText outside the roots", "outside_root",
        files.ReadText("C:/Windows/win.ini"))

    -- Clean up: files first, then the now empty folder.
    step("Remove " .. plain, files.Remove(plain))
    step("Remove " .. unicode, files.Remove(unicode))
    step("Remove " .. folder, files.Remove(folder))
    log("exists after cleanup: " .. tostring(files.Exists(folder)))
end

run()
