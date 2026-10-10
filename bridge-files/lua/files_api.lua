-- UE4SSLuaFileBridge Lua layer (API 1). Embedded in the native DLL and run once
-- per Lua environment from on_lua_start. Public API: bridge-files/docs/API.md.
-- Native boundary: bridge-files/include/FileBridgeNativeContract.hpp.
local __session = __UE4SSLuaFileBridge_SessionId
__UE4SSLuaFileBridge_SessionId = nil
assert(math.type(__session) == "integer", "file bridge session id is missing")

local __API_VERSION = 1
local __NATIVE_PREFIX = "UE4SSLuaFileBridge_"
local __NATIVE_NAMES = {
    "GetVersion", "GetCapabilities", "GetDispatchStats", "Locations", "Normalize",
    "SafePolicy", "AddPath", "ReleasePolicy",
    "Exists", "Stat", "List", "ReadText", "ReadBytes",
    "MakeDir", "WriteText", "Append", "Remove", "RemoveTree", "Copy", "Move",
    "StreamOpen", "StreamWrite", "StreamFlush", "StreamClose",
    "TailOpen", "TailClose",
}

-- Natives crash the game on a wrong argument shape, so only this layer may
-- reach them: capture each one and remove the global (contract R5).
local __natives = {}
for _, name in ipairs(__NATIVE_NAMES) do
    local global = __NATIVE_PREFIX .. name
    local fn = _G[global]
    assert(type(fn) == "function", "file bridge native is missing: " .. global)
    __natives[name] = fn
    _G[global] = nil
end

-- Flag bits (contract section "flag arguments").
local __ADDPATH_DELETE, __ADDPATH_NON_RECURSIVE, __ADDPATH_EXTENSIONS = 1, 2, 4
local __WRITE_NON_ATOMIC = 1
local __COPYMOVE_OVERWRITE = 1
local __OPEN_TRUNCATE, __OPEN_NO_CREATE, __OPEN_MANUAL_FLUSH = 1, 2, 4
local __TAIL_CHUNKS, __TAIL_FROM_START = 1, 2
local __KIND_DATA, __KIND_CLOSED, __KIND_STOP = 1, 2, 3
local __DELIVERY_RESET, __DELIVERY_PARTIAL = 1, 2

local function __fail(message)
    return nil, "invalid", message
end

-- A native raises only when UE4SS refuses the calling Lua thread (a coroutine
-- the mod created, contract R6). That becomes nil, "invalid", message.
-- __refused tells the Lua layer's own callers which case it was.
local __refused = false

local function __finish(name, ok, ...)
    __refused = not ok
    if not ok then
        return nil, "invalid", name .. ": not available here (" .. tostring((...)) .. ")"
    end
    return ...
end

local function __raw(name, ...)
    return __finish(name, pcall(__natives[name], ...))
end

-- Garbage collection only queues ids; no native runs inside a finalizer
-- (contract M11). The queue drains at the start of the next native call.
local __pending = {}

local function __drain()
    if #__pending == 0 then return end
    local work = __pending
    __pending = {}
    for index = 1, #work do
        local item = work[index]
        __raw(item[1], __session, item[2])
        if __refused then
            for rest = index, #work do __pending[#__pending + 1] = work[rest] end
            return
        end
    end
end

local function __native(name, ...)
    __drain()
    return __raw(name, ...)
end

-- Argument checks -----------------------------------------------------------

local function __isPath(value)
    return type(value) == "string" and not value:find("\0", 1, true)
end

local function __pathArg(op, value, label)
    if __isPath(value) then return value end
    return nil, op .. ": " .. (label or "path") .. " must be a string without NUL bytes"
end

local function __integerArg(value)
    if type(value) ~= "number" then return nil end
    return math.tointeger(value)
end

-- Checks an options table against {key = checker}; returns the table or an error.
local function __options(op, options, allowed)
    if options == nil then return {} end
    if type(options) ~= "table" then
        return nil, op .. ": options must be a table"
    end
    for key, value in pairs(options) do
        if type(key) ~= "string" or allowed[key] == nil then
            return nil, op .. ": unknown option " .. tostring(key)
        end
        local message = allowed[key](value)
        if message then return nil, op .. ": option " .. key .. " " .. message end
    end
    return options
end

local function __boolean(value)
    if type(value) ~= "boolean" then return "must be a boolean" end
end

