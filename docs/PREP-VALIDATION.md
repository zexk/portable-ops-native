# Preparation validation — 2026-10-31

No game build, gameplay run, publication or external message was performed
for this preparation pass. Nix instantiated the development-shell environment;
that did not compile the game or its modules.

Checks performed:

- `nix develop --command python3 --version`: passed after removing the private
  workspace input and its transitive lock entries. Existing nixpkgs pin retained.
- `python3 scripts/test-package-windows.py`: both edition selections, release
  configuration, missing modules, dependency closure/cycles, builder-only
  runtime rejection, and x64 format checks passed on synthetic fixtures.
- `python3 scripts/test-release-audit.py`: wrong module identity despite matching
  count, disguised PSP/disc data, path leakage across read boundaries, changed
  fingerprints, modified packaged files and manifest traversal checks passed.
- `bash scripts/test-package-config.sh`: both editions and invalid selection
  checked using temporary synthetic module directories; no game corpus needed.
- Python syntax checks, shell syntax checks, ShellCheck on new shell/config
  files and Git whitespace checks passed.
- `ctest --test-dir out/po/portablekit --output-on-failure --timeout 60`: all
  21 existing framework test binaries passed. These binaries predate this
  preparation; this is not evidence of a fresh build or Windows compatibility.

Not performed: public CI execution, source-to-binary provenance validation,
complete package creation, Windows process execution, clean-machine checks,
first-run GUI validation, gameplay, multiplayer or release signing.

The local legacy module discrepancy is recorded in RELEASE.md. No old module
binaries, game images, saves or user tracing changes were removed.
