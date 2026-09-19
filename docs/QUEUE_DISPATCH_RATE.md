# Queue dispatch rate

Default: 20 dispatch passes per second, with at least 50 ms between event-batch pass starts.
Override with the process environment variable UE4SSLEB_QUEUE_CHECKS_PER_SECOND before
starting the game. Accepted values are whole numbers from 1 through 1000; missing,
empty, malformed, zero, negative or out-of-range values use 20. Restart the game
after changing it. The value is read once when the bridge is constructed.

For example, in a PowerShell launcher that starts the game process:
    $env:UE4SSLEB_QUEUE_CHECKS_PER_SECOND = '40'
    # Start the game from this process so it inherits the variable.
An already-running Steam process may not inherit a newly set launcher variable.

UE4SS still invokes on_update at its own cadence. Each invocation reads a monotonic
clock and returns before touching either queue when the next pass is not due.
The bridge creates no timer thread, sleeps, or catch-up bursts.

Each eligible pass continues the oldest batch in order, subject to existing
inactive-session/subscription and callback-error handling. The default allowance
is 256 live callback attempts or 2,000 microseconds per pass. Canceled bindings
and stopped sessions consume time but do not consume the callback-count allowance.
A failed callback attempt still counts. The first-progress allowance applies to
one examined entry, so an all-canceled batch cannot bypass the time limit. Remaining events stay in the batch
for the next eligible pass; new batches never overtake them. There is no sleep
between events. The time allowance starts before batch acquisition and buffer
recycling. Dequeue tracing is performed only for each admitted event, inside the
same budget; acquiring a batch does not format traces for the entire batch.
The allowance is non-preemptive: preparation, lock waits, or one slow Lua callback
can exceed it. At least one event is processed per nonempty pass to avoid starvation.

Set `UE4SSLEB_MAX_EVENTS_PER_PASS` and `UE4SSLEB_MAX_DISPATCH_US` before startup
to integers from 0 through 1,000,000. Zero disables that limit; setting both to
zero restores historical drain-all behavior. Malformed values use the defaults.

`UE4SSLEB_MAX_QUEUED_EVENTS` defaults to 65,536 and accepts 0 through 1,000,000;
zero explicitly allows unbounded producer queue growth. A full producer queue
rejects newest events, preserves accepted-event ordering, and emits an error log
for the overflow episode even when debug tracing is off. This is an overload
failure, not lossless delivery. Avoid increasing the cap to hide slow callbacks.
The consumer may also retain one previously drained batch, so the producer limit
is not the total number of events held across both buffers.

`UE4SSLuaEventBridge.GetDispatchStats()` returns process-wide `queued` (producer
queue plus the retained consumer batch, excluding the event currently being
processed), `queue_high_water` (producer queue only), `rejected_events` and
`rejected_traces` counters. Canceled entries remain queued until discarded.
The producer limit and high-water mark still describe producer capacity; total
`queued` can exceed both while a previous batch is retained.
Counters last for the bridge lifetime. Read them on demand; no diagnostic polling
is added by the bridge. A rejection must be treated as an input delivery failure.

Trace buffering is capped at 1,024 lines. Each pass separately allows 32 lines or
250 microseconds of Lua logging work, while preserving buffered line order.
Excess diagnostics increment `rejected_traces`. Native debugger tracing remains
synchronous when enabled; keep debug tracing off for gameplay measurements.

The 50 ms interval is between pass starts, not a sleep after completion. Actual
delivery can take longer because of UE4SS scheduling and slow callbacks. A slow
pass does not trigger repeated catch-up passes. Sustained overload can still
increase event latency or cause explicit rejections. Fewer checks do not by themselves prove lower frame times. Test the new limits under the intended workload before deployment.

API 4 and event payloads unchanged; dispatch statistics are additive.
Deployment and in-game validation belong to COORDINATION.

Opted-in primitive targets can register delivery-fault notifications separately
from these counters (see DEVELOPER_API.md). A rejection permanently disables the
affected target and its retained backlog; a coalesced reset notification is not
stored in either bounded queue. It is serviced at the normal dispatch cadence,
not synchronously from Unreal input. This does not guarantee focus-loss detection.
