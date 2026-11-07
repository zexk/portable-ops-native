# Changelog

## Unreleased (release preparation)

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
- Confirmed a PO+ Windows development build under Wine; local candidates are
  being prepared for North American PO and PO+ on Linux and Windows.
- Licensed original port code under MIT and included its license in packages.
- Recorded candidate archive hashes, source/corpus fingerprints, dependency
  notices and loader checks separately from gameplay acceptance.
- Identified six unimplemented imports affecting date formatting, dialog
  cancellation, thread wakeups and access-point scanning. Only wakeup
  cancellation is confirmed reached in the existing Wine smoke; effects on
  gameplay remain unverified. See [import audit](docs/release-import-audit.md).

No release archives are approved. Native Windows and clean-machine setup,
full gameplay coverage and multiplayer acceptance remain pending. Hardware
requirements, public downloads, final distribution materials and release
version are not final. Original port code is MIT; game and third-party code
retain separate terms.
