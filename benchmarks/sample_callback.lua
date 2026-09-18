-- Sample workload only: no engine calls. Replace with a file returning a callback.
local count, sum, lastSequence = 0, 0, -1001
return function(event)
    assert(event.sequence > lastSequence, "out-of-order delivery")
    lastSequence = event.sequence
    count = count + 1
    sum = sum + event.value.x
    -- A small state update, similar in shape to a consumer's dispatch bookkeeping.
    _G.benchmark_state = { count = count, sum = sum, phase = event.phase }
end