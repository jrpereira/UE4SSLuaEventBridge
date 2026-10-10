-- A fake of the 26 UE4SSLuaFileBridge natives, following
-- bridge-files/include/FileBridgeNativeContract.hpp. It checks every call's
-- arity and argument types (contract R2) and records violations instead of
-- crashing, refuses calls from coroutines like UE4SS (R6), and keeps a small
-- in-memory file store so content can round-trip through the escape.
local Fake = {}

local SESSION = 7
local SAFE_POLICY = 1

-- Argument shapes: i = integer, s = string (no NUL), from native_functions.
local SHAPES = {
    GetVersion = "", GetCapabilities = "", GetDispatchStats = "",
    Locations = "i", Normalize = "is",
    SafePolicy = "i", AddPath = "iisssi", ReleasePolicy = "ii",
    Exists = "iis", Stat = "iis", List = "iis",
    ReadText = "iis", ReadBytes = "iisii",
    MakeDir = "iis", WriteText = "iisisi", Append = "iisis",
    Remove = "iis", RemoveTree = "iis", Copy = "iissi", Move = "iissi",
    StreamOpen = "iisi", StreamWrite = "iiis", StreamFlush = "ii", StreamClose = "ii",
    TailOpen = "iisii", TailClose = "ii",
}
Fake.SHAPES = SHAPES
Fake.SESSION = SESSION
Fake.SAFE_POLICY = SAFE_POLICY

