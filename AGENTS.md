# Development and release rules

Module owners retain implementation and regression responsibility. Coordinate
shared contracts, repository changes and overlapping live operations with the
project coordinator. Report user requests to the coordinator only when they
affect shared goals or work; do not relay unrelated interactions.

Machine-specific ownership and internal-record locations may be documented in an
external file named by the local Git setting `bdw.localInstructions`. Read that
file when configured. Keep it outside public source; direct user instructions
take precedence. Preserve exact archived version labels as evidence while using
clear public milestone names (QuickslotsForever omits the old `-native` suffix).

- Keep source, tests, maintained public documentation and reviewed defaults in Git.
  Keep personal settings, generated installers/archives, logs, data dumps, internal
  audits and investigation notes outside the public repository. Consult
  `docs/REPOSITORY-STANDARDS.md` and `repository-policy.json`.
- Run `python tools/bootstrap.py` in a new checkout. Validate staged files and
  introduced history with the shared hygiene tools. Never bypass a failed guard
  just to commit or publish an artifact.
- Use explicit release manifests. Keep generated builds and packages in ignored
  output directories. Publish tested packages and checksums as release assets.
- Before deployment, verify package hashes against source and installed files.
  Close the game before replacement; preserve personal settings and enablement.
  Keep deployment backups outside public source and payload directories.
- Do not add gameplay polling or a runtime dependency on the optional diagnostic
  UEBridge for repository, release or version-checking tools.
- Keep native-game acceptance and performance results separate from offline tests.
  Reconstruction commits must identify actual evidence and must not fabricate
  historical tests, release claims or dates.
