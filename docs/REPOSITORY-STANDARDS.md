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

Runtime inspection adapters, collected evidence, diagnostic fixtures and history
evidence manifests also belong in private tooling, outside public Git history.
Keep production runtime code, ordinary tests, release manifests and checksums.

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

Before deployment, verify staged package hashes against current source and installed
files. Close the game before replacing files and preserve personal settings and
enablement. The deployment tooling checks whether the game is running; it does
not inspect loaded modules, read game logs or query a diagnostic bridge.

Use `tools/deployment_preflight.py` for on-disk comparisons and
`tools/release_session.py` for guarded deployment or launch. Deployment backups
belong outside the public repository and payload directories. Package hashes
establish file integrity, not in-game behavior or performance.