local function decodeEscaped(escaped, length)
    local out, index = {}, 1
    while index <= #escaped do
        local byte = escaped:sub(index, index)
        if byte == "\1" then
            local nextByte = escaped:sub(index + 1, index + 1)
            if nextByte == "\1" then out[#out + 1] = "\1"
            elseif nextByte == "\2" then out[#out + 1] = "\0"
            else return nil end
            index = index + 2
        else
            out[#out + 1] = byte
            index = index + 1
        end
    end
    local text = table.concat(out)
    if #text ~= length then return nil end
    return text
end
Fake.decodeEscaped = decodeEscaped

local LOCATIONS = { "game", "user", "mod", "moddata", "temp", "savegames" }
local ROOTS = { game = true, user = true }

function Fake.new()
    local fake = {
        calls = {},
        violations = {},
        failures = {},
        files = {},
        released = {},
        policies = { [SAFE_POLICY] = { safe = true } },
        livePolicies = 0,
        policyLimit = math.huge,
        nextPolicy = SAFE_POLICY,
        streams = {},
        nextStream = 0,
        subscriptions = {},
        nextSubscription = 0,
        unbound = {},
        listing = "",
    }

    local function normalize(path)
        if path:find("..", 1, true) then return nil, "outside_root", "Normalize " .. path .. ": climbs out" end
        local first, rest = path:match("^([^/\\]+)(.*)$")
        if first == nil then return nil, "invalid", "Normalize " .. path .. ": malformed" end
        if first:match("^%a:$") then return (path:gsub("\\", "/")) end
        for _, name in ipairs(LOCATIONS) do
            if first == name then
                if fake.unbound[name] then return nil, "invalid", "Normalize " .. path .. ": unbound" end
                return "C:/Root/" .. name .. (rest:gsub("\\", "/"))
            end
        end
        return nil, "invalid", "Normalize " .. path .. ": unknown location"
    end

    local impl = {}

    function impl.GetVersion() return "0.1.0" end
    function impl.GetCapabilities() return 1, true, true, true, true, 67108864, "97b7e501" end
    function impl.GetDispatchStats() return 2, 1840, 3 end
    function impl.Locations()
        local records = {}
        for _, name in ipairs(LOCATIONS) do
            local path = fake.unbound[name] and "" or ("C:/Root/" .. name)
            local exists = (fake.unbound[name] or name == "moddata") and "0" or "1"
            records[#records + 1] = table.concat({ name, path, ROOTS[name] and "1" or "0", exists }, "\t")
        end
        return table.concat(records, "\n")
    end
    function impl.Normalize(_, path) return normalize(path) end

    function impl.SafePolicy() return SAFE_POLICY end
    function impl.AddPath(_, parent, path, mode, extensions, flags)
        if not fake.policies[parent] then return nil, "invalid", "AddPath: unknown policy" end
        local normalized, code, message = normalize(path)
        if not normalized then return nil, code, message end
        if fake.livePolicies >= fake.policyLimit then
            return nil, "io", "AddPath " .. path .. ": too many environments"
        end
        fake.nextPolicy = fake.nextPolicy + 1
        fake.livePolicies = fake.livePolicies + 1
        fake.policies[fake.nextPolicy] = {
            parent = parent, path = path, mode = mode, extensions = extensions, flags = flags,
        }
        return fake.nextPolicy
    end
    function impl.ReleasePolicy(_, policy)
        fake.released[#fake.released + 1] = policy
        if policy ~= SAFE_POLICY and fake.policies[policy] then
            fake.policies[policy] = nil
            fake.livePolicies = fake.livePolicies - 1
        end
        return true
    end

    local function file(path)
        local normalized, code, message = normalize(path)
        if not normalized then return nil, code, message end
        return normalized
    end

    function impl.Exists(_, _, path)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        return fake.files[key] ~= nil
    end
    function impl.Stat(_, _, path)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        local content = fake.files[key]
        if content == nil then return nil, "not_found", "Stat " .. key .. ": not found" end
        return "file", #content, 1791640800.25, 1791640000, false, false
    end
    function impl.List() return fake.listing end
    function impl.ReadText(_, _, path)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        local content = fake.files[key]
        if content == nil then return nil, "not_found", "ReadText " .. key .. ": not found" end
        return (content:gsub("^\239\187\191", ""))
    end
    function impl.ReadBytes(_, _, path, offset, length)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        local content = fake.files[key]
        if content == nil then return nil, "not_found", "ReadBytes " .. key .. ": not found" end
        local last = length < 0 and #content or offset + length
        return content:sub(offset + 1, last)
    end
    function impl.MakeDir() return true end
    function impl.WriteText(_, _, path, length, escaped, flags)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        local text = decodeEscaped(escaped, length)
        if text == nil then return nil, "invalid", "WriteText: bad escape" end
        fake.files[key] = text
        fake.lastWrite = { escaped = escaped, length = length, flags = flags }
        return true
    end
    function impl.Append(_, _, path, length, escaped)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        local text = decodeEscaped(escaped, length)
        if text == nil then return nil, "invalid", "Append: bad escape" end
        fake.files[key] = (fake.files[key] or "") .. text
        return true
    end
    function impl.Remove() return true end
    function impl.RemoveTree() return true end
    function impl.Copy() return true end
    function impl.Move() return true end

    function impl.StreamOpen(_, policy, path, flags)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        fake.nextStream = fake.nextStream + 1
        fake.streams[fake.nextStream] = { path = key, flags = flags, policy = policy, open = true, writes = 0 }
        return fake.nextStream, key
    end
    function impl.StreamWrite(_, stream, length, escaped)
        local state = fake.streams[stream]
        if not state or not state.open then return nil, "invalid", "StreamWrite: closed" end
        local text = decodeEscaped(escaped, length)
        if text == nil then return nil, "invalid", "StreamWrite: bad escape" end
        state.writes = state.writes + 1
        fake.files[state.path] = (fake.files[state.path] or "") .. text
        return true
    end
    function impl.StreamFlush(_, stream)
        local state = fake.streams[stream]
        if not state or not state.open then return nil, "invalid", "StreamFlush: closed" end
        return true
    end
    function impl.StreamClose(_, stream)
        local state = fake.streams[stream]
        if state then state.open = false end
        return true
    end

    function impl.TailOpen(_, policy, path, token, flags)
        local key, code, message = file(path)
        if not key then return nil, code, message end
        fake.nextSubscription = fake.nextSubscription + 1
        fake.subscriptions[fake.nextSubscription] = {
            path = key, token = token, flags = flags, policy = policy, open = true,
        }
        return fake.nextSubscription, key
    end
    function impl.TailClose(_, subscription)
        local state = fake.subscriptions[subscription]
        if state then state.open = false end
        return true
    end

    local function checkShape(name, ...)
        local shape = SHAPES[name]
        local count = select("#", ...)
        if count ~= #shape then return false end
        for index = 1, count do
            local value = select(index, ...)
            local slot = shape:sub(index, index)
            if slot == "i" and math.type(value) ~= "integer" then return false end
            if slot == "s" and (type(value) ~= "string" or value:find("\0", 1, true)) then return false end
        end
        return true
    end

    fake.natives = {}
    for name in pairs(SHAPES) do
        fake.natives[name] = function(...)
            local _, main = coroutine.running()
            if not main then
                error("[process_lua_function] The lua state has no instance inside lua_instances unordered map", 0)
            end
            fake.calls[#fake.calls + 1] = { name = name, args = table.pack(...) }
            if not checkShape(name, ...) then
                fake.violations[#fake.violations + 1] = name
                return nil, "invalid", name .. ": bad arguments"
            end
            local shape = SHAPES[name]
            if shape:sub(1, 1) == "i" and name ~= "GetVersion" and select(1, ...) ~= SESSION then
                return nil, "invalid", "Lua session is not registered or is stopping"
            end
            local failure = fake.failures[name]
            if failure then
                fake.failures[name] = nil
                return nil, failure[1], failure[2]
            end
            return impl[name](...)
        end
    end

    function fake.install()
        for name, fn in pairs(fake.natives) do _G["UE4SSLuaFileBridge_" .. name] = fn end
        __UE4SSLuaFileBridge_SessionId = SESSION
    end

    function fake.callsTo(name)
        local found = {}
        for _, call in ipairs(fake.calls) do
            if call.name == name then found[#found + 1] = call end
        end
        return found
    end

    function fake.lastCall()
        return fake.calls[#fake.calls]
    end

    return fake
end

return Fake
