# Portable Ops Native

Metal Gear Solid: Portable Ops (ULUS-10202, 1.02) and Portable Ops+ (ULUS-10290,
1.01) as native PC programs, by static recompilation on
[PortableKit](https://github.com/zexk/PortableKit) (a fork of
[TeamGDB/PortableKit](https://github.com/TeamGDB/PortableKit) carrying the
fixes these games need, on branch `mgs`). Vulkan and SDL3.

No disc images or generated game code are tracked in this repository. Planned
releases provide precompiled programs; players supply their own
supported disc image. No release artifacts are approved yet. Maintainers
generate and compile the native game code before packaging.

See [player support](SUPPORT.md), [release gates](docs/RELEASE.md) and
[unreleased changes](CHANGELOG.md). Public download location, original-code
license, release version and multiplayer scope remain open decisions.

## Local Linux packages

Maintainers can build both North American editions locally after generating
both main corpora and their module corpora (`profiles/<edition>/generated/`,
`overlays/` and `units/`):

```sh
nix develop --command scripts/package-linux.sh both
```

The build uses Docker or Podman and the pinned Steam Runtime SDK. The wrapper
needs Python 3.11+ (provided by `nix develop`, or your host environment).
Players need neither Nix nor a compiler. Builds are resumable. Use
`po` or `pop` instead of `both` to build one edition, or add `--skip-build` to
repack an already staged build. Archives, checksums, build information and
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
ships a compiler. Windows builds and first-run launch checks remain pending.

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

## Testing without a window

`scripts/run-headless.sh po|pop [seconds] [VAR=value...]` runs under Xvfb with
software Vulkan and writes screenshots to `out/shots-<edition>/`.

Useful variables (prefix `MGSPO_` or `MGSPOP_`): `TRACE_IO`, `TRACE_MODULES`,
`TRACE_AUDIO`, `TRACE_MPEG`, `DUMP_MODULES`, `INPUT_SCRIPT`.
`PSPRECOMP_NO_AOT=1` runs everything interpreted, to tell a recompiler fault
from a system-layer one.

## Online play

Portable Ops+ plays on line through SaveMGO's community server by default.
`server/` is a server of our own, written from the protocol
(`server/PROTOCOL.md`) with Python's standard library alone:

```sh
python3 server/mpo_server.py --data out/server-data adduser NAME PASSWORD PLAYER_NAME
python3 server/mpo_server.py --data out/server-data --web-port 8080 \
    --game ~/.local/share/PortableOpsPlusNative/ULUS10290/EBOOT.ELF
MGSPOP_ONLINE_SERVER=127.0.0.1:8080 out/pop/bin/PortableOpsPlusNative
```

`--game` gives it the game's defaults for the one file the game insists on
downloading. Each server chooses its own terms, news and banner (see the
script's help). Logging in, the lobby and hosting a game work; other players
finding and joining a game do not yet. `<PREFIX>_NET_CAPTURE=<file>` records a
session's traffic, and `server/decode.py` reads it.
