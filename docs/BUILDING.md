# Building Portable Ops Native

Game code is generated locally from your ISO. Generated code, module corpora
and disc images stay out of Git and release source archives.

## Linux

The Nix development shell provides CMake, Ninja, a compiler, SDL3, Vulkan and
FFmpeg. From the repository root:

```sh
git submodule update --init --recursive
nix develop
cmake -S . -B out/po -G Ninja -DMGS_EDITION=po
cmake --build out/po --target PortableOpsNative psp_recomp -j2
out/po/bin/PortableOpsNative --install /path/to/po.iso --in-place
out/po/portablekit/psp_recomp \
    ~/.local/share/PortableOpsNative/ULUS10202/EBOOT.ELF \
    --auto profiles/po/generated
cmake -S . -B out/po
cmake --build out/po --target PortableOpsNative -j2
```

The first build has no recompiled game code. Generate the stage and module
corpora below before using it for normal play.

| Edition | CMake edition | Program | Disc |
| --- | --- | --- | --- |
| PO NA | `po` | `PortableOpsNative` | ULUS10202 |
| PO+ NA | `pop` | `PortableOpsPlusNative` | ULUS10290 |
| PO+ EU | `pop-eu` | `PortableOpsPlusNative` | ULES01003 |
| PO+ JP | `pop-jp` | `PortableOpsPlusNative` | ULJM05261 |

Use separate build directories and the matching ISO for each edition.
PO+ data lives under `PortableOpsPlusNative` rather than `PortableOpsNative`.

## Stage and module code

The engine loads sound, file-system and compression modules, plus one stage
module at a time. Each loaded image needs a matching recompiled corpus;
unknown images fall back to the interpreter.

Generate stages from the disc:

```sh
python3 scripts/stage-images.py po /path/to/po.iso out/stages-po
python3 scripts/add-modules.py po out/stages-po
cmake -S . -B out/po
cmake --build out/po -j2
```

Capture other loaded modules during play, then compile the dumps:

```sh
MGSPO_DUMP_MODULES=$PWD/out/module-dumps out/po/bin/PortableOpsNative
python3 scripts/add-modules.py po out/module-dumps
cmake -S . -B out/po
cmake --build out/po -j2
```

Replace `po` with the chosen edition; PO+ environment variables use `MGSPOP_`.
Use `PSPRECOMP_NO_INTERPRETER=1` for coverage checks. It stops at any address
without recompiled code.

Stage images include PRX-relocated startup code and the code after engine
linking. `.entry` sidecars record `module_start`; `.stubs` files keep imports
out of generated code so they reach the HLE. The runtime checks image identity
at first entry and after instruction-cache flushes. `coverage=full` in generated
metadata marks a corpus with an entry for every instruction word.

Only `module_start` runs from a startup image: the engine links the stage when
it returns, then flushes the instruction cache, and the runtime identifies the
linked image. When the closure of `module_start` has no computed jumps or calls
and stays inside the image, `stage-images.py` writes it to a `.startup` sidecar
and `add-modules.py` compiles only that (`coverage=startup`). In every PO NA
1.02 and PO+ NA 1.01 stage it is the same 13 instructions, where a full corpus
of each startup image was half of the module build. Other images get full
coverage.

Module units compile at `-O1` (`PSPRECOMP_OVERLAY_OPT_LEVEL`; PortableKit's
default is `-Os`). With GCC 16.2, 12 of 13 sampled PO and PO+ units compiled
in 47 to 57 percent of the `-Os` time with 10 to 34 percent smaller objects;
the largest PO+ unit took as long. Idle in st01t2 after loading a save, guest
time was 10.25 ms per game frame at `-O1` and 10.50 at `-Os`, at 20 game
frames per second in both, within the runs' spread. A build directory keeps
its cached level until reconfigured with `-U PSPRECOMP_OVERLAY_OPT_LEVEL`.

`scripts/stage-sweep.py` boots every stage on the disc headless, with the
interpreter off, and reports any stage that stops for missing code:

```sh
python3 scripts/stage-sweep.py po /path/to/user-data/ULUS10202
```

To check a built module against its dump:

```sh
c++ -std=c++20 -Iportablekit/include \
    scripts/check-module-coverage.cpp -ldl -o out/check-module-coverage
out/check-module-coverage out/po/bin/overlays/modBASE_NAME_HASH.so \
    out/module-dumps/modBASE_NAME_HASH.bin
```

During development, build named executable and module targets. Host changes
do not require rebuilding module libraries. Keep whole-corpus builds and
coverage sweeps for release candidates.

## Packages

Generate the main and module corpora before packaging. Players receive compiled
programs and runtime dependencies; they import their ISO through setup.

Linux packages use Docker or Podman with the pinned Steam Runtime SDK:

```sh
nix develop --command scripts/package-linux.sh po
nix develop --command scripts/package-linux.sh pop
```

`all` also builds EU/JP PO+. `--skip-build` repacks an existing staged build.
Archives, checksums, build information and FFmpeg source go to
`out/release-linux-<edition>/dist/`.

Windows builds and ZIP packaging:
[packaging/windows/BUILDING.md](../packaging/windows/BUILDING.md).
Release checks: [RELEASE.md](RELEASE.md).

## Research stage boot

Use a copied user-data folder for direct stage boots:

```sh
MGSPO_DATA_DIR=/path/to/copied-profile \
    MGSPO_STAGE_REDIRECT=title=st11_boss out/po/bin/PortableOpsNative
```

`title=st11_boss` reaches Python; `title=st20` reaches Gene. Both were checked
on PO ULUS-10202 1.02. This uses startup state rather than a campaign checkpoint.
PO+ long-name redirects are unverified. `scripts/test-stage-redirect.cpp`
checks name selection and rejection.

## Testing

`scripts/run-headless.sh po|pop [seconds] [VAR=value...]` uses Xvfb and software
Vulkan. Screenshots go to `out/shots-<edition>/`.

Useful environment variables, with prefix `MGSPO_` or `MGSPOP_`:
`TRACE_IO`, `TRACE_MODULES`, `TRACE_AUDIO`, `TRACE_MPEG`, `DUMP_MODULES`,
`INPUT_SCRIPT`, `PERF`, `TRACE_INTERPOLATION`, `TRACE_RENDER`, `TRACE_STALLS`.
`PSPRECOMP_NO_AOT=1` runs under the interpreter for comparison.

## Private server

`mpo-server` builds with the game and is packaged with PO+. It can also be built
on its own:

```sh
cc -O2 -o mpo-server server/mpo_server.c
```

Add `-lws2_32` on Windows. Create an account and start it:

```sh
out/pop/bin/mpo-server --data out/server-data adduser NAME PASSWORD PLAYER_NAME
out/pop/bin/mpo-server --data out/server-data --web-port 8080 \
    --game ~/.local/share/PortableOpsPlusNative/ULUS10290/EBOOT.ELF
MGSPOP_ONLINE_SERVER=127.0.0.1:8080 out/pop/bin/PortableOpsPlusNative
```

`--game` supplies the defaults file required by the client. See `mpo-server --help`
for server options and [PROTOCOL.md](../server/PROTOCOL.md) for the protocol.
