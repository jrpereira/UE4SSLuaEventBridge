# Separate Windows Sandbox keypress benchmark

Experimental integration test, excluded from the production mod package. It adds

no measurement hooks or timers to normal gameplay. Windows 11 with Sandbox and an

interactive desktop is required. Run artifacts are local experiment records;
they are excluded from the public repository and production package.

The current independent-key schedule completed four bridge runs and three baseline
runs in Sandbox, all with 600/600 callbacks. One baseline attempt was interrupted
by a runner status-file race, now covered by a regression test. The aggregate
historical sample below describes that earlier drain-all implementation.

## Event fountain comparison (current default)

`-Schedule Fountain -CallbackMode Bridge` sends **600 real taps across 20 keys (A-T)**.
Both modes watch all 20 keys throughout the run. Repeat this five-second pattern
20 times, for **100 seconds of requested pacing**:

| Second in cycle | Presses | Requested spacing |
|---|---:|---:|
| 1 | 4 | 250 ms |
| 2 | 5 | 200 ms |
| 3 | 6 | 166.667 ms |
| 4 | 7 | 142.857 ms |
| 5 | 8 | 125 ms |

Keys rotate through A-T continuously across cycle boundaries: 30 presses per key,
with different keys within each second. Each key-up has its own **100 ms hold**
deadline. The sender waits for the next key-down or key-up, whichever is due first;
a key-down does not require every other key to be released. With evenly spaced
4-8/sec starts and 100 ms holds, the ideal schedule does not actually overlap.
No extra chords or longer holds are inferred. Control keys F7/F8/F10 are outside
the pool. A 25 ms initial offset places the last requested release at 100 seconds.

Tap callbacks occur on release. Each nominal second has the prescribed number of
presses; scheduling/SendInput overhead extends wall time rather than causing
catch-up bursts. The last requested release is at 100 seconds. Traces include
cycle, second, key, and the measured sender hold lower bound. The experiment tests
whether either implementation is faster or less lossy; neither outcome is assumed.

Run the same command with `-CallbackMode Baseline` for Lua Tap detection (10 ms polling); the bridge mod is disabled in mods.txt for that run. Use the same sample,

render settings and schedule, and alternate/repeat both modes. Both consumers time

the same Lua workload. Baseline event normalization occurs outside that timed block.

Lua Tap detection and Enhanced Input Tap use different input sampling paths, so

this is an end-to-end path comparison, not an isolated native dispatch microbenchmark.

`fountain-timing.csv` records event number, requested interval, cumulative minimum

seconds, elapsed seconds at key release and excess elapsed time (`elapsed - minimum`).

CSV columns are `minimum_seconds`, `elapsed_seconds`, and `excess_elapsed_seconds`.

`sender-results.csv` summarizes those totals and received callbacks. The sender waits up to 30 seconds after its loop for callback 600 to publish

completion. Missing callbacks make the run incomplete, with no total elapsed metric.

Both sender and receiver use the guest QueryPerformanceCounter clock. Total elapsed

time ends at the later of sender-loop completion and callback-600 completion; it

excludes marker observation delay, CSV writing and the F10 reporting handshake.

The marker is written after callback workload processing. Repeated taps within each key carry no

unique sender ID: completion assumes one callback per tap. Loss or duplication must

be investigated rather than interpreting callback count as verified event identity. `callbacks.csv` records Lua workload timing, and frames-1.txt records average

frame rate. Callback execution time excludes dispatch/queue overhead; fountain drift

measures sender pacing, **not bridge latency**. Neither alone proves bridge overhead.

A fountain row uses target_presses_per_second=0 because its rate varies; consult the

schedule and timing trace. Legacy `-Schedule Sweep` retains the ten fixed-rate stages.

Keep `AllowVGPU=0` on this host: the user confirmed it fixes Sandbox startup crashes.

Pass `-DisableVGpu` explicitly. Software rendering is suitable for matching relative

runs, with rendering load and timing variability reported. The historical sample covers
the earlier drain-all bridge. The new budgeted dispatch implementation still needs
its own isolated integration run; offline tests do not extend that sample's evidence.

