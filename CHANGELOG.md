# Changelog

## Unreleased — release preparation, not a release announcement

- Prepared Linux x86-64 and Windows x64 packaging for North American PO and PO+.
- Reused first-run ISO setup; packages are intended to contain native game
  code and runtime dependencies, with no player-side compiler.
- Added module identity, dependency, architecture and package-content checks.
- Added package manifests, source-only CI, support and acceptance documentation.
- Removed the development shell's dependency on a private local workspace.
- Rewrote the online server in C (`mpo-server`), built with the game and
  packaged beside Portable Ops+ for Linux and Windows.

No release archives are approved. Windows execution, clean-machine setup,
full gameplay coverage and multiplayer acceptance remain pending. Hardware
requirements, public downloads, licensing and release version are not final.
