# Portable Ops Native

Metal Gear Solid: Portable Ops (ULUS-10202, 1.02) and Portable Ops+ (ULUS-10290,
1.01) as native PC programs, by static recompilation on
[PortableKit](https://github.com/zexk/PortableKit) (a fork of
[TeamGDB/PortableKit](https://github.com/TeamGDB/PortableKit) carrying the
fixes these games need, on branch `mgs`). Vulkan and SDL3.

Unofficial fan project, not affiliated with or endorsed by Konami. Metal Gear
and Metal Gear Solid are trademarks of Konami Digital Entertainment.

No disc images or generated game code are tracked in this repository.
[Releases](https://github.com/zexk/portable-ops-native/releases) provide
precompiled programs; players supply their own supported disc image.
Maintainers generate and compile the native game code before packaging.

`0.1.0-rc1` is a preliminary prerelease. Windows builds were tested under
Wine only; multiplayer (`mpo-server`) is experimental. See
[player support](SUPPORT.md), [release gates](docs/RELEASE.md) and the
[changelog](CHANGELOG.md).

Original port code uses the [MIT License](LICENSE). Game data and generated
game code are not covered by this grant. Third-party components retain their
own licenses; see [third-party notices](packaging/THIRD_PARTY_NOTICES.md).

## Local Linux packages

Maintainers can build both North American editions locally after generating
both main corpora and their module corpora (`profiles/<edition>/generated/`,
`overlays/` and `units/`):

```sh
for edition in po pop; do
    nix develop --command scripts/package-linux.sh "$edition"
done
```

The build uses Docker or Podman and the pinned Steam Runtime SDK. The wrapper
needs Python 3.11+ (provided by `nix develop`, or your host environment).
Players need neither Nix nor a compiler. Builds are resumable. Use
`po` or `pop` to build one edition; `all` also includes EU/JP PO+. Add
`--skip-build` to repack an already staged build. Archives, checksums, build information and
FFmpeg source land in `out/release-linux-{po,pop}/dist/`. This builds thousands
of units; the default is two compile jobs, with a 2 GiB compiler cache per
edition. `scripts/test-package-config.sh` checks edition selection without
starting a build. These scripts use the accompanying local changes to
PortableKit's Linux release scripts.

Extract the entire archive and run `PortableOpsNative` or
`PortableOpsPlusNative`. With no installed assets, the built-in setup asks for
the ISO, validates it and prepares game data before launching. Later launches
start the game directly. `--install` opens setup again. Data and saves stay
in the user's data directory when the program is replaced.

## Local Windows packages

Windows x64 preparation covers both editions. Follow
[the Windows build and packaging recipe](packaging/windows/BUILDING.md).
`scripts/package-windows.py` packages existing release builds into portable
ZIPs with runtime DLLs and native modules; it never compiles anything or
ships a compiler. Both Windows candidate archives passed gameplay smoke
tests under Wine. PO+ also passed first-run ISO import and save reload there.
Native Windows and full archive acceptance remain pending.

## Build

```sh
# From a source checkout:
git submodule update --init --recursive
nix develop
cmake -S . -B out/po  -G Ninja -DMGS_EDITION=po
cmake --build out/po            # first build: no game code yet
out/po/bin/PortableOpsNative --install /path/to/po.iso --in-place
out/po/portablekit/psp_recomp ~/.local/share/PortableOpsNative/ULUS10202/EBOOT.ELF --auto profiles/po/generated
cmake -S . -B out/po && cmake --build out/po   # ~15 min with 16 GB of memory
out/po/bin/PortableOpsNative
```

Portable Ops+ is the same with `pop`, `PortableOpsPlusNative` and
`ULUS10290`. Its European and Japanese releases are the editions `pop-eu`
(`ULES01003`) and `pop-jp` (`ULJM05261`), built from the same profile.

## Game modules and stages

The games load their own modules (sound, file system, zlib) and one stage
module at a time from the disc. Each runs under the interpreter until it has
recompiled code of its own.

Stages are made straight from the disc: `stage-images.py` loads each stage
as the engine does, linking it to the engine with the relocations in the
stage's `.rlc` file, and writes the code the game runs:

```sh
nix develop --command python3 scripts/stage-images.py po /path/to/po.iso out/stages-po
nix develop --command python3 scripts/add-modules.py po out/stages-po
cmake -S . -B out/po && cmake --build out/po
```

The other modules come from playing with dumping on, then building what was
dumped the same way:

```sh
MGSPO_DUMP_MODULES=$PWD/out/module-dumps out/po/bin/PortableOpsNative
nix develop --command python3 scripts/add-modules.py po out/module-dumps
```

A module is identified by its code as it is in memory when it first runs after
the game has patched it. A stage also runs for a moment before that, which
`add-modules.py` leaves to the interpreter. Stages of one map (`trneko0800`,
`0801`, `0802`) differ in a few units only, so `add-modules.py` moves each
unit to `profiles/<edition>/units/`, named by its code, and the build compiles
it once for all the stages that share it. Import stubs inside a module's code
(`KCEJ_FS` has three) are dumped beside it as a `.stubs` file and kept out of
its compiled code.

## Optional Fulton prototype (PO only)

Set `MGSPO_FULTON=1` before launching Portable Ops Native. Press **L + R +
Triangle** near an unconscious ordinary soldier to recover them without a
truck. Release the chord before another attempt. Disabled unless explicitly
enabled; PO+ and online play are excluded.

This first mechanic prototype performs immediate recovery using PO's original
soldier generation and pending-capture handling. It has no balloon, lift
animation or PW asset dependency yet. Scripted prisoners, carried soldiers
and unsupported scenario callbacks are excluded. Full pending buffers and
rejected captures leave the soldier present. Original roster admission still
runs later through the game's normal results and selection screens.

The port locates capture helpers from unique stage-code signatures and checks
them again before use. Logs begin with `[fulton]`; `soldier recovered` means
a source-2 record for the selected soldier was appended before actor cleanup.
Instant capture and permanent roster admission were tested on an ordinary PO
soldier. Read-only memory checks matched the captured WHALE record (`0x410`)
to active roster slot 18 after mission exit, with pending count cleared.
Save/reload acceptance remains unverified.

Selection and commit checks run without game data:

```sh
c++ -std=c++20 -Iportablekit/include -Iportablekit/host -Icommon/host \
    scripts/test-fulton.cpp -o out/test-fulton
out/test-fulton
```

### Developer visual experiment

`MGSPO_FULTON_VISUALS=1` adds the PW PSP balloon model. Snake attaches it with
PO's own crouch-and-place clip (the TNT/claymore/magazine one: clip `0x25` of
`sna_wp_claymore`), then the balloon inflates and rises. Both checked live.
The soldier still vanishes without a lift animation. Failed or interrupted
attachment cancels capture and leaves the soldier present.

Provide a local asset directory containing `fult_item_balloon.mdp` and
`fult_item_balloon.txp` from PW PSP. PO's texture loader expects its own
layout, so the script converts the TXP. Prepare selected stages from an
extracted PO disc:

```sh
python3 scripts/prepare-fulton-assets.py STAGE_DIRECTORY ASSET_DIRECTORY OUTPUT \
    --stage st01 --stage st01t1 --stage st01t2
MGSPO_FULTON=1 MGSPO_FULTON_VISUALS=1 MGSPO_FULTON_STAGE_DIR=OUTPUT \
    out/po/bin/PortableOpsNative
```

The stage filter verifies both original and replacement hashes. It supplies
prepared bytes only in memory; the disc image stays untouched. Stages without
prepared donor data cannot run this visual experiment.
`MGSPO_FULTON_TRACE_MOTION=1` logs every motion request and the player's
motion channels (two 0x80-byte layers). Position Snake facing
the unconscious soldier for the attachment test.

## Testing without a window

`scripts/run-headless.sh po|pop [seconds] [VAR=value...]` runs under Xvfb with
software Vulkan and writes screenshots to `out/shots-<edition>/`.

Useful variables (prefix `MGSPO_` or `MGSPOP_`): `TRACE_IO`, `TRACE_MODULES`,
`TRACE_AUDIO`, `TRACE_MPEG`, `DUMP_MODULES`, `INPUT_SCRIPT`.
`PSPRECOMP_NO_AOT=1` runs everything interpreted, to tell a recompiler fault
from a system-layer one.

## Online play

Portable Ops+ plays on line through SaveMGO's community server by default.
`mpo-server` is a server of our own, written from the protocol
(`server/PROTOCOL.md`): one C file (`server/mpo_server.c`) with no
dependencies, built with the game and shipped beside Portable Ops+ in its
packages. Without CMake: `cc -O2 -o mpo-server server/mpo_server.c` (add
`-lws2_32` on Windows).

```sh
out/pop/bin/mpo-server --data out/server-data adduser NAME PASSWORD PLAYER_NAME
out/pop/bin/mpo-server --data out/server-data --web-port 8080 \
    --game ~/.local/share/PortableOpsPlusNative/ULUS10290/EBOOT.ELF
MGSPOP_ONLINE_SERVER=127.0.0.1:8080 out/pop/bin/PortableOpsPlusNative
```

`--game` gives it the game's defaults for the one file the game insists on
downloading. Each server chooses its own terms, news and banner (see
`mpo-server --help`). Logging in, the lobby, hosting, and finding and joining a game work
(tested with two instances on one machine). `<PREFIX>_NET_CAPTURE=<file>` records a
session's traffic, and `server/decode.py` reads it.
