# Changelog

## 0.1.0-rc1 (2026-11-08, prerelease)

- Prepared Linux x86-64 and Windows x64 packaging for North American PO and PO+.
- Reused first-run ISO setup; packages are intended to contain native game
  code and runtime dependencies, with no player-side compiler.
- Added module identity, dependency, architecture and package-content checks.
- Added package manifests, source-only CI, support and acceptance documentation.
- Removed the development shell's dependency on a private local workspace.
- Rewrote the online server in C (`mpo-server`), built with the game and
  packaged beside Portable Ops+ for Linux and Windows.
- Reduced generated-code size and generation cost, and removed unnecessary
  host exports and framework-test builds in ports.
- Added minimal Windows FFmpeg cross-builds and independent module linking.
- Confirmed gameplay smoke tests for all four local candidate archives:
  PO and PO+ on Linux through `steam-run`, and Windows builds through Wine.
- Confirmed PO+ Windows ISO copy/import, restart without setup and save
  reload under Wine, using an initially empty test data directory.
- Licensed original port code under MIT and included its license in packages.
- Recorded candidate archive hashes, source/corpus fingerprints, dependency
  notices and loader checks separately from gameplay acceptance.
- Identified six unimplemented imports affecting date formatting, dialog
  cancellation, thread wakeups and access-point scanning. Only wakeup
  cancellation is confirmed reached in the existing Wine smoke; effects on
  gameplay remain unverified. See [import audit](docs/release-import-audit.md).

Preliminary prerelease. Native Windows and clean-machine setup, full
gameplay coverage and multiplayer acceptance remain pending. Hardware
requirements are not final. Original port code is MIT; game and third-party code
retain separate terms.
