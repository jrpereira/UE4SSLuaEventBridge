-- Offline integration: real bridge_api.lua, simulated UE objects/native boundary.
-- No claim of C++ shutdown, real concurrency, Unreal GC, or ABI validation.
local function check(value, message)
    if not value then error(message or "expectation failed", 2) end
    return value
end
local function count(t)
    local n = 0
    for _ in pairs(t) do n = n + 1 end
    return n
end

local function fixture()
    local f = {
        gameThread = true, contexts = {}, bindings = {}, targets = {},
        nextId = 0, nextSession = 0, removeFailures = 0, unbindFailures = 0,
        removals = 0, mutations = 0, componentAlive = true,
    }
    function f:id() self.nextId = self.nextId + 1; return self.nextId end
    function f:mutate()
        check(self.gameThread, "reflected mutation attempted off game thread")
        self.mutations = self.mutations + 1
    end
    local function object(kind)
        local o = { valid = true, path = "/Engine/Transient.Lifecycle_" .. f:id() }
        function o:IsValid() return self.valid end
        function o:IsA(name)
            return name == "/Script/EnhancedInput." .. kind
                or (kind == "Class" and name == "/Script/CoreUObject.Class")
        end
        function o:GetFullName() return kind .. " " .. self.path end
        return o
    end
    f.subsystem = object("EnhancedInputLocalPlayerSubsystem")
    function f.subsystem:AddMappingContext(context)
        f:mutate()
        f.contexts[context] = true
    end
    function f.subsystem:RemoveMappingContext(context)
        check(self.valid, "called destroyed subsystem")
        f:mutate()
        if f.removeFailures > 0 then
            f.removeFailures = f.removeFailures - 1
            error("injected removal failure")
        end
        check(f.contexts[context], "context removed twice")
        f.contexts[context] = nil
        f.removals = f.removals + 1
    end
    function f:loadSession()
        self.nextSession = self.nextSession + 1
        local session = { id = self.nextSession }
        local env = setmetatable({ __UE4SSLuaEventBridge_SessionId = session.id }, { __index = _G })
        env.FName = function(s) return s end
        env.StaticFindObject = function(path)
            if path == "Subsystem" then return self.subsystem end
            if path == "/Script/Engine.Default__KismetSystemLibrary" then
                return { IsValid = function() return true end,
                    Conv_ObjectToSoftObjectReference = function() self:mutate() end }
            end
            local c = object("Class")
            c.class = path:match("%.([^%.]+)$")
            return c
        end
        env.StaticConstructObject = function(class)
            self:mutate()
            local o = object(class.class)
            function o:MapKey(action, key) self.action = action; self.key = key end
            return o
        end
        local function own(id) check(id == session.id, "cross-session native call") end
        env.UE4SSLuaEventBridge_GetVersion = function() return "test-fixture" end
        env.UE4SSLuaEventBridge_GetCapabilities = function()
            return 4, true, true, true, true, true, true, true, true, "fixture", true
        end
        env.UE4SSLuaEventBridge_IsInGameThread = function() return self.gameThread end
        env.UE4SSLuaEventBridge_TraceScope = function() end
        env.UE4SSLuaEventBridge_OpenInputComponent = function(id)
            own(id)
            local target = self:id()
            self.targets[target] = id
            return target
        end
        env.UE4SSLuaEventBridge_CloseInputComponent = function(id, target)
            own(id)
            check(self.targets[target] == id, "target closed twice or by wrong session")
            self.targets[target] = nil
            return true
        end
        env.UE4SSLuaEventBridge_BindAction = function(...)
            local args = {...}
            own(args[1])
            check(self.componentAlive, "bind after component destruction")
            local handle = self:id()
            self.bindings[handle] = { session = session, token = args[69], handle = handle }
            return handle
        end
        env.UE4SSLuaEventBridge_Unbind = function(id, handle)
            own(id)
            if self.unbindFailures > 0 then
                self.unbindFailures = self.unbindFailures - 1
                return false, "injected native removal failure"
            end
            local binding = self.bindings[handle]
            check(binding and binding.session == session, "invalid native unbind")
            -- Backend contract: dead component does not prevent logical unsubscribe.
            self.bindings[handle] = nil
            return true
        end
        session.dispatch = check(loadfile("mod/lua/bridge_api.lua", "t", env))()
        session.api = env.UE4SSLuaEventBridge
        session.cleanup = env.__UE4SSLuaEventBridge_CloseHelperScopes
        function session:open(debugEnabled)
            return check(self.api.Helpers.OpenInput({
                component_path = "Component", subsystem_path = "Subsystem",
                debug = debugEnabled or false,
            }))
        end
        return session
    end
    function f:queued(handle)
        local b = check(self.bindings[handle])
        -- Capture before teardown, as an already-drained native event would.
        return function()
            b.session.dispatch(b.token, b.handle, "Action", "Triggered", 0, 0, 1, 0, 0, 0, 1)
        end
    end
    function f:empty()
        check(count(self.contexts) == 0, "mapping contexts leaked")
        check(count(self.bindings) == 0, "native subscriptions leaked")
        check(count(self.targets) == 0, "component targets leaked")
    end
    return f
