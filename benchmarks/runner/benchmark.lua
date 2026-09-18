-- Separate integration consumer: only the baseline polls raw input state.
local out = os.getenv("BRIDGE_BENCH_RESULTS") or "C:/TestResults"
local function write(name, value)
    local f=assert(io.open(out.."/"..name,"w")); f:write(value); f:close()
end
local function guarded(fn)
    return function(...)
        local ok, err = xpcall(fn, debug.traceback, ...)
        if not ok then write("failure.txt", tostring(err)); error(err) end
    end
end
local tapThresholdSeconds=0.250
local baseline=os.getenv("BRIDGE_BENCH_MODE")=="Baseline"
local fountain=os.getenv("BRIDGE_BENCH_SCHEDULE")=="Fountain"
local inputKeys={}
if fountain then
    for vk=0x41,0x54 do inputKeys[#inputKeys+1]={name=string.char(vk),vk=vk} end
else
    inputKeys={{name="F9",vk=0x78}} -- Historical single-key Sweep stays separate.
end
local targetCount=600
local bridge=not baseline and assert(UE4SSLuaEventBridge, "Bridge unavailable")
local seconds=assert(BridgeBenchmarkSeconds, "Test clock unavailable")
local input, binding, running, stage, started, firstFrame
stage=0
local samples={}
local cpuSamples={}
local cpuSnapshot=assert(BridgeBenchmarkThreadCpu,"Thread CPU counter unavailable")
local workload={count=0, sum=0}
local completedAt, completedCpu
local perKey={}
local processCpu=assert(BridgeBenchmarkProcessCpu,"Process CPU counter unavailable")
local csv=out.."/callbacks.csv"
local function frameCount()
    local ok,value=pcall(function()
        return StaticFindObject('/Script/Engine.Default__KismetSystemLibrary'):GetFrameCount()
    end)
    return ok and type(value)=='number' and value or nil
end
RegisterKeyBind(Key.F8, guarded(function()
    ExecuteInGameThread(guarded(function()
        assert(not binding, "Already armed")
        local adapter=assert(loadfile("C:/TestInput/adapter.lua"))()
        local options=adapter()
        options.debug=false
        if BridgeBenchmarkInspectLayout then
            local function address(path)
                local obj=StaticFindObject(path)
                return obj and obj:IsValid() and obj:GetAddress() or 0
            end
            BridgeBenchmarkInspectLayout(options.component_path,
                address('/Script/Engine.InputComponent'),
                address('/Script/EnhancedInput.EnhancedInputComponent'),
                address('/Script/EnhancedInput.InputActionInstance'))
        end
        local function measured(event)
            if not running then return end
            local before=seconds()
            local cyclesBefore,ticksBefore,threadBefore=cpuSnapshot()
            assert(cyclesBefore,ticksBefore)
            workload.count=workload.count+1
            workload.sum=workload.sum+event.value.x
            workload.last={sequence=event.sequence,value=event.value.x}
            local cyclesAfter,ticksAfter,threadAfter=cpuSnapshot()
            assert(cyclesAfter,ticksAfter)
            assert(threadBefore==threadAfter,"Callback migrated OS thread; CPU sample invalid")
            local cycles,ticks=cyclesAfter-cyclesBefore,ticksAfter-ticksBefore
            assert(cycles>=0 and ticks>=0,"Non-monotonic thread CPU accounting")
            local elapsed=(seconds()-before)*1000000
            cpuSamples[#cpuSamples+1]={cycles=cycles,ticks=ticks}
            samples[#samples+1]=elapsed
        end
        local function dispatch(event,keyName)
            if not running then return end
            measured(event)
            perKey[keyName]=(perKey[keyName] or 0)+1
            -- The callback workload has returned, including sample bookkeeping.
            -- Timestamp completion before publishing the acknowledgement.
            local allKeysComplete=true
            if fountain and #samples==targetCount then
                for _,key in ipairs(inputKeys) do
                    if perKey[key.name]~=30 then allKeysComplete=false end
                end
            end
            if fountain and #samples==targetCount and allKeysComplete and not completedAt then
                completedCpu=assert(processCpu())
                completedAt=seconds()
                write("fountain-complete.txt",string.format("%.9f",completedAt))
            end
        end
        if baseline then
            local down=assert(BridgeBenchmarkKeyDown,"Baseline key-state helper unavailable")
            local sequence, pressedAt=0,{}
            LoopAsync(10,guarded(function()
                if not running then
                    if next(pressedAt) then pressedAt={} end
                    return false
                end
                local now=seconds()
                for _,key in ipairs(inputKeys) do
                    if down(key.vk) then
                        if not pressedAt[key.vk] then pressedAt[key.vk]=now end
                    elseif pressedAt[key.vk] then
                        local duration=now-pressedAt[key.vk]
                        pressedAt[key.vk]=nil
                        if duration<=tapThresholdSeconds then
                            sequence=sequence+1
                            dispatch({sequence=sequence,value={x=1}},key.name)
                        end
                    end
                end
                return false
            end))
            binding=true
        else
            input=assert(bridge.Helpers.OpenInput(options))
            binding={}
            for _,key in ipairs(inputKeys) do
                local keyName=key.name
                binding[#binding+1]=assert(input:Bind(keyName, bridge.Helpers.Trigger.Tap,
                    guarded(function(event) dispatch(event,keyName) end),
                    {threshold_seconds=tapThresholdSeconds, consume_input=false}))
            end
        end
        local f=assert(io.open(csv,"w"))
        f:write("schedule,callback_mode,target_presses_per_second,received_callbacks,stage_seconds,callback_total_ms,callback_mean_us,callback_p95_us,callback_p99_us,callback_max_us,timer_pair_floor_us\n")
        f:close()
        if input and input.InspectBindings then write("binding-snapshot.txt", assert(input:InspectBindings())) end
        write("armed.txt",(fountain and "A-T pool (20 keys)" or "F9")..
            (baseline and "; Lua Tap; threshold250ms; poll10ms; bridge disabled" or "; EI Tap; threshold250ms"))
    end))
end))
RegisterKeyBind(Key.F7, guarded(function() ExecuteInGameThread(guarded(function()
    assert(binding and not running and stage<(fountain and 1 or 10),"Invalid stage transition")
    samples={}; cpuSamples={}; perKey={}; completedAt=nil; completedCpu=nil
    stage=stage+1; firstFrame=frameCount(); started=seconds(); running=true
    write("begin-"..stage..".txt",fountain and "600 taps; 20 cycles of 4,5,6,7,8 presses/sec; A-T; independent 100ms holds" or tostring(stage*10))
end)) end))
RegisterKeyBind(Key.F10, guarded(function() ExecuteInGameThread(guarded(function()
    assert(running,"No running stage")
    local finishedCpu=completedCpu or assert(processCpu())
    write("process-cpu-end.txt",string.format("%d",finishedCpu))
    write("completion-status.txt",(completedAt and "complete" or "incomplete").." callbacks="..#samples)
    running=false
    local keyFile=assert(io.open(out.."/key-counts-"..stage..".csv","w"))
    keyFile:write("key,received_callbacks\n")
    for _,key in ipairs(inputKeys) do keyFile:write(key.name..","..(perKey[key.name] or 0).."\n") end
    keyFile:close()
    local elapsed=(completedAt or seconds())-started
    local lastFrame=frameCount()
    if firstFrame and lastFrame then write("frames-"..stage..".txt", string.format("frames=%d seconds=%.6f fps=%.3f",lastFrame-firstFrame,elapsed,(lastFrame-firstFrame)/elapsed)) end
    local floor=math.huge
    for _=1,100 do local a=seconds(); floor=math.min(floor,(seconds()-a)*1000000) end
    -- Report accounting quantization and counter-call cost without subtracting it.
    local cycleFloor,tickFloor=math.huge,math.huge
    for _=1,100 do
        local c,t,thread=cpuSnapshot();assert(c,t)
        local c2,t2,thread2=cpuSnapshot();assert(c2,t2)
        assert(thread==thread2,"CPU calibration changed thread")
        cycleFloor=math.min(cycleFloor,c2-c);tickFloor=math.min(tickFloor,t2-t)
    end
    local totalCycles,totalTicks,zeroTicks=0,0,0
    for _,sample in ipairs(cpuSamples) do
        totalCycles=totalCycles+sample.cycles;totalTicks=totalTicks+sample.ticks
        if sample.ticks==0 then zeroTicks=zeroTicks+1 end
    end
    local cf=assert(io.open(out.."/callback-cpu-"..stage..".csv","w"))
    cf:write("mode,callbacks,cpu_cycles_total,cpu_cycles_mean,cpu_time_ms,zero_cpu_time_samples,counter_pair_min_cycles,counter_pair_min_cpu_ticks_100ns\n")
    cf:write(string.format("%s,%d,%d,%.6f,%.6f,%d,%d,%d\n",baseline and "Baseline" or "Bridge",#cpuSamples,totalCycles,
        #cpuSamples>0 and totalCycles/#cpuSamples or 0,totalTicks/10000,zeroTicks,cycleFloor,tickFloor))
    cf:close()
    local total=0
    for _,v in ipairs(samples) do total=total+v end
    table.sort(samples)
    local function pct(p) return samples[math.max(1,math.ceil(#samples*p))] or 0 end
    local f=assert(io.open(csv,"a"))
    f:write(string.format("%s,%s,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
        fountain and "Fountain" or "Sweep",baseline and "Baseline" or "Bridge",fountain and 0 or stage*10,#samples,elapsed,total/1000,#samples>0 and total/#samples or 0,pct(.95),pct(.99),pct(1),floor))
    f:close()
    write("end-"..stage..".txt",tostring(#samples))
end)) end))
write("ready.txt","Lua consumer and native clock loaded")
