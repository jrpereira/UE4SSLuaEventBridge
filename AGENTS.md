# Development and release rules

- Keep source, tests, maintained public documentation and reviewed defaults in Git.
  Keep personal settings, generated installers/archives, logs, data dumps, internal
  audits and investigation notes outside the public repository. Consult
  `docs/REPOSITORY-STANDARDS.md` and `repository-policy.json`.
- Run `python tools/bootstrap.py` in a new checkout. Validate staged files and
  introduced history with the shared hygiene tools. Never bypass a failed guard
  just to commit or publish an artifact.
- Use explicit release manifests. Keep generated builds and packages in ignored
  output directories. Publish tested packages and checksums as release assets.
- Before deployment, launch, live inspection or integration testing, compare the
  intended tested candidate with working and deployed versions and payload hashes.
  `tools/deployment_preflight.py` provides on-demand comparisons. When the diagnostic
  bridge is live, establish the loaded version using evidence tied to the current
  process/session. Installed files and old logs are not proof of loaded contents.
- Report mismatches and unverified loaded state. Offer correction (close if needed,
  deploy while preserving personal config, relaunch) or ignore for this launch.
  Honor correction already authorized by the user's deploy request; do not add
  redundant approval steps. Respect current computer-control authorization and
  game-running deployment guards. A restage or rebuild must pass relevant checks.
- Do not add gameplay polling or a runtime dependency on the optional diagnostic
  UEBridge for repository, release or version-checking tools.
- Keep native-game acceptance and performance results separate from offline tests.
  Reconstruction commits must identify actual evidence and must not fabricate
  historical tests, release claims or dates.
