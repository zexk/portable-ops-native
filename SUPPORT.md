# Player support

No public release is available from this checkout yet. Local Linux and Windows
candidates are undergoing validation. Build/package checks and gameplay
acceptance are recorded separately in the [release evidence](docs/RELEASE.md).

## Installation and data

Extract the entire archive, then open `PortableOpsNative` or
`PortableOpsPlusNative` (`.exe` on Windows). Keep bundled libraries, overlays
and fonts beside it. First-run setup asks for the supported ISO. No compiler
or source checkout is needed by players.

Supported package inputs are North American PO 1.02 / ULUS-10202 and
PO+ 1.01 / ULUS-10290. Other regions are not accepted by those packages.
If setup cannot find a previously selected ISO, restore its path or run the
program with `--install` and select it again.

User data normally lives at:

| Edition | Linux | Windows |
| --- | --- | --- |
| PO | `~/.local/share/PortableOpsNative/ULUS10202/` | `%APPDATA%\PortableOpsNative\ULUS10202\` |
| PO+ | `~/.local/share/PortableOpsPlusNative/ULUS10290/` | `%APPDATA%\PortableOpsPlusNative\ULUS10290\` |

Linux respects `XDG_DATA_HOME`. Custom `MGSPO_DATA_DIR` or `MGSPOP_DATA_DIR`
settings override these locations. Saves live under `ms0/PSP/SAVEDATA/`. The
save each slot held before its last write is kept in
`ms0/PSP/SAVEDATA/.previous/` and loaded instead when the newer one turns out
broken, for example after a crash while saving.
Back up the user-data folder before testing experimental builds. To change
versions, extract into a separate program folder and leave user data alone.
Do not delete user data as a troubleshooting step.

## Startup problems

A Vulkan-capable GPU and working Vulkan driver are required. Hardware minimums
are not established yet. On Windows, use GPU-vendor drivers rather than
individual DLL downloads. If files are missing, re-extract the whole archive.
On Linux, check the package's recorded glibc requirement. Never mix modules
or DLLs from different editions or releases.

## Tearing on Linux

Horizontal tearing while the camera turns, most visible on poles and fence
posts and unaffected by the Vsync setting, comes from the X server and not
from the game. On X11 with more than one monitor a fullscreen window may be
copied to the screen and not page-flipped. Enable the driver's tear-free
option for the output and try again:

```sh
xrandr --prop | grep -E ' connected|TearFree'
xrandr --output <output> --set TearFree on
```

Seen and resolved this way on an AMD GPU with the `amdgpu` X driver and two
monitors (144 Hz and 60 Hz), 2026-11-11. Other drivers are unverified.

## Reporting an issue

Use the repository's bug-report template once its public location is announced.
Include package version/checksum, edition, OS, GPU/driver, reproducible steps,
and whether the failure happens with an existing or fresh installation.

Check the build revision with `--version`. Every run writes what it prints to
`PortableOpsNative.log` in the user-data folder (**System > Open the data
folder**) and keeps the run before as `PortableOpsNative-previous.log`. Send
both soon after a problem: each start replaces them. Driver and library
messages appear only in a terminal; to capture those too, run from one and
save standard output/error:

```sh
./PortableOpsNative --version
./PortableOpsNative > startup.log 2>&1
```

```powershell
.\PortableOpsNative.exe --version
.\PortableOpsNative.exe *> startup.log
```

Use `PortableOpsPlusNative` for PO+. Review logs before sharing: remove personal
paths, account names and network addresses. Do not attach ISOs, decrypted
executables, account databases, keys or unreviewed network captures. Small
screenshots or a description of the failing scene are usually sufficient.

Online discovery/join is incomplete. A full-release claim must wait for its
acceptance tests or an explicit narrower release scope.