## Usage from the repository

Build the bridge with `dev-build.ps1`, then build the test-only clock:

```powershell
./benchmarks/runner/build-clock.ps1
./benchmarks/runner/build-viewer.ps1
./benchmarks/runner/run-sandbox-test.ps1 -Smoke
```

After the smoke test succeeds, supply a complete packaged Windows x64 application

as a direct HTTPS ZIP URL (not an itch landing page), pinned UE4SS distribution and

relative executable path:

```powershell
./benchmarks/runner/run-sandbox-test.ps1 `
  -ApplicationUrl 'https://YOUR-SOURCE/application.zip' `
  -ApplicationSha256 'EXPECTED-APPLICATION-SHA256' `
  -UE4SSArchive 'C:/downloads/UE4SS.zip' `
  -UE4SSSha256 'EXPECTED-UE4SS-SHA256' `
  -BridgeDll './build/sandbox-layout128/dist/UE4SSLuaEventBridge/dlls/main.dll' `
  -Executable 'Sample/Binaries/Win64/Sample-Win64-Shipping.exe' `
  -SecondsPerStage 3 -PassesPerSecond 50 -DisableVGpu
```

`-ApplicationArchive` reuses a local archive only when `-ApplicationSha256` is supplied; the source URL is still recorded. `-DisableVGpu` selects software rendering; use it on this host to retain the working Sandbox configuration.

`-PrepareOnly` builds inputs and the .wsb file without starting Sandbox.

`-Adapter path/to/adapter.lua` overrides one-time input resolution. The default

adapter requires one local-player subsystem and a possessed pawn/controller with

Enhanced Input already active. Menus, missing VC runtime, multiple local players,

unsupported engine versions and absent input components are setup problems.

The current bridge targets UE 5.5 and UE4SS commit 97b7e501; arbitrary UE binaries

must not be presumed ABI-compatible. This runner cannot certify that compatibility.

Downloads are capped at 1 GiB/10 minutes. Before Sandbox launches, the application ZIP is extracted into input/Application and the staged ZIP is deleted. The original cached download, if supplied, is retained. Extraction is capped at 4 GiB/50,000 entries with traversal/link checks. Before launch, UE4SS, the bridge, benchmark clock and Lua mod are installed into the extracted application. When -VCRuntimeDirectory is supplied, its DLLs are placed beside the real executable and their hashes recorded. Staged application and UE4SS ZIPs are removed from mapped inputs. The guest copies this prepared application directory to writable C:\Work\Application, sets the test environment and launches the real executable with its recorded arguments. Bundled prerequisite installers, when needed instead of app-local runtime DLLs, still run only inside the guest. A recorded hash

identifies bytes, not trust. Suggested links are unverified examples, never

endorsements. No downloaded executable is launched on the host.

## Execution and results

Each run gets `build/sandbox-tests/<run-id>/input`, `results`, and `test.wsb`.

Only input (read-only) and results (writable) are mapped. Networking and clipboard

are disabled. vGPU is enabled; 8 GiB RAM defaults can be changed with `-MemoryMB`.

UE4SS must have the `dwmapi.dll` plus `ue4ss/UE4SS.dll` distribution layout.

Bundled enabled.txt markers are disabled and mods.txt lists just the three test mods.

If the archive contains exactly one UEPrereqSetup_x64.exe, the guest runs it with /install /quiet /norestart and a 180-second timeout. Its exit code must be 0 or 3010. Nothing is installed on the host.

The guest launches the actual Binaries/Win64 executable. Generic UnrealGame executables may need a project argument; preserve the project argument from the sample bootstrap. The sender waits for Lua

readiness, sends F8 to bind F9 Tap through the bridge, and runs ten F7/F10 controlled

stages from 10 to 100 presses/sec. It checks foreground ownership before input,

uses separate down/up events, and never sends catch-up bursts. Keys cannot be

assumed to survive engine frame coalescing at high rates. Actual achieved sender

rate and callback count are both reported; mismatches fail the run.

Inspect `results/result.json`, `guest.log`, `UE4SS.log`, `sender-results.csv`,

`callbacks.csv`, and handshake/failure files. A launcher return is not a passing

run. No result file means guest startup was not established. The launcher is

asynchronous; there is currently no automatic host-side startup watchdog. Use

`wsb list --raw` and `wsb stop --id <test-instance-id>` to stop a failed instance.

Do not stop an unrelated sandbox. Guest execution shuts down after completion.

Callback timings bracket a small Lua state update and table allocation using a

separate native steady clock. They include clock boundary overhead and exclude

bridge dispatcher, helper wrapper, queue handling, sample recording and GC that

occurs outside that bracket. Timer-pair floor is reported separately, not

subtracted. This is not total bridge CPU time or a gameplay FPS benchmark.

Sandbox virtualization and sender CPU load affect results. Native gameplay

performance requires separate measurement. Trace is disabled.

## Polling and waits introduced by this test

- External sender readiness/handshake files: every 200 ms, bounded to 120 seconds

  at startup and 30 seconds thereafter; never runs in the gameplay Lua callback.

- Key pacing: compiled external sender uses a high-resolution waitable timer and short SpinWait near deadlines. This uses

  CPU and can perturb measurements; actual send rates are recorded.

- 100 ms control-key holds, 500 ms queue drain and 500 ms stage separation.

  A stalled engine can exceed the drain interval; callback loss is a failure.

- Adapter object discovery: once at F8, no repeat scan or retry timer.

- Production bridge still gates queue passes at default 20/sec. The test has no

  Lua tick/poll registration and does not change production defaults.

## Viewer isolation and cleanup

Run `./benchmarks/runner/build-viewer.ps1` before launching tests. The runner now

starts Sandbox through the CLI and launches its viewer on a separate host Windows

desktop using CreateDesktopW/CreateProcessW. It never calls SwitchDesktop. This

allows an interactive guest without placing the viewer on the user's desktop.

Actual viewer HWNDs were enumerated on the private desktop, and one such session

loaded UE4SS plus the benchmark Lua consumer. Later runs hit guest DWM crashes.

This is not headless guest execution: real input still needs a working guest login.

System-only execution was tested and provides no interactive user session.

A viewer log records window identities and rectangles. Oversized viewer windows

are bounded to1024x768 without activation. Observation occurs every second on the

host, not in gameplay. The launcher stops its exact Sandbox ID after900seconds or

when its per-run viewer-stop.txt file exists, then terminates only remote-viewer

processes owning windows on its private desktop. Cleanup was verified on a failed

probe. Startup and SendInput reliability remain blocked as described in the report.

## Local validation without an Unreal application

```powershell
./benchmarks/runner/send-sweep.ps1 -ValidateOnly
./build/tools/lua-5.4.8/src/lua.exe benchmarks/runner/test-consumer.lua
```

These validate input structure and mocked control flow only; no real keys or UE

callbacks. The separate `benchmarks/run-flow-benchmark.ps1` is a synthetic queue/

Lua benchmark and must be labelled accordingly. The custom Unreal fixture under

`benchmarks/unreal` requires an engine installation to package.

The third-party UEBridge / UE4SS Bridge � Live Lua MCP is excluded. It is not a dependency of this test, production module, packaging, installation, CI or releases.

`-VCRuntimeDirectory` optionally stages Microsoft x64 app-local CRT DLLs beside the guest executable, recording each SHA256, and skips the bundled installer. Use an appropriate Microsoft redistributable directory. This does not provide every possible application prerequisite.

## Experimental sample compatibility

Frog Fu required `-unattended -AllowSoftwareRendering` in the current Sandbox.

The sample uses a different component layout from the default bridge target.

The opt-in CMake option `UE4SSLEB_COMPONENT_LAYOUT_128=ON` builds a separate

experimental candidate (observed base/component sizes 0x128/0x160); it is OFF by

default and must not be treated as general engine compatibility detection.

The optional `-Adapter benchmarks/runner/frogfu-adapter.lua` routes input to the

game viewport. The generic adapter initializes the component weak identity

through Unreal before native validation. A one-shot read-only native layout probe

and binding snapshot run before measurement; neither runs per gameplay frame.

The legacy high-rate sweep is separate from the current cyclic experiment.
An attempted rate is not an achieved rate: validate sender timestamps and complete
delivery before interpreting any run. Software rendering and sparse samples limit
performance conclusions.

## Measured pacing and packaging

The sender records an input-free calibration comparing waitable-timer and

busy-wait pacing, then selects the faster measured method for that run.

Busy-waiting consumes CPU in the external sender and can affect the guest load;

`pacing-mode.txt` and `pacing-calibration.csv` record this condition. A run passes

only if all callbacks arrive and achieved press rates stay within 5% of each

requested rate. `frames-N.txt` reports stage-boundary frame-count deltas without

installing a frame hook or gameplay polling.

The manifest records requested vGPU, RAM and the host AllowVGPU policy. A disabled

host policy means the requested GPU setting cannot provide hardware rendering.

The runner reports this mismatch and never changes host policy itself.

Run `python benchmarks/runner/package-tests.py --output build/test-bundles/my-run.zip` from the repository to create the

separate test ZIP at a new output path. Existing archives/checksums are not overwritten.
It uses an explicit source/artifact allowlist, preserves repository-relative paths and verifies file hashes

and ZIP CRCs. It includes the explicitly experimental layout128 candidate under

`build/sandbox-layout128`, plus our clock/viewer binaries. It excludes downloaded

applications, UE4SS runtime archives, Microsoft CRT, third-party UEBridge, raw
session records, investigation notes and personal configuration. The optional
standalone C++ flow benchmark also requires separately supplied Lua 5.4 sources.

Supply these inputs separately when running the test; suggested sources remain

unendorsed. Validate delivery and achieved rates for each run before reporting results.

## Tap baseline update

Baseline now recognizes Tap in Lua with a 0.250-second release threshold and 10 ms

LoopAsync polling of the entire 20-key A-T pool. A release without a prior press and

a hold exceeding the threshold produce no Tap. The bridge still uses Unreal

InputTriggerTap with the same threshold. Polling/timer scheduling and recognition

work remain part of each implementation's cost. This is matching gesture semantics,

not the same underlying engine trigger: OS state and Unreal frame sampling differ.

The raw state helper is in the separate test clock DLL, not a production dependency.

Existing paired results describe the OLD key-down baseline; do not reuse them as

Tap-versus-Tap results. New baseline code has mock validation pending a real rerun.

## Current benchmark configuration and data flow

Both Tap implementations use a 250 ms release threshold. The runner defaults to
50 bridge queue passes/sec via UE4SSLEB_QUEUE_CHECKS_PER_SECOND. Baseline polling
is 10 ms. The installed production RC retains its 20/sec default.
Both paths use the exact same measured Lua workload and completion bookkeeping.
Event recognition and construction differ before that workload.

```text
             WITH BRIDGE                         WITHOUT BRIDGE
             -----------                         --------------
        Fountain sends press                  Fountain sends press
             and release                           and release
                  |                                     |
                  v                                     v
        Unreal processes input               Windows updates key state
          on a game frame                               |
                  |                                     v
                  v                            UE4SS runs Lua detector
        EI detects press -> release             every ~10 ms requested
          within T, emits Tap                           |
                  |                                     v
                  v                            Native test helper reads
        Native bridge delegate                  A-T via GetAsyncKeyState
          receives Tap event                            |
                  |                                     v
                  v                            Lua detects press -> release
        Event enters bridge queue               within T, run callback
                  |                                     |
                  v                                     |
        Next bridge dispatch pass                       |
          50/sec (~20 ms interval)                      |
                  |                                     |
                  v                                     |
        Pending batch drained                           |
          into Lua callbacks                            |
                  |                                     |
                  v                                     v
        Lua callback workload                  Lua callback workload
             completes                              completes
                  |                                     |
                  v                                     v
        Callback 600 records                    Callback 600 records
          completion timestamp                   completion timestamp
                  |                                     |
                  v                                     v
        Total elapsed ends once                Total elapsed ends once
        BOTH sender and callback               BOTH sender and callback
             have finished                          have finished
                         T = 250 ms
```

EI means Unreal Enhanced Input. Baseline polling reads all 20 keys each pass and
can detect at most one Tap per key per pass; it does not drain an event queue.
The diagram and recorded sample used drain-all bridge dispatch. Since 0.3.4-rc.2,
bridge dispatch retains FIFO order across passes with a default budget of 256
events or 2 ms; remaining events wait for another pass. A callback already running
is allowed to finish. See [queue dispatch limits](../../docs/QUEUE_DISPATCH_RATE.md).
The 10 ms polling interval is requested, not guaranteed scheduling precision.
The sender requests a 100 ms hold for each selected key, giving the requested 10 ms
baseline poll several opportunities to sample it. Actual scheduling can still run
late. Completion requires 600 callbacks and exactly 30 per key; per-key counts are
exported even on timeout. This detects cross-key count mismatches but cannot prove
that a loss and duplicate on the same key did not cancel out.
The current cyclic experiment uses a 250 ms Tap threshold, bridge dispatch at
50/sec, and baseline polling at 10 ms. Earlier single-key experiments used
different schedules and settings; their results must not be pooled with this one.

## Callback CPU execution accounting

The test clock now exposes Windows GetThreadTimes(user+kernel) and QueryThreadCycleTime
snapshots for the current thread, with its thread ID. Each shared Lua workload is
bracketed with snapshots, checked for matching thread identity, and accumulated in
callback-cpu-N.csv. CPU time excludes descheduled/sleep time. CPU cycles are reported
as cycles, never converted to seconds. GetThreadTimes100ns units do not imply100ns
measurement resolution: zero deltas for short callbacks are explicitly counted and
must not be interpreted as zero CPU cost. Counter-pair minimum costs are reported
without speculative subtraction. These counters have observer overhead and can still
reflect cache/memory contention and VM accounting effects; they cannot remove every
host-load effect. QPC elapsed timing remains a separate responsiveness measurement.

Scope: the new CPU metrics cover the Lua workload bracket plus measurement-boundary
costs. They DO NOT measure native EI recognition, bridge queuing/dispatch before that
callback, or baseline polls that emit no callback. A complete bridge/no-bridge CPU
comparison uses the separate process accounting described below;
subtracting minimum sleep time from wall time does not provide that CPU comparison.
The existing callback wall-time metric now includes added accounting calls, so do not
compare it directly with earlier runs that lacked this instrumentation.

References: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes
https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime

Intentional sleep remains included in total elapsed time. Requested sleep total is
also reported as the minimum duration, not deducted from CPU accounting. Excess
elapsed time is descriptive, not pure processing time: it includes sleep overshoot,
scheduling and queue delay. CPU execution and total completion time answer different
questions and must be reported separately. No single clock both includes intentional
sleep and automatically removes all other scheduling/load delays.

## Disable world rendering while retaining keyboard input
Use `-Adapter benchmarks/runner/frogfu-no-world-render-adapter.lua` with the
normal windowed DX11 sample arguments (not NullRHI). The adapter calls
`GameplayStatics:SetEnableWorldRendering(controller, false)` once on the game
thread, checks `GetEnableWorldRendering(controller) == false`, and records
`world-rendering.txt`. It then applies the existing small-window settings and
focuses the viewport. Failure to disable rendering aborts arming.
This is a world-rendering switch, not proof of zero GPU/UI/render-thread work.
It adds no per-frame Lua hook or polling. This test adapter does not alter
production bridge code.

## Process execution, pacing, and processing accounting
`process-accounting.csv` reports:
- Execution: process user + kernel CPU delta across all Unreal process threads.
- Pacing: scheduled input timeline through final release (100 seconds).
- Processing: execution + pacing, a constructed accounting metric, not wall time.

The sender refreshes Windows Process.TotalProcessorTime immediately before calling
Fountain. The native test clock captures GetProcessTimes inside Unreal after the
600th callback workload and per-key bookkeeping finish, before publishing its completion
marker. Start observation precedes the sender's QPC start by call overhead; these
are two distinct clocks and not an atomic snapshot. Startup/arming/settling and
completion-file observation are outside the successful process CPU window.
Includes all UE routing, UE4SS, Lua, background UE work, and remaining graphics CPU.
Excludes the separate PowerShell sender, Sandbox services, GPU time, and other
processes. No subtraction of rendering CPU is attempted. World rendering remains
disabled in the special adapter. CPU aggregates cores and can exceed wall time.
It excludes time when threads do not execute, but does not remove cache/contention
or VM effects. Pacing includes the requested delay budget, not sleep overshoot.

On completion timeout, F10 captures a diagnostic CPU endpoint and exports the
actual callback count. Those figures are marked incomplete and include the drain
wait; they are not comparable with a successful run ending at callback 600.
Source: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes

## Repeated comparison with complete-run averages
The current 600-tap, 100 ms hold sample batch ran four attempts per mode:
bridge 4/4 complete, baseline 3/4 complete. One baseline attempt was interrupted
by a host status-file read race, now fixed for future batches. All complete runs
delivered 600 callbacks. This historical aggregate uses the earlier drain-all
bridge; raw session records are kept outside the public repository.

Sample results (four attempts per mode, 600 taps per run, 100 ms holds):

| Average of complete runs | Bridge | No bridge |
|---|---:|---:|
| Completed | 4/4 | 3/4 |
| Execution CPU | 489.875 s | 493.286 s |
| Intentional pacing | 100.000 s | 100.000 s |
| Total | 589.875 s | 593.286 s |

Total is execution CPU plus intentional pacing. The interrupted baseline attempt
is excluded from averages. The difference is smaller than run-to-run variation;
this sample does not establish a speed advantage.

`run-comparison-batch.ps1 -AttemptsPerMode 10 -Configuration ./batch-config.json`
uses an explicit JSON object of runner parameters. Required fields are
`ApplicationUrl`, `ApplicationArchive`, `ApplicationSha256`, `UE4SSArchive`,
`UE4SSSha256`, `BridgeDll`, `Adapter` and `Executable`. Optional fields include
`ApplicationArguments`, `MemoryMB`, `DisableVGpu`, `PassesPerSecond` and
`VCRuntimeDirectory`; `BenchmarkClockDll` optionally selects a specific measurement
helper (otherwise `build/sandbox-clock/main.dll`). Use the same values for both modes. Paths are resolved from
the repository directory. Use `-StartupSettleSeconds` to override the default 30 s.
Start with [batch-config.example.json](batch-config.example.json), replacing its
placeholder URL, hashes and paths with your chosen compatible fixture. It does not
endorse a download source. Preflight rejects malformed fields and missing files
before staging or launching Sandbox; archive contents are hash-checked during staging.
`test-batch-configuration.ps1` exercises invalid configuration without Sandbox.
On Windows, `test-staging.ps1` runs the actual preparation path with inert ZIP
fixtures, verifies both modes and staged hashes, then removes its generated files.
It never launches the fixture executables or Windows Sandbox. Both tests run in CI.
The configuration can name any user-selected compatible fixture; it no longer
depends on an earlier run's private manifest. It runs exactly 10 attempts per mode,
alternating Bridge/Baseline in fresh private-desktop Sandboxes, one at a time.
It stages 30 s startup settling (outside measurement), uses the 600-event, 20-key
cycle schedule, and retains failed attempts without replacing them. It requires no other
Sandbox environment to be active. Each attempt has a240s orchestration deadline.
The batch writes attempts.json/current.json under build/sandbox-batches/<timestamp>-<id>.

After completion:
`python benchmarks/runner/summarize-comparison-batch.py <batch-directory> --output <report-directory>`

Only passed runs with 600 sent and completed callbacks (30 per key), complete CPU accounting,
and disabled-world-rendering readback enter averages. Complete-runs.csv preserves
all recorded numeric summary metrics; averages.csv provides sample size, mean,
sample standard deviation, minimum and maximum for each. All raw attempt outputs
are retained. No complete runs means N/A, never zero. Averages of per-run callback
percentiles are not pooled percentiles. CPU counter endpoint averages are merely
audit data; their deltas are the meaningful execution measurement.
