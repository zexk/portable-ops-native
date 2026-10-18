# Portable Ops Native

Metal Gear Solid: Portable Ops (ULUS-10202, 1.02) and Portable Ops+ (ULUS-10290,
1.01) as native Linux programs, by static recompilation on
[PortableKit](https://github.com/zexk/PortableKit) (a fork of
[TeamGDB/PortableKit](https://github.com/TeamGDB/PortableKit) carrying the
fixes these games need, on branch `mgs`). Vulkan and SDL3.

Nothing from the games is in this repository. Bring your own disc images; the
recompiled code is generated from them on your machine.

## Build

```sh
git clone --recursive <this repository>
nix develop
cmake -S . -B out/po  -G Ninja -DMGS_EDITION=po
cmake --build out/po            # first build: no game code yet
out/po/bin/PortableOpsNative --install /path/to/po.iso --in-place
out/po/portablekit/psp_recomp ~/.local/share/PortableOpsNative/ULUS10202/EBOOT.ELF --auto profiles/po/generated
cmake -S . -B out/po && cmake --build out/po   # ~15 min with 16 GB of memory
out/po/bin/PortableOpsNative
```

Portable Ops+ is the same with `pop`, `PortableOpsPlusNative` and
`ULUS10290`.

## Game modules and stages

The games load their own modules (sound, file system, zlib) and one stage
module at a time from the disc. Each runs under the interpreter until it has
recompiled code of its own. To make that code, play with dumping on, then
build what was dumped:

```sh
MGSPO_DUMP_MODULES=$PWD/out/module-dumps out/po/bin/PortableOpsNative
nix develop --command python3 scripts/add-modules.py po out/module-dumps
cmake -S . -B out/po && cmake --build out/po
```

A module is identified by its code as it is in memory when it first runs after
the game has patched it, so every stage you have reached runs natively next
time. Import stubs inside a module's code (`KCEJ_FS` has three) are dumped
beside it as a `.stubs` file and kept out of its compiled code.

## Testing without a window

`scripts/run-headless.sh po|pop [seconds] [VAR=value...]` runs under Xvfb with
software Vulkan and writes screenshots to `out/shots-<edition>/`.

Useful variables (prefix `MGSPO_` or `MGSPOP_`): `TRACE_IO`, `TRACE_MODULES`,
`TRACE_AUDIO`, `TRACE_MPEG`, `DUMP_MODULES`, `INPUT_SCRIPT`.
`PSPRECOMP_NO_AOT=1` runs everything interpreted, to tell a recompiler fault
from a system-layer one.

## Known issues

- Portable Ops story comic cut-scenes are silent and stop at a fixed frame
  unless skipped: the scene asks for a sound bank part
  (`oss_bgm_bgm01_01_03.sep`) that is not on the disc, apparently because its
  music and voice sequences never start.
