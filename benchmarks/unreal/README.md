# Actual-keypress Unreal fixture (source scaffold; not engine-built or run)

This is a game-agnostic end-to-end test fixture, not an existing commercial game.
Requires Unreal Engine 5.5 with Win64 support, the existing VS2022 C++ toolchain,
and the bridge-compatible UE4SS build (3.0.1 Beta #0, commit 97b7e501).
No engine installation was found in the standard launcher/registry locations.
No Unreal download, install, packaging, deployment or key injection has occurred.

## Build and prepare

1. Install UE5.5 through Epic Games Launcher. Open BridgeBench.uproject, generate
   project files and build the C++ project. EngineAssociation is 5.5.
2. Package a Win64 Shipping build with debug symbols. The config uses the engine
   Entry map and BenchGameMode; verify that BenchController is instantiated and
   its Enhanced Input component exists. If the engine map is unavailable for
   cooking, create/save an empty map and set GameDefaultMap and MapsToCook to it.
3. Install the pinned UE4SS distribution alongside the packaged executable
   using its installation guide, then install the diagnostic.6 bridge and
   Mods/BridgeFlowBenchmark. Do not copy unrelated commercial-game settings.
   UE4SS scanning compatibility with this stock build still needs verification.
4. Start windowed with VSync/frame limit off. Keep the test window focused.
   Default bridge dispatch is 20 passes/s. Set the startup environment override
   before launching when testing other dispatch rates.
5. Press F8 to arm once and confirm the ARMED log. F9 is mapped to a real Enhanced
   Input action; its Started phase is the bridge subscription. Press F7 to begin,
   tap F9, then wait for the queue to drain and press F10 to write a stage result.
6. For an automated full sweep, restart the fixture and run, in 64-bit PowerShell:
   ./send-keypress-sweep.ps1 -TargetProcessId <fixture PID> -SecondsPerStage 5
   The driver sends F8, then F7/F9/F10 for stages 10,20,...100 presses/s.
   Confirm the ARMED/BEGIN/END messages and ten CSV rows before accepting a run.
   The driver restricts injection to a BridgeBench process, aborts on focus loss,
   sends real Windows key down/up input, and records achieved send rates.

## Measurements and interpretation

BridgeFlowBenchmark.csv is written to the packaged process working directory.
The sender writes sender-results.csv to its working directory.
Compare sent_presses with received_callbacks per stage. A deficit is a failed
delivery test, not evidence of lower callback cost. Zero samples are invalid.
The 20-pass dispatch limit batches events; it does not cap delivery at 20 events/s.

Callback measurements surround the sample Lua callback body using the fixture's
FPlatformTime::Seconds function accessed through UE4SS. They INCLUDE clock-call/
reflection overhead and EXCLUDE native dispatch, helper table creation/xpcall
before entering the callback, and queue waiting. timer_pair_floor_us measures
the same empty timing bracket; it is reported separately, not subtracted as if
it were an exact correction. This is instrumented callback-body timing, not
a measurement of total bridge overhead or an uninstrumented performance claim.
Per-stage total/mean/p95/p99/max are reported. No per-event file/log writes occur.
Replace the clearly marked sample body to benchmark meaningful consumer work.
The sample does not stand in for ExtendedControls, ModMenuDecorator, or gameplay.
Stage duration includes start and drain guard intervals; sender duration is separate.

Windows input delivery and Unreal's frame-based action evaluation impose limits.
100 presses/s means 200 down/up transitions/s. Running at 60 FPS may coalesce
transitions; a 100/s test therefore cannot assume one Started event per press.
Even uncapped rendering does not guarantee success. Record actual engine FPS/
frame times and achieved send rate with each run, and reject loss or overlap.
The key driver deliberately avoids catch-up bursts when scheduling slips.
F7/F10 are benchmark control bindings; F9 callbacks exclusively use the bridge.
This fixture remains unvalidated until engine compilation and an actual sweep.

## Secondary standalone benchmark

../run-flow-benchmark.ps1 is a separate synthetic queue/Lua microbenchmark.
It uses the production Lua dispatcher and portable queue/schedule code but does
not test keys, Unreal, UE4SS registry wrappers, native subscriptions or rendering.
It is not a substitute for this fixture. Its native timing measures dispatcher/
callback execution, unlike the callback-body bracket used above.

References:
- https://docs.ue4ss.com/dev/guides/fixing-compatibility-problems-advanced.html
- https://docs.ue4ss.com/dev/installation-guide.html
- https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine?application_version=5.5
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput