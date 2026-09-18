# Repository standards

Commit source, tests, documentation, public schemas and reviewed default settings.
Builds, distributions, installers, personal configuration and diagnostic output
belong outside tracked source. Publish installation archives and checksums as
GitHub Release assets. Keep generated files in ignored `build/`, `dist/`,
`artifacts/` or `work/` directories; tests in `tests/`, tooling in `tools/`,
documentation in `docs/`, and CI in `.github/workflows/`.

These are public repositories. Keep raw logs, object/data dumps, crash captures,
internal audits, coordination reports, investigation notes and draft architecture
notes in COORDINATOR's separate local records area, outside all three repositories.
Never publish that records area as a release asset. Local storage is separate,
not encrypted or automatically backed up.

Public `docs/` should contain maintained user/developer guidance, API contracts,
build instructions and design explanations needed by users or contributors.
Review and sanitize an internal finding before turning it into public documentation.
Changelogs should summarize shipped behavior and honest validation limits without
embedding raw session evidence, personal paths or machine-specific details.
Do not blanket-ignore JSON/CSV/XML: reviewed synthetic test fixtures can be valid
source. Keep collected data in excluded `dumps/`, `logs/` or the external records
area, with narrowly reviewed exceptions for intentional fixtures.

Run `python tools/bootstrap.py` once per checkout to enable local staged-file
and outgoing-history guards. They use Git blobs, not unstaged working files.
Run `python tools/repository_hygiene.py --staged` before a commit and
`python tools/check_incoming_history.py` for a current-tree check outside CI.
Python and Git must be on PATH. Hooks are opt-in and bypassable; CI is a second
gate, not a means to prevent the initial upload of private material.

`repository-policy.json` lists narrowly reviewed path exceptions, such as clean
distribution defaults. Never add an exception for a personal config. The checker
also rejects private-key headers, including in excepted paths; it is not a
comprehensive secret scanner. Use GitHub secret push protection where available.

Require the repository-hygiene and project test checks before merging. Releases
must depend on the same checks, build the tested source and publish checksums.
Keep native in-game acceptance separate from offline tests. Neither mocked Lua
tests nor a successful DLL build proves live performance or engine safety.

History reconstruction must identify its evidence and use honest contemporary
commit dates. Do not fabricate release status, historical tests or chronology.
Preserve existing published lineage and record ambiguous snapshots explicitly.

Before local testing, compare the intended working candidate against immutable
stage and deployed payload hashes. When diagnostic UEBridge is available, also
establish the loaded version from evidence tied to the current game session.
Installed files do not prove what a running process loaded. Missing evidence is
unverified, and equal version labels do not prove equal contents. Offer correction
(close, deploy with config preservation, relaunch) or ignore for this launch.
Do this on demand; do not add gameplay polling or a production dependency on the
diagnostic helper.

Example preflight after extracting a tested package into an ignored staging area:

```sh
python tools/deployment_preflight.py --manifest stage/MODULE/manifest.json --staged stage/MODULE --deployed /path/to/Mods/MODULE --working . --definition release-manifest.json --working-version CURRENT_VERSION
```

Replace module, version and paths with current evidence. The source definition maps
clean defaults to their installed example paths. A native DLL must be checked against
its tested build output and build metadata; do not compare C++ source bytes to a DLL.
On Windows the CLI detects a running Dawnwalker process. Runtime evidence can be
supplied with `--runtime-evidence`; it must include the same PID and process-start
identity as the detected session. `--session` is for an explicitly established fresh
session identity or isolated tests. Neither option makes stale evidence authoritative.
Exit 2 means attention is required. The tool presents choices but performs no
deployment, restart or input. An ignore choice is for this launch, not a saved bypass.

The executable local workflow is `tools/release_session.py`. Use `check`, `deploy`
or `launch` with `--manifest`, `--staged`, `--deployed`, `--working` and `--records`.
Put records outside public source and payload folders. Lua candidates use the
manifest embedded in their tested package. The working release definition and
complete payload must match. Deployment refuses stale source or tampered stage,
backs up existing files, preserves personal settings and existing enablement,
and stops if the game is running. Close the game first and repeat deployment;
the tool never terminates an active game automatically.

For a local native candidate, run `python tools/native_candidate.py --working .
--output build/NEW-CANDIDATE` in an x64 MSVC developer shell. This creates a fresh
Release build, runs CTest and writes `candidate-manifest.json` beside its `dist/`
folder. Existing output directories are refused. Its input hashes include source,
headers, ABI definitions, CMake files and tests, including untracked additions.
Failed tests or changed inputs during compilation produce no candidate manifest.
This record establishes a tested local build, not in-game acceptance or a signed
supply-chain attestation. A release ZIP sidecar alone cannot certify a dirty local
build; use this local builder when comparing working source to a DLL.

Use `--log /path/to/UE4SS.log` for Lua loaded-version messages. The adapter requires
the log boot timestamp to follow the current process start within two minutes.
It explicitly labels this timestamp correlation and never treats it as proof of
loaded bytes. Missing or oversized logs leave runtime identity unverified.
`--bridge-dir /path/to/ue4ss/bridge` optionally makes one bounded protocol-3 query
to the diagnostic UEBridge for the native product version. Coordinate access to
that shared client channel. A cached response or changed process is rejected;
another queued request is never overwritten. No helper is needed in production.

Launch passes the executable and arguments after `--`. A discrepancy blocks
launch until corrected or explicitly overridden by `--ignore-once`; the override
is recorded for that invocation and never stored as a future preference. Example:

```sh
python tools/release_session.py launch --manifest stage/MODULE/manifest.json --staged stage/MODULE --deployed /path/to/Mods/MODULE --working . --records /private/session-records --ignore-once -- /path/to/Dawnwalker.exe
```

This command starts the game: obtain any required computer-control permission
before running it. Running processes are inspected rather than launched again.
Version-only evidence cannot establish exact loaded contents, so the warning
remains even when the version label matches. Native acceptance/performance tests
must still be recorded separately.
