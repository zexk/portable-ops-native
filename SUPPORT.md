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
settings override these locations. Saves live under `ms0/PSP/SAVEDATA/`.
Back up the user-data folder before testing experimental builds. To change
versions, extract into a separate program folder and leave user data alone.
Do not delete user data as a troubleshooting step.

## Startup problems

A Vulkan-capable GPU and working Vulkan driver are required. Hardware minimums
are not established yet. On Windows, use GPU-vendor drivers rather than
individual DLL downloads. If files are missing, re-extract the whole archive.
On Linux, check the package's recorded glibc requirement. Never mix modules
or DLLs from different editions or releases.

## Reporting an issue

Use the repository's bug-report template once its public location is announced.
Include package version/checksum, edition, OS, GPU/driver, reproducible steps,
and whether the failure happens with an existing or fresh installation.

Check the build revision with `--version`. For startup diagnostics, run from a
terminal and save standard output/error:

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
