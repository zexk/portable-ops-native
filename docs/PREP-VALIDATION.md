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

## Candidate preparation: 2026-11-07

Candidate source is port `9dd16fc`, PortableKit `5a522cc`. Original port code
now uses MIT, as selected by the owner. The license and packaging followup
changed no compiled source, corpus or compiler configuration; the resumed
build regenerates its version header before packaging.

- All 22 freshly built framework tests pass inside the pinned Linux SDK.
  The minimal FFmpeg synthetic decoder check and SDL/FFmpeg binary path
  scans also pass there.
- Windows PO and PO+ archives pass module-identity and dependency-closure
  checks, architecture/path scans, extraction and manifest/hash verification.
  Each staged program prints `9dd16fc` through `--version` under Wine 11.0.
  Exact artifacts and remaining acceptance checks are in the
  [PO evidence](release-evidence-po-windows-rc1.json) and
  [PO+ evidence](release-evidence-pop-windows-rc1.json).
- ImGui 1.92.9b, stb_truetype 1.26, stb_image 2.30, stb_image_write 1.16 and
  xxHash 0.8.3 match their vendored headers and notices. The SDL Windows
  headers and DLL version resource both identify 3.4.16.
- SDL 3.4.16 source, FFmpeg 7.1.5 source, Noto CJK Sans 2.004 font and its
  license match their pinned SHA-256 values. Both Windows archives contain
  the same verified font and recorded SDL/libc++/libunwind runtime binaries.
  Neither imports a bundled winpthread or swresample library.
- Packaged MIT, ImGui, tiny-AES-c, xxHash, SDL, Noto, LLVM and MinGW license
  files match the selected inputs byte for byte. The FFmpeg license matches
  `COPYING.LGPLv2.1` from its exact source archive. Its source note describes
  the minimal ATRAC3/ATRAC3plus/H.264 build. The full public-domain text for
  the stb alternative selected in the notices is also present in the
  included Unlicense text.
- Windows packaging, release-audit and edition-selection regression checks
  pass after the MIT packaging change. Static module analysis identifies
  the [six import gaps](release-import-audit.md) without running game code.

This adds build/package evidence, not native Windows, clean-machine,
first-run GUI, gameplay-completion or multiplayer acceptance. Publication,
signing and public CI execution remain unverified.