end

local cases = {}
cases["off-thread helper shutdown refuses mutation and permits retry"] = function()
    local f = fixture()
    local s = f:loadSession()
    local scope = s:open()
    check(scope:Bind("A", s.api.Helpers.Trigger.Tap, function() end))
    local before = f.mutations
    f.gameThread = false
    local ok, why = s.cleanup()
    check(not ok and why:find("game thread", 1, true), "off-thread cleanup must report failure")
    check(f.mutations == before and count(f.contexts) == 1 and count(f.bindings) == 1)
    f.gameThread = true
    check(s.cleanup())
    f:empty()
end

cases["100 fresh Lua sessions clean up and reject old queued callbacks"] = function()
    local f, total, stale = fixture(), 0, {}
    for _ = 1, 100 do
        local s = f:loadSession()
        local scope = s:open(true)
        local handle = check(scope:Bind("A", s.api.Helpers.Trigger.Tap, function() total = total + 1 end))
        check(count(f.bindings) == 4, "debug observers missing")
        local event = f:queued(handle)
        event()
        stale[#stale + 1] = event
        check(s.cleanup())
        check(s.cleanup(), "cleanup must be idempotent")
        f:empty()
        for _, oldEvent in ipairs(stale) do oldEvent() end
    end
    check(total == 100, "old session callbacks ran after cleanup")
end

cases["failed context removal is retained, reported, and retried"] = function()
    local f, calls = fixture(), 0
    local s = f:loadSession()
    local scope = s:open(true)
    local handle = check(scope:Bind("A", s.api.Helpers.Trigger.Hold, function() calls = calls + 1 end))
    local event = f:queued(handle)
    f.removeFailures = 2
    for _ = 1, 2 do
        local ok, why = s.cleanup()
        check(not ok and why:find("injected removal failure", 1, true))
        check(count(f.contexts) == 1 and count(f.targets) == 1 and count(f.bindings) == 0)
        event()
        check(calls == 0, "callback survived failed context cleanup")
    end
    check(s.cleanup())
    check(f.removals == 1)
    f:empty()
end

cases["partial native removal failure retains retry ownership"] = function()
    local f = fixture()
    local s = f:loadSession()
    local scope = s:open(true)
    check(scope:Bind("A", s.api.Helpers.Trigger.Tap, function() end))
    f.unbindFailures = 1
    local ok, why = s.cleanup()
    check(not ok and why:find("injected native removal failure", 1, true))
    check(count(f.bindings) == 1 and count(f.contexts) == 0 and count(f.targets) == 1)
    check(s.cleanup())
    check(f.removals == 1, "retry removed context twice")
    f:empty()
end

cases["component destruction allows helper teardown and suppresses late callback"] = function()
    local f, calls = fixture(), 0
    local s = f:loadSession()
    local scope = s:open()
    local handle = check(scope:Bind("A", s.api.Helpers.Trigger.Tap, function() calls = calls + 1 end))
    local event = f:queued(handle)
    event()
    f.componentAlive = false
    check(s.cleanup())
    event()
    check(calls == 1)
    f:empty()
end

cases["late callbacks after explicit close do not reach a new scope"] = function()
    local f, first, second = fixture(), 0, 0
    local s = f:loadSession()
    local a = s:open()
    local h = check(a:Bind("A", s.api.Helpers.Trigger.Tap, function() first = first + 1 end))
    local event = f:queued(h)
    check(a:Close())
    local b = s:open()
    local h2 = check(b:Bind("A", s.api.Helpers.Trigger.Tap, function() second = second + 1 end))
    event()
    f:queued(h2)()
    check(first == 0 and second == 1, "stale callback misrouted")
    check(s.cleanup())
    f:empty()
end


cases["subsystem destruction avoids reflected calls into invalid objects"] = function()
    local f = fixture()
    local s = f:loadSession()
    local scope = s:open()
    check(scope:Bind("A", s.api.Helpers.Trigger.Tap, function() end))
    -- Engine destruction removes its contexts; Lua wrappers remain but are invalid.
    f.subsystem.valid = false
    f.contexts = {}
    local before = f.mutations
    check(s.cleanup())
    check(f.mutations == before, "cleanup touched destroyed subsystem")
    f:empty()
end

cases["one failed scope does not prevent cleanup of other scopes"] = function()
    local f = fixture()
    local s = f:loadSession()
    for _, key in ipairs({"A", "B", "C"}) do
        local scope = s:open()
        check(scope:Bind(key, s.api.Helpers.Trigger.Tap, function() end))
    end
    f.removeFailures = 1
    local ok, why = s.cleanup()
    check(not ok and why:find("injected removal failure", 1, true))
    check(count(f.bindings) == 0 and count(f.contexts) == 1 and count(f.targets) == 1,
        "cleanup abandoned healthy scopes")
    check(s.cleanup())
    check(f.removals == 3)
    f:empty()
end

local names = {}
for name in pairs(cases) do names[#names + 1] = name end
table.sort(names)
for _, name in ipairs(names) do
    cases[name]()
    print("PASS: " .. name)
end
print("Lifecycle helper integration tests passed (" .. #names .. " cases; simulated engine/native boundary)")
