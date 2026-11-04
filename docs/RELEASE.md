# First-release gate

Status: preparation only. No release builds or gameplay runs are authorized
by the current task. No artifact is ready to publish. Checkboxes below are
acceptance requirements, not claims that a feature works.

## Candidate scope

The existing packaging matrix is North American PO 1.02 (ULUS10202) and
PO+ 1.01 (ULUS10290), each on Linux x86-64 and Windows x64. EU/JP PO+,
macOS, Android, ARM and explicit Steam Deck certification are outside this
matrix. Multiplayer inclusion and the original project's license require
owner decisions; this document does not silently choose an offline release.

## Completed preparation

- Linux and Windows packaging entry points and player readmes.
- Source-only packaging checks and a public CI workflow with no game data.
- Portable Nix development environment, with the private workspace removed.
- Windows DLL dependency closure with an explicit OS-library allowlist.
- Exact module identity checks, rather than trusting module counts.
- Staged file checks for PSP data, disc images, source files, wrong ELF/PE
  architecture and source/home path leakage (UTF-8 and UTF-16).
- Package manifests with file hashes, source revisions, local-change status,
  and aggregate source/corpus hashes. No absolute source paths are recorded.
- A support template, candidate changelog and this acceptance checklist.

Manifests describe packaging-time inputs. They do not prove which source
produced a stale binary. Build every public candidate from a fresh dedicated
build directory at frozen source revisions, then package without editing
sources. Verify the manifest and archive checksums after extracting the final
archive with:

```sh
python3 scripts/release_audit.py --verify --stage /path/to/extracted/program-folder
```

This verifies recorded staged files and rejects extras in runtime directories;
verify the outer archive checksum too. If the path scan rejects embedded compiler paths, use compiler
file-prefix mappings and rebuild; do not disable the check for publication. Do not publish a dirty candidate.

## Known source-review findings

- Local development module directories contain one extra KCEJ_FS binary per
  edition, compared with restored sources. PO: `mod08B61500_KCEJ_FS_165FFB12D002A786`;
  PO+: `mod08AFED00_KCEJ_FS_B9E5210B94CF41F7`. Restored source lists contain 94
  and 130 modules. These extras are not proven necessary or obsolete; fresh
  builds plus gameplay coverage must settle that. Exact-set audit rejects
  accidentally shipping stale extras.
- Recent smoke logs contain short interpreted stage startup sequences before
  recompiled code takes over. Do not claim zero interpretation.
- Those logs report three unresolved imports in one PO stage and one in one
  PO+ stage. Identify their NIDs and establish whether execution reaches them.
- `host/main.cpp` hashes an installed executable but falls back to the
  interpreter when its hash is unknown. The initial missing-data check is
  not an integrity check. Release recovery for corrupted installed data
  remains open; changing framework behavior needs compiled tests.
- The experimental server (`mpo-server`, shipped beside PO+) stores account
  passwords in plaintext in `accounts.txt` (the login proof is keyed by the
  plain password), and its protocol implementation is incomplete. Do not
  present it as a production-ready server.
- Root project license, public repository/download location, dependency
  source-distribution materials and Windows signing policy remain undecided.

## Before starting candidate builds

- [ ] Owner approves scope, including online play and supported regions.
- [ ] Owner chooses the license for original code and approves distribution materials.
- [ ] Review and commit intended game changes; remove or retain debug tracing deliberately.
- [x] Commit framework packaging changes locally and pin their exact submodule revision.
  These commits are not published; public source availability remains a release gate.
- [ ] Archive private generation inputs and record executable/corpus hashes.
- [ ] Confirm disk headroom for build objects, cache, staging and archive extraction.
- [ ] Review SDL, FFmpeg, font and toolchain pins against their notices.
- [ ] Choose version, public repository, downloads and support destination.

## Candidate build checks (not yet run)

- [ ] Build Windows first to expose untested host/overlay linking and DLL loading.
- [ ] Build all four matrix entries from fresh dedicated directories.
- [ ] Run framework tests against freshly built code.
- [ ] Package with exact module audit, dependency closure, architecture checks and manifests.
- [ ] Inspect archive contents and extract elsewhere; verify manifest file hashes.
- [ ] Scan shipped binaries/logs for build-machine paths and unintended private data.
- [ ] Collect toolchain versions and exact source revisions in candidate evidence.
- [ ] Prepare corresponding dependency sources and final notices for distribution.

## Per-artifact acceptance (repeat for all four)

Record results in a copy of `release-evidence.json`, using the archive's actual
SHA-256. Never mark another platform passed based on a developer build.

- [ ] Clean machine without development tools: launch and first-run setup.
- [ ] ISO validation: supported, wrong edition/version, truncated and modified images.
- [ ] Setup cancellation, insufficient space, copy/in-place modes and recovery.
- [ ] Paths with spaces/non-ASCII, read-only program directory and package relocation.
- [ ] Corrupt/missing installed executable or moved ISO: actionable recovery.
- [ ] Restart skips setup; `--install` reopens it without losing settings or saves.
- [ ] New save, overwrite, multiple slots, restart/reload and supported save imports.
- [ ] Replace/roll back package: saves and user settings survive.
- [ ] PO campaign through credits, bosses, recruitment and progression.
- [ ] PO+ advertised modes, progression, unlocks and stage transitions.
- [ ] Long-session memory, audio synchronization, pause, focus and suspend/resume.
- [ ] Keyboard/mouse, gamepads, remapping, hotplug, aiming and camera behavior.
- [ ] HUD, subtitles, menus, movies, sprites, Fill/aspect modes and interpolation.
- [ ] Representative GPU vendors; record actual game speed and frame-time measurements.
- [ ] Every advertised multiplayer mode between separate machines, including disconnects.

## Publication

- [ ] All promised features pass or owner explicitly changes release scope.
- [ ] Finalize `CHANGELOG.md`, requirements, known issues and download instructions.
- [ ] Confirm license/notices/source materials and signing/unsigned-download policy.
- [ ] Publish the exact tested archives, checksums and source revisions.
- [ ] Test public download/extraction and keep the previous tested version available.

No automatic publication or messages to external services are part of these scripts.
