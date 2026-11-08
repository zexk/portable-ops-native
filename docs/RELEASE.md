# First-release gate

Status: `0.1.0-rc1` published 2026-11-08 as a GitHub prerelease: North
American PO and PO+ on Linux x86-64 and Windows x64, unsigned, with
checksums and the FFmpeg source. The owner released it with the tested scope
stated; multiplayer is labelled experimental. Hardware requirements and the
per-artifact acceptance below remain open. Checkboxes below distinguish
evidence from remaining work.

## Candidate scope

The existing packaging matrix is North American PO 1.02 (ULUS10202) and
PO+ 1.01 (ULUS10290), each on Linux x86-64 and Windows x64. EU/JP PO+,
macOS, Android, ARM and explicit Steam Deck certification are outside this
matrix. The owner selected MIT for original port code. Multiplayer inclusion
still requires an owner decision; this document does not silently choose an
offline release.

## Completed preparation

- Linux and Windows packaging entry points and player readmes.
- Source-only packaging checks and a public CI workflow with no game data.
- Build-size optimizations merged into the PortableKit fork, with the finished
  optimization branch and worktree removed.
- PO+ Windows development build, all 153 overlay DLLs and runtime dependency
  closure checked; startup and basic gameplay confirmed under Wine 11.0.
  See [development smoke evidence](release-evidence-wine-pop.json). This is
  not clean-machine or packaged-candidate acceptance.
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

License, packaging and documentation changes may reuse candidate objects only
after verifying that compiled sources, generated corpora, build configuration
and the framework pin are unchanged. Record both revisions and rerun the build
before packaging so the version header and linked executable match the final
revision.

This verifies recorded staged files and rejects extras in runtime directories;
verify the outer archive checksum too. If the path scan rejects embedded compiler paths, use compiler
file-prefix mappings and rebuild; do not disable the check for publication. Do not publish a dirty candidate.

## Known source-review findings

- Current source manifests contain 94 PO and 153 PO+ overlay identities.
  Candidate builds must reproduce these exact sets; counts alone are not
  sufficient, and previous development directories can contain stale modules.
- Recent smoke logs contain short interpreted stage startup sequences before
  recompiled code takes over. Do not claim zero interpretation.
- The [import audit](release-import-audit.md) identifies six gaps across PO's
  title, PO+'s title/briefing and PO+'s main executable. These explain the
  stage-loader counts and include a reached `sceKernelCancelWakeupThread`
  fallback. Effects on gameplay remain unverified; unresolved counts alone
  do not measure compatibility.
- `host/main.cpp` hashes an installed executable but falls back to the
  interpreter when its hash is unknown. The initial missing-data check is
  not an integrity check. Release recovery for corrupted installed data
  remains open; changing framework behavior needs compiled tests.
- The experimental server (`mpo-server`, shipped beside PO+) stores account
  passwords in plaintext in `accounts.txt` (the login proof is keyed by the
  plain password), and its protocol implementation is incomplete. Do not
  present it as a production-ready server.
- Public repository/download location, dependency
  source-distribution materials and Windows signing policy remain undecided.

## Before starting candidate builds

- [x] Owner selects North American PO and PO+ on Linux and Windows.
- [x] Owner finalizes advertised online functionality.
- [x] Owner chooses MIT for original port code; project license is included in
  Linux and Windows packaging. Game and third-party code retain separate terms.
- [x] Owner approves final distribution materials.
- [x] Review and commit packaging fixes and the early `--version` path.
  Temporary, uncommitted PO model/camera tracing stays outside candidate source.
- [x] Commit framework packaging changes locally and pin their exact submodule revision.
  Framework commits are pushed to the owner's PortableKit fork. The port has
  no remote configured; its public source location remains undecided.
- [x] Freeze private executable and generated-corpus snapshots; record both
  executable hashes, both corpus hashes and the exact source revisions in the
  local candidate's `inputs.json`. Compilation began at port `d45129a` and
  PortableKit `5a522cc`. The MIT packaging followup at `9dd16fc` changes no
  compiled source or build configuration; both revisions and unchanged corpus
  fingerprints are recorded, and the build resumes at `9dd16fc`.
