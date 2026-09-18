-- Fixture-only consumer. No automatically running input or benchmark timers.
local bridge, controller, target, binding
local stage = 0
local running = false
local samples = {}
local started = 0
local key = Key
local csv = "BridgeFlowBenchmark.csv" -- relative to packaged process working directory
local function seconds() return controller:BenchmarkSeconds() end
local function percentile(values, fraction)
    if #values == 0 then return 0 end
    return values[math.max(1, math.ceil(#values * fraction))]
end

-- F8: bind the real Enhanced Input action. Call once, after the test map is loaded.
RegisterKeyBind(key.F8, function()
    ExecuteInGameThread(function()
        if binding then return end
        bridge = assert(UE4SSLuaEventBridge, "Bridge not loaded in benchmark Lua session")
        controller = FindFirstOf("BenchController")
        assert(controller and controller:IsValid(), "BenchController unavailable")
        target = assert(bridge.OpenInputComponent(controller.BenchmarkComponentPath:ToString()))
        binding = assert(bridge.BindAction(
            target, controller.BenchmarkActionPath:ToString(), "Started",
            function(event)
                if not running then return end
                local before = seconds()
                -- Replace only this workload to benchmark a different callback.
                local value = event.value.x
                _G.BridgeBenchmarkLastEvent = { sequence = event.sequence, value = value }
                local after = seconds()
                samples[#samples + 1] = (after - before) * 1000000
            end))
        stage = 0
        local file = assert(io.open(csv, "w"))
        file:write("target_presses_per_second,received_callbacks,stage_seconds,callback_total_ms,callback_mean_us,callback_p95_us,callback_p99_us,callback_max_us,timer_pair_floor_us\n")
        file:close()
        print("[BridgeFlowBenchmark] ARMED F9 Started; F7 begins stage, F10 ends stage")
    end)
end)

-- F7: begin next 10,20,...100 presses/second stage.
RegisterKeyBind(key.F7, function()
    assert(binding, "Press F8 to arm the benchmark first")
    assert(not running, "End current stage with F10 first")
    assert(stage < 10, "Sweep complete; restart fixture for another sweep")
    samples = {}
    stage = stage + 1
    started = seconds()
    running = true
    print("[BridgeFlowBenchmark] BEGIN target=" .. stage * 10)
end)

-- F10: run after sender stops and pending events have drained.
RegisterKeyBind(key.F10, function()
    if not running then return end
    running = false
    local elapsed = seconds() - started
    local floor = math.huge
    for _ = 1, 100 do
        local a = seconds()
        local b = seconds()
        floor = math.min(floor, (b - a) * 1000000)
    end
    local total = 0
    for _, value in ipairs(samples) do total = total + value end
    table.sort(samples)
    local file = assert(io.open(csv, "a"))
    file:write(string.format("%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
        stage * 10, #samples, elapsed, total / 1000,
        #samples > 0 and total / #samples or 0,
        percentile(samples, .95), percentile(samples, .99),
        percentile(samples, 1), floor))
    file:close()
    print("[BridgeFlowBenchmark] END target=" .. stage * 10 .. " received=" .. #samples)
end)