local function __oneOf(...)
    local accepted = {}
    for _, choice in ipairs({ ... }) do accepted[choice] = true end
    local text = table.concat({ ... }, ", ")
    return function(value)
        if not accepted[value] then return "must be one of " .. text end
    end
end

local function __nonNegativeInteger(value)
    local integer = __integerArg(value)
    if integer == nil or integer < 0 then return "must be an integer >= 0" end
end

-- Content escape (contract section 5): \0 -> \1\2, \1 -> \1\1, plus the length.
local __ESCAPES = { ["\0"] = "\1\2", ["\1"] = "\1\1" }

local function __escape(text)
    if text:find("[%z\1]") then
        return #text, (text:gsub("[%z\1]", __ESCAPES))
    end
    return #text, text
end

-- Records (contract section 6): '\n' between records, '\t' between fields,
-- empty fields kept, "" for no records.
local function __split(text, separator)
    local parts, start = {}, 1
    while true do
        local at = text:find(separator, start, true)
        if not at then
            parts[#parts + 1] = text:sub(start)
            return parts
        end
        parts[#parts + 1] = text:sub(start, at - 1)
        start = at + 1
    end
end

local function __records(text)
    local records = {}
    if text == "" then return records end
    for _, line in ipairs(__split(text, "\n")) do
        records[#records + 1] = __split(line, "\t")
    end
    return records
end

-- Lexical helpers (pure Lua) --------------------------------------------------

local function __slashes(path)
    return (path:gsub("\\", "/"))
end

local function __trimTrailing(path)
    if path:match("^%a:/$") or path == "/" then return path end
    return (path:gsub("/+$", ""))
end

local function __join(op, ...)
    local count = select("#", ...)
    if count == 0 then return __fail(op .. ": at least one part is required") end
    local parts = {}
    for index = 1, count do
        local part = select(index, ...)
        if not __isPath(part) then
            return __fail(op .. ": part " .. index .. " must be a string without NUL bytes")
        end
        parts[#parts + 1] = __slashes(part)
    end
    return __trimTrailing((table.concat(parts, "/"):gsub("/+", "/")))
end

-- Last segment of a path, with `\` read as `/` and a trailing slash ignored.
local function __lastSegment(path)
    if not __isPath(path) then return nil end
    local clean = __trimTrailing(__slashes(path))
    return clean:match("[^/]*$"), clean
end

local function __extensionAt(name)
    local dot = name:match("^.*()%.")
    if dot and dot > 1 then return dot end
end

local function __Join(...)
    return __join("Join", ...)
end

local function __Parent(path)
    local name, clean = __lastSegment(path)
    if name == nil then return __fail("Parent: path must be a string without NUL bytes") end
    local parent = clean:sub(1, #clean - #name - 1)
    if name == "" or parent == "" or not clean:find("/", 1, true) then
        return __fail("Parent: " .. path .. " has no parent")
    end
    return parent
end

local function __Name(path)
    local name = __lastSegment(path)
    if name == nil then return __fail("Name: path must be a string without NUL bytes") end
    return name
end

local function __Stem(path)
    local name = __lastSegment(path)
    if name == nil then return __fail("Stem: path must be a string without NUL bytes") end
    local dot = __extensionAt(name)
    return dot and name:sub(1, dot - 1) or name
end

local function __Extension(path)
    local name = __lastSegment(path)
    if name == nil then return __fail("Extension: path must be a string without NUL bytes") end
    local dot = __extensionAt(name)
    return dot and name:sub(dot + 1) or ""
end

-- Version, capabilities, statistics, locations -------------------------------

local function __locationRecords()
    local text, code, message = __native("Locations", __session)
    if text == nil then return nil, code, message end
    return __records(text)
end

local __locationNames

local function __knownLocations()
    if __locationNames then return __locationNames end
    local records, code, message = __locationRecords()
    if records == nil then return nil, code, message end
    local names = {}
    for _, record in ipairs(records) do names[#names + 1] = record[1] end
    __locationNames = names
    return names
end

local function __GetVersion()
    return __native("GetVersion")
end

local function __GetCapabilities()
    local api, environments, appendOnly, streams, tail, maxRead, target =
        __native("GetCapabilities")
    if api == nil then return nil, environments, appendOnly end
    local names, code, message = __knownLocations()
    if names == nil then return nil, code, message end
    local locations = {}
    for index, name in ipairs(names) do locations[index] = name end
    return {
        api = api,
        environments = environments,
        append_only = appendOnly,
        streams = streams,
        tail = tail,
        locations = locations,
        lock = false,
        hash = false,
        temp_file = false,
        max_read_bytes = maxRead,
        target_ue4ss_commit = target,
    }
end

local function __GetDispatchStats()
    local subscriptions, delivered, exhausted = __native("GetDispatchStats")
    if subscriptions == nil then return nil, delivered, exhausted end
    return {
        subscriptions = subscriptions,
        delivered = delivered,
        budget_exhausted_passes = exhausted,
    }
end

-- Environments -----------------------------------------------------------------

local __safePolicy

-- Every operation resolves its environment's policy id through the holder.
-- The safe set's id is fetched once, on first use.
local function __policy(holder)
    if holder.id then return holder.id end
    if __safePolicy then return __safePolicy end
    local id, code, message = __native("SafePolicy", __session)
    if id == nil then return nil, code, message end
    __safePolicy = id
    return id
end

local function __releasePolicy(holder)
    if holder.id and holder.id ~= __safePolicy then
        __pending[#__pending + 1] = { "ReleasePolicy", holder.id }
    end
end

local __releasableHolder = { __gc = __releasePolicy }

local __operations = {}
local __newEnvironment

local function __readOnly()
    error("UE4SSLuaFileBridge objects are read-only", 2)
end

function __newEnvironment(holder)
    local proxy, bound = {}, {}
    setmetatable(proxy, {
        __index = function(_, key)
            local fn = bound[key]
            if fn then return fn end
            local operation = __operations[key]
            if operation == nil then return nil end
            -- env.X(...) and env:X(...) both work: a path is never a table.
            fn = function(first, ...)
                if rawequal(first, proxy) then return operation(holder, ...) end
                return operation(holder, first, ...)
            end
            bound[key] = fn
            return fn
        end,
        __newindex = __readOnly,
        __metatable = false,
    })
    return proxy
end

local function __withPolicy(holder, op)
    local id, code, message = __policy(holder)
    if id == nil then return nil, code, op .. ": " .. tostring(message) end
    return id
end

-- Runs a native that takes (session, policy, path, ...) after checking the path.
local function __pathOperation(name)
    return function(holder, path)
        local checked, message = __pathArg(name, path)
        if checked == nil then return __fail(message) end
        local id, code, why = __withPolicy(holder, name)
        if id == nil then return nil, code, why end
        return __native(name, __session, id, checked)
    end
end

local function __Locations()
    local records, code, message = __locationRecords()
    if records == nil then return nil, code, message end
    local result = {}
    for _, record in ipairs(records) do
        result[record[1]] = {
            path = record[2] ~= "" and record[2] or nil,
            root = record[3] == "1",
            exists = record[4] == "1",
        }
    end
    return result
end

local function __Normalize(path)
    local checked, message = __pathArg("Normalize", path)
    if checked == nil then return __fail(message) end
    return __native("Normalize", __session, checked)
end

local function __Path(name, ...)
    if type(name) ~= "string" then return __fail("Path: location name must be a string") end
    local names, code, message = __knownLocations()
    if names == nil then return nil, code, message end
    local known = false
    for _, candidate in ipairs(names) do
        if candidate == name then known = true break end
    end
    if not known then return __fail("Path: unknown location " .. name) end
    local joined, joinCode, joinMessage = __join("Path", name, ...)
    if joined == nil then return nil, joinCode, joinMessage end
    return __native("Normalize", __session, joined)
end

local __MODES = { ro = true, wo = true, rw = true, ao = true, stat = true }
local __NO_DELETE_MODES = { ro = true, stat = true, ao = true }

local function __extensionsList(value)
    if type(value) ~= "table" then return nil, "must be a list of strings" end
    local count = 0
    for _ in pairs(value) do count = count + 1 end
    local entries = {}
    for index = 1, count do
        local entry = value[index]
        if type(entry) ~= "string" then return nil, "must be a list of strings" end
        if entry == "" or entry:find("[%c%./\\;<>:\"|%?%*]") then
            return nil, "has an invalid entry " .. string.format("%q", entry)
        end
        entries[index] = entry
    end
    return table.concat(entries, ";")
end

local __ADDPATH_OPTIONS = {
    mode = function(value)
        if not __MODES[value] then return "must be one of ro, wo, rw, ao, stat" end
    end,
    extensions = function(value)
        local _, message = __extensionsList(value)
        return message
    end,
    delete = __boolean,
    recursive = __boolean,
}

-- AddPath raises at the caller's line (API.md). It is reached by tail calls
-- from the bound function, so level 2 is the mod's call site.
function __operations.AddPath(holder, path, options)
    if not __isPath(path) then
        error("invalid: AddPath: path must be a string without NUL bytes", 2)
    end
    local checked, message = __options("AddPath", options, __ADDPATH_OPTIONS)
    if checked == nil then error("invalid: " .. message, 2) end
    if checked.delete == true and __NO_DELETE_MODES[checked.mode] then
        error("invalid: AddPath: delete can't be combined with mode " .. checked.mode, 2)
    end
    local flags, extensions = 0, ""
    if checked.delete == true then flags = flags | __ADDPATH_DELETE end
    if checked.recursive == false then flags = flags | __ADDPATH_NON_RECURSIVE end
    if checked.extensions ~= nil then
        flags = flags | __ADDPATH_EXTENSIONS
        extensions = __extensionsList(checked.extensions)
    end
    local parent, code, why = __withPolicy(holder, "AddPath")
    if parent == nil then error(code .. ": " .. tostring(why), 2) end
    local mode = checked.mode or ""
    local id
    id, code, why = __native("AddPath", __session, parent, path, mode, extensions, flags)
    if id == nil and code == "io" then
        -- The policy limit counts environments that are unreachable but not yet
        -- collected: collect, release them, and try once more.
        collectgarbage()
        id, code, why = __native("AddPath", __session, parent, path, mode, extensions, flags)
    end
    if id == nil then error(tostring(code) .. ": " .. tostring(why), 2) end
    return __newEnvironment(setmetatable({ id = id }, __releasableHolder))
end

__operations.Exists = __pathOperation("Exists")
__operations.MakeDir = __pathOperation("MakeDir")
__operations.ReadText = __pathOperation("ReadText")
__operations.Remove = __pathOperation("Remove")
__operations.RemoveTree = __pathOperation("RemoveTree")

local __statNative = __pathOperation("Stat")
local __listNative = __pathOperation("List")

function __operations.Stat(holder, path)
    local kind, size, modified, created, readonly, link = __statNative(holder, path)
    if kind == nil then return nil, size, modified end
    return {
        type = kind,
        size = size,
        modified = modified,
        created = created,
        readonly = readonly,
        link = link,
    }
end

function __operations.List(holder, path)
    local text, code, message = __listNative(holder, path)
    if text == nil then return nil, code, message end
    -- Fields: name, type, size, modified, link, created. A missing field
    -- (an older record without `created`) decodes as nil.
    local entries = {}
    for index, record in ipairs(__records(text)) do
        entries[index] = {
            name = record[1],
            type = record[2],
            size = record[3] and tonumber(record[3]),
            modified = record[4] and tonumber(record[4]),
            link = record[5] == "1",
            created = record[6] and tonumber(record[6]),
        }
    end
    return entries
end

local __READBYTES_OPTIONS = { offset = __nonNegativeInteger, length = __nonNegativeInteger }

function __operations.ReadBytes(holder, path, options)
    local checked, message = __pathArg("ReadBytes", path)
    if checked == nil then return __fail(message) end
    local opts
    opts, message = __options("ReadBytes", options, __READBYTES_OPTIONS)
    if opts == nil then return __fail(message) end
    local offset = opts.offset ~= nil and __integerArg(opts.offset) or 0
    local length = opts.length ~= nil and __integerArg(opts.length) or -1
    local id, code, why = __withPolicy(holder, "ReadBytes")
    if id == nil then return nil, code, why end
    return __native("ReadBytes", __session, id, checked, offset, length)
end

local function __contentArg(op, text)
    if type(text) ~= "string" then return nil, op .. ": text must be a string" end
    return text
end

local __WRITETEXT_OPTIONS = { atomic = __boolean }

function __operations.WriteText(holder, path, text, options)
    local checked, message = __pathArg("WriteText", path)
    if checked == nil then return __fail(message) end
    local content
    content, message = __contentArg("WriteText", text)
    if content == nil then return __fail(message) end
    local opts
    opts, message = __options("WriteText", options, __WRITETEXT_OPTIONS)
    if opts == nil then return __fail(message) end
    local flags = opts.atomic == false and __WRITE_NON_ATOMIC or 0
    local id, code, why = __withPolicy(holder, "WriteText")
    if id == nil then return nil, code, why end
    local length, escaped = __escape(content)
    return __native("WriteText", __session, id, checked, length, escaped, flags)
end

function __operations.Append(holder, path, text)
    local checked, message = __pathArg("Append", path)
    if checked == nil then return __fail(message) end
    local content
    content, message = __contentArg("Append", text)
    if content == nil then return __fail(message) end
    local id, code, why = __withPolicy(holder, "Append")
    if id == nil then return nil, code, why end
    local length, escaped = __escape(content)
    return __native("Append", __session, id, checked, length, escaped)
end

local __COPYMOVE_OPTIONS = { overwrite = __boolean }

local function __copyMove(name)
    return function(holder, from, to, options)
        local source, message = __pathArg(name, from, "from")
        if source == nil then return __fail(message) end
        local target
        target, message = __pathArg(name, to, "to")
        if target == nil then return __fail(message) end
        local opts
        opts, message = __options(name, options, __COPYMOVE_OPTIONS)
        if opts == nil then return __fail(message) end
        local flags = opts.overwrite == true and __COPYMOVE_OVERWRITE or 0
        local id, code, why = __withPolicy(holder, name)
        if id == nil then return nil, code, why end
        return __native(name, __session, id, source, target, flags)
    end
end

__operations.Copy = __copyMove("Copy")
__operations.Move = __copyMove("Move")

-- Streams ------------------------------------------------------------------------

local __streams = setmetatable({}, { __mode = "k" })
local __Stream = {}

local function __streamState(op, self)
    local state = __streams[self]
    if state == nil then return nil, "stream:" .. op .. ": call it with a colon on a stream" end
    return state
end

function __Stream.Write(self, ...)
    local state, message = __streamState("Write", self)
    if state == nil then return __fail(message) end
    if state.closed then return __fail("stream is closed") end
    local count = select("#", ...)
    if count == 0 then return true end
    local parts = {}
    for index = 1, count do
        local part = select(index, ...)
        local kind = type(part)
        if kind == "number" then
            part = tostring(part)
        elseif kind ~= "string" then
            return __fail("stream:Write: argument " .. index .. " must be a string or a number")
        end
        parts[index] = part
    end
    local length, escaped = __escape(table.concat(parts))
    return __native("StreamWrite", __session, state.id, length, escaped)
end

function __Stream.Flush(self)
    local state, message = __streamState("Flush", self)
    if state == nil then return __fail(message) end
    if state.closed then return __fail("stream is closed") end
    return __native("StreamFlush", __session, state.id)
end

function __Stream.Close(self)
    local state, message = __streamState("Close", self)
    if state == nil then return __fail(message) end
    if state.closed then return true end
    state.closed = true
    local ok, code, why = __native("StreamClose", __session, state.id)
    if __refused then
        __pending[#__pending + 1] = { "StreamClose", state.id }
        return nil, code, why
    end
    if ok == nil then return nil, code, why end
    return true
end

local __streamMeta = {
    __index = function(self, key)
        if key == "path" then
            local state = __streams[self]
            return state and state.path
        end
        return __Stream[key]
    end,
    __newindex = __readOnly,
    __metatable = false,
    __gc = function(self)
        local state = __streams[self]
        if state and not state.closed then
            state.closed = true
            __pending[#__pending + 1] = { "StreamClose", state.id }
        end
    end,
}

local __OPEN_OPTIONS = {
    append = __boolean,
    create = __boolean,
    flush = __oneOf("write", "manual"),
}

function __operations.Open(holder, path, options)
    local checked, message = __pathArg("Open", path)
    if checked == nil then return __fail(message) end
    local opts
    opts, message = __options("Open", options, __OPEN_OPTIONS)
    if opts == nil then return __fail(message) end
    local flags = 0
    if opts.append == false then flags = flags | __OPEN_TRUNCATE end
    if opts.create == false then flags = flags | __OPEN_NO_CREATE end
    if opts.flush == "manual" then flags = flags | __OPEN_MANUAL_FLUSH end
    local id, code, why = __withPolicy(holder, "Open")
    if id == nil then return nil, code, why end
    local stream, normalized
    stream, normalized, why = __native("StreamOpen", __session, id, checked, flags)
    if stream == nil then return nil, normalized, why end
    local proxy = setmetatable({}, __streamMeta)
    __streams[proxy] = { id = stream, path = normalized, closed = false }
    return proxy
end

-- Tail subscriptions ----------------------------------------------------------

local __subscriptions = setmetatable({}, { __mode = "k" })
local __callbacks = {}
local __nextToken = 0
local __Subscription = {}

function __Subscription.Close(self)
    local state = __subscriptions[self]
    if state == nil then return __fail("sub:Close: call it with a colon on a subscription") end
    if state.closed then return true end
    state.closed = true
    __callbacks[state.token] = nil
    local ok, code, why = __native("TailClose", __session, state.id)
    if __refused then
        __pending[#__pending + 1] = { "TailClose", state.id }
        return nil, code, why
    end
    if ok == nil then return nil, code, why end
    return true
end

local __subscriptionMeta = {
    __index = function(self, key)
        local state = __subscriptions[self]
        if key == "closed" then return state == nil or state.closed end
        if key == "path" then return state and state.path end
        return __Subscription[key]
    end,
    __newindex = __readOnly,
    __metatable = false,
}

local __TAIL_OPTIONS = { chunks = __boolean, from = __oneOf("end", "start") }

function __operations.Tail(holder, path, fn, options)
    local checked, message = __pathArg("Tail", path)
    if checked == nil then return __fail(message) end
    if type(fn) ~= "function" then return __fail("Tail: callback must be a function") end
    local opts
    opts, message = __options("Tail", options, __TAIL_OPTIONS)
    if opts == nil then return __fail(message) end
    local flags = 0
    if opts.chunks == true then flags = flags | __TAIL_CHUNKS end
    if opts.from == "start" then flags = flags | __TAIL_FROM_START end
    local id, code, why = __withPolicy(holder, "Tail")
    if id == nil then return nil, code, why end
    __nextToken = __nextToken + 1
    local token = __nextToken
    local state = { token = token, closed = false }
    -- Registered before the native call so no delivery can find it missing.
    __callbacks[token] = { fn = fn, state = state }
    local subscription, normalized
    subscription, normalized, why = __native("TailOpen", __session, id, checked, token, flags)
    if subscription == nil then
        __callbacks[token] = nil
        return nil, normalized, why
    end
    state.id = subscription
    state.path = normalized
    local proxy = setmetatable({}, __subscriptionMeta)
    __subscriptions[proxy] = state
    return proxy
end

-- Functions every environment shares with the global table.
__operations.Path = function(_, ...) return __Path(...) end
__operations.Locations = function() return __Locations() end
__operations.Normalize = function(_, path) return __Normalize(path) end
__operations.Join = function(_, ...) return __Join(...) end
__operations.Parent = function(_, path) return __Parent(path) end
__operations.Name = function(_, path) return __Name(path) end
__operations.Stem = function(_, path) return __Stem(path) end
__operations.Extension = function(_, path) return __Extension(path) end
__operations.GetVersion = function() return __GetVersion() end
__operations.GetCapabilities = function() return __GetCapabilities() end
__operations.GetDispatchStats = function() return __GetDispatchStats() end

-- Dispatcher -------------------------------------------------------------------

local function __stop()
    for _, entry in pairs(__callbacks) do entry.state.closed = true end
    __callbacks = {}
    for _, state in pairs(__streams) do state.closed = true end
    __pending = {}
end

-- Called by the native side on the mod's root Lua state (contract section 7).
local function __dispatch(kind, token, text, offset, flags)
    if kind == __KIND_DATA then
        local entry = __callbacks[token]
        if entry == nil then return end
        local info = {
            offset = offset,
            reset = flags & __DELIVERY_RESET ~= 0,
            partial = flags & __DELIVERY_PARTIAL ~= 0,
        }
        local ok, failure = pcall(entry.fn, text, info)
        if not ok then
            -- The bridge closes the subscription and logs the error.
            __callbacks[token] = nil
            entry.state.closed = true
            error(failure, 0)
        end
    elseif kind == __KIND_CLOSED then
        local entry = __callbacks[token]
        if entry == nil then return end
        __callbacks[token] = nil
        entry.state.closed = true
    elseif kind == __KIND_STOP then
        __stop()
    end
end

-- Global table -------------------------------------------------------------------

local __safeEnvironment = __newEnvironment({})

UE4SSLuaFileBridge = setmetatable({}, {
    __index = {
        API_VERSION = __API_VERSION,
        GetVersion = __GetVersion,
        GetCapabilities = __GetCapabilities,
        GetDispatchStats = __GetDispatchStats,
    },
    __call = function() return __safeEnvironment end,
    __newindex = __readOnly,
    __metatable = false,
})

return __dispatch