- [x] Check initial disk headroom: about 49 GiB available before candidate
  snapshots/builds. Recheck before each matrix build.
- [x] Review SDL, FFmpeg, font and toolchain pins against their notices.
  Header/resource versions, downloaded source/font hashes, shipped Windows
  runtime hashes and all bundled Windows license files match the recorded
  candidate inputs. See [preparation validation](PREP-VALIDATION.md).
- [x] Choose version, public repository, downloads and support destination.

## Candidate build checks

- [x] Build and run the PO+ Windows development target under Wine, including
  loaded native modules. Fresh candidate builds still require separate checks.
- [x] Build all four matrix entries from fresh dedicated directories.
- [x] Run framework tests against freshly built code: 22/22 pass in the pinned
  Linux SDK. SDL and minimal FFmpeg libraries also pass binary path scans;
  the FFmpeg synthetic decoder check passes there.
- [x] Package with exact module audit, dependency closure, architecture checks and manifests.
- [x] Inspect archive contents and extract elsewhere; verify manifest file hashes.
- [x] Scan shipped binaries/logs for build-machine paths and unintended private data.
- [x] Collect toolchain versions and exact source revisions in candidate evidence.
- [x] Prepare corresponding dependency sources and final notices for distribution.

Windows candidates built and passed packaging checks on 2026-11-07 at port
`9dd16fc` and PortableKit `5a522cc`: [PO evidence](release-evidence-po-windows-rc1.json)
and [PO+ evidence](release-evidence-pop-windows-rc1.json). These record archive
hashes, exact module counts, MIT license inclusion and the staged Wine
`--version` check. Both passed user-driven packaged gameplay smoke tests
under Wine. PO+ also passed fresh ISO copy/import, restart without setup
and save reload.
Both archives remain local; full gameplay and clean-machine acceptance
remain pending, including native Windows testing.

[Linux PO evidence](release-evidence-po-linux-rc1.json) and
[Linux PO+ evidence](release-evidence-pop-linux-rc1.json) record verified
archives with 94 and 153 modules, respectively. Both pass relocated
`--version` checks in the pinned SDK. The four-candidate matrix completed
on 2026-11-07 at 23:15 UTC. Both Linux packages subsequently passed
user-driven gameplay smoke tests through `steam-run` on NixOS. All four
remain local candidates with full acceptance pending. Each distribution
directory includes checksums and the corresponding FFmpeg source archive;
final publication review remains
pending.

## Per-artifact acceptance (repeat for all four)

Record results in a copy of `release-evidence.json`, using the archive's actual
SHA-256. Never mark another platform passed based on a developer build.

- [x] Brief user-driven gameplay smoke for all four exact candidate archives:
  Linux through `steam-run` and Windows through Wine, on one AMD GPU system.
  This does not complete the broader checks below.
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

- [x] All promised features pass or owner explicitly changes release scope.
- [ ] Finalize `CHANGELOG.md`, requirements, known issues and download instructions.
- [x] Confirm license/notices/source materials and signing/unsigned-download policy.
- [x] Publish the exact tested archives, checksums and source revisions.
- [x] Test public download/extraction and keep the previous tested version available.

No automatic publication or messages to external services are part of these scripts.

## Source history rewrite

Before publication, history was rewritten to remove a local filesystem path
from early `flake.nix` and `flake.lock` revisions. Commit hashes changed;
trees after the path fix did not. The `0.1.0-rc1` binaries, their
`--version` output and the evidence files keep the pre-rewrite hashes.
Matching commits have identical trees:

| Recorded | Public | Tree |
| --- | --- | --- |
| `9dd16fc` | `5579c7e` | `0a31612` |
| `d45129a` | `80156e0` | `38e8f74` |
| `d86cf7f` | `b675077` | `65fa892` |
