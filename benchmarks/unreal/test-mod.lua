-- Mock control flow only: not an Unreal or timing validation.
local keys, callback = {}, nil
Key = { F7=7, F8=8, F9=9, F10=10 }
function RegisterKeyBind(key, fn) keys[key] = fn end
function ExecuteInGameThread(fn) fn() end
local time = 0
local controller = {
    BenchmarkComponentPath = {ToString=function() return "Fixture.Component" end},
    BenchmarkActionPath = {ToString=function() return "Fixture.Action" end},
    IsValid = function() return true end,
    BenchmarkSeconds = function() time = time + 0.000001; return time end,
}
function FindFirstOf(name) assert(name=="BenchController"); return controller end
UE4SSLuaEventBridge = {
    OpenInputComponent = function(path) assert(path=="Fixture.Component"); return 1 end,
    BindAction = function(target, path, phase, fn)
        assert(target==1 and path=="Fixture.Action" and phase=="Started")
        callback=fn
        return 1
    end,
}
assert(loadfile(assert(arg[1])))()
keys[8]()
assert(callback)
for rate=10,100,10 do
    keys[7]()
    for i=1,rate do callback({sequence=i, value={x=1}}) end
    keys[10]()
end
local file=assert(io.open("BridgeFlowBenchmark.csv","r"))
local lines={}
for line in file:lines() do lines[#lines+1]=line end
file:close()
assert(#lines==11)
for row=2,11 do
    local rate,count=lines[row]:match("^(%d+),(%d+),")
    assert(tonumber(rate)==(row-1)*10 and rate==count)
end
print("Fixture Lua control flow passed; mock results only")