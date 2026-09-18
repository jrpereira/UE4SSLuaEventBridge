-- Deterministic control-flow test. This is NOT a performance or Unreal run.
local fountain=arg[1]=="Fountain"
local baseline=arg[2]=="Baseline"
local incomplete=arg[3]=="Incomplete"
assert(not incomplete or fountain)
local real_getenv=os.getenv
os.getenv=function(name)
    if name=="BRIDGE_BENCH_SCHEDULE" then return fountain and "Fountain" or "Sweep" end
    if name=="BRIDGE_BENCH_MODE" then return baseline and "Baseline" or "Bridge" end
    return real_getenv(name)
end
local keys, callbacks = {}, {}
local files = {}
local real_open=io.open
io.open=function(path, mode)
    if mode=="w" then files[path]="" end
    return {write=function(_, text) files[path]=(files[path] or "")..text end, close=function() end}
end
local original_loadfile=loadfile
loadfile=function(path)
    if path=="C:/TestInput/adapter.lua" then
        return function() return function() return {component_path="test",subsystem_path="test"} end end
    end
    return original_loadfile(path)
end
Key={F7=7,F8=8,F9=9,F10=10}
function RegisterKeyBind(k,fn) keys[k]=fn end
function ExecuteInGameThread(fn) fn() end
local poll,downState=nil,{}
local scanned={}
function LoopAsync(interval,fn) assert(interval==10);poll=fn end
function BridgeBenchmarkKeyDown(vk) scanned[vk]=true;return downState[vk]==true end
local time=0
local fakeCycles=0
function BridgeBenchmarkThreadCpu() fakeCycles=fakeCycles+100;return fakeCycles,0,123 end
local lastProcessCpu
function BridgeBenchmarkProcessCpu() lastProcessCpu=math.floor(time*10000000);return lastProcessCpu end
function BridgeBenchmarkSeconds() time=time+0.000001; return time end
UE4SSLuaEventBridge={Helpers={Trigger={Tap="tap"},OpenInput=function(options)
    assert(options.debug==false)
    return {Bind=function(_,key,trigger,fn,options)
        assert((fountain and #key==1 and key>='A' and key<='T' or not fountain and key=='F9')
            and trigger=="tap" and options.threshold_seconds==0.250)
        assert(not callbacks[key]);callbacks[key]=fn;return key
    end}
end}}
if baseline then UE4SSLuaEventBridge=nil end
original_loadfile('benchmarks/runner/benchmark.lua')()
keys[8]()
local stages=fountain and 1 or 10
for stage=1,stages do
    local count=fountain and (incomplete and 599 or 600) or stage*10
    keys[7]()
    if baseline then
        downState={};poll() -- release alone is not a Tap
        for vk=(fountain and 0x41 or 0x78),(fountain and 0x54 or 0x78) do
            assert(scanned[vk], 'A watched key was not polled')
        end
        assert(files['C:/TestResults/fountain-complete.txt']==nil or not fountain)
        downState[fountain and 0x41 or 0x78]=true;poll();time=time+0.251
        downState={};poll() -- long hold rejected
    end
    local first=1
    if fountain then
        if baseline then
            -- Exercise independent held states even though evenly spaced100ms
            -- schedule inputs normally do not overlap at4-8 presses/sec.
            downState[0x41]=true;poll();time=time+0.030
            downState[0x42]=true;poll();time=time+0.070
            downState[0x41]=false;poll();time=time+0.030
            downState[0x42]=false;poll();poll()
        else
            callbacks.A({sequence=1,value={x=1}})
            callbacks.B({sequence=2,value={x=1}})
        end
        first=3
    end
    for seq=first,count do
        local vk=fountain and (0x41+(seq-1)%20) or 0x78
        local keyName=fountain and string.char(vk) or 'F9'
        if baseline then
            downState[vk]=true;poll();time=time+0.100
            downState[vk]=false;poll();poll() -- exactly one callback, no repeat on released state
        else assert(callbacks[keyName])({sequence=seq,value={x=1}}) end
        if fountain and seq<600 then assert(files['C:/TestResults/fountain-complete.txt']==nil) end
    end
    local cpuAtCompletion
    if fountain and not incomplete then
        assert(tonumber(files['C:/TestResults/fountain-complete.txt']))
        local timestamp=files['C:/TestResults/fountain-complete.txt']
        cpuAtCompletion=lastProcessCpu
        time=time+5 -- Sender observes completion later: must retain callback timestamp.
        assert(files['C:/TestResults/fountain-complete.txt']==timestamp)
    end
    keys[10]()
    assert(files['C:/TestResults/end-'..stage..'.txt']==tostring(count))
    local exportedCpu=assert(tonumber(files['C:/TestResults/process-cpu-end.txt']))
    if fountain and not incomplete then assert(exportedCpu==cpuAtCompletion, 'Observer delay entered CPU window') end
    if incomplete then
        assert(files['C:/TestResults/fountain-complete.txt']==nil)
        assert(files['C:/TestResults/completion-status.txt']=='incomplete callbacks=599')
    end
    if fountain then
        local counts=assert(files['C:/TestResults/key-counts-1.csv'])
        for vk=0x41,0x54 do
            local expected=incomplete and vk==0x54 and 29 or 30
            assert(counts:find(string.char(vk)..','..expected..'\n',1,true))
        end
    end
    local cpu=assert(files['C:/TestResults/callback-cpu-'..stage..'.csv'])
    assert(cpu:find(','..count..','..(count*100)..',100.000000,0.000000,'..count..',100,0',1,true))
end
assert(files['C:/TestResults/failure.txt']==nil)
local rows=0
for _ in files['C:/TestResults/callbacks.csv']:gmatch('\n') do rows=rows+1 end
assert(rows==stages+1)
local ok=pcall(keys[7]);assert(not ok and files['C:/TestResults/failure.txt'])
io.open=real_open
os.getenv=real_getenv
print('PASS: '..(fountain and (incomplete and '599-callback incomplete cycle test' or '600-event/20-key cycle test') or '550-event sweep')..' '..(baseline and 'baseline without bridge' or 'bridge')..'; independent keys, per-key delivery, CPU completion boundary, CSV; no timing claim')
