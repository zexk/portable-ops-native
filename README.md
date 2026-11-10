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
in the user's data directory when the program is replaced. For a portable
copy, create a folder named `portable` beside the executable: data, saves
and settings then live there instead (on any system but Android).

Press **F12** to save a PNG of the presented window at its full resolution,
including the interface, in `screenshots/` inside the data folder. Each press
uses a new filename. **Video > Field of view** adjusts the 3D view live from
75% to 150%; 100% is the original view. Relative aim zoom and interface size
are preserved. The setting is saved; `MGSPO_FOV=125` overrides it for one run.

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
module at a time from the disc. Each needs recompiled code for every image it runs. Unknown images fall
back to the interpreter.

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

A module is identified by its code in memory at first entry and again after
an instruction-cache flush. `stage-images.py` writes both the PRX-relocated
startup image and the image after engine linking, with a `.entry` sidecar
for `module_start`. `add-modules.py` compiles both. The runtime matches the
startup corpus before `module_start`, then drops it when the game flushes
its instruction cache after linking and matches the linked corpus on its
next call. Regenerate stage images to add startup corpora to an existing
build.

Stages of one map (`trneko0800`, `0801`, `0802`) differ in a few units only, so `add-modules.py` moves each
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

`MGSPO_FULTON_VISUALS=1` adds PW PSP donors to prepared stages. Snake
attaches the device with the optional converted PW `snake_coop` clip 156
(`fult_place.mtar`), falling back to PO's crouch-and-place clip (`0x25` of
`sna_wp_claymore`) when absent. The harness (`fult_item_body`) and balloon ride the
soldier's upper spine. The harness uses bone 2's local point `(0,100,-200)`
and bone 4 for initial orientation, with PW's model rotation correction.
The balloon stays upright, `1000 * growth` above the harness.
`MGSPO_FULTON_HARNESS_Y` adds a world-Y trim (default 0);
`MGSPO_FULTON_BALLOON_Y` overrides the full-size balloon height (default
1000). The soldier plays PW's
lift clips (`enedam` 101 then 103, converted by the metal-gear `mtar.py` into
`fult_enemy.mtar`) and is captured on the last frame. Afterwards Snake gets
back the engine's last own motion requests. Failed or interrupted attachment
cancels capture and leaves the soldier present. The Fulton sound
(`fulton.sep`, PW's `segrp_75_furuton` as PO bank `0x31`) is filed and
activated per stage. Optional `fulton-lift.sep` adds two common PW
extraction effects, bank `0x32`, sounds `0x272` and `0x273`, at yank start
and settle cursor 100. The metal-gear `tools/portable-ops/fulton_sfx.py`
builds it from two decoded mono 44.1 kHz 16-bit WAV effects, with a normal
ADPCM instrument (without its base note, pitch is five octaves off). PO's
sound table types `0x272` as ATRAC3plus, which plays one effect at a time,
so the host gives it `0x273`'s ADPCM entry; otherwise it takes the balloon's
voice and neither plays. Banks lost in a stage reset are filed again, and a
cleared sound2 bank entry is activated again before each play. PW
attachment and recruitment completed in PO live checks; balloon and both
effects play, also after a stage reset. Prone soldiers held at gunpoint
(hold-up flag `0x40`, motion state `0x152`) are eligible, confirmed in game;
a rejected nearby soldier's flags and behavior are logged on a chord press.
Optional `fulton-foley.sep` adds the three confirmed attachment foleys at
donor ticks 60, 175 and 280, plus the confirmed ordinary-soldier yank grunt
at settle cursor 140. Build it with `fulton_sfx.py --foley` from four mono
44.1 kHz 16-bit WAVs in that order. It uses private bank `0x144` and sound
IDs `0xB9B` through `0xB9E`; the host extends the sound catalog while
preserving existing entries. The first live check confirmed bank loading
but exposed the engine wrapper's fixed sound-ID limit. Appended cues now
use its listener and positional sound2 calls directly. The user confirmed
new audio playback on a fresh launch; logs show bank activation, completed
recovery and no sound refusals. Survival across a stage reset still needs
a live check for this new bank.
Other voice families and two remaining attachment cues remain unmapped.
Body placement now eases the soldier toward Snake's starting heading +90
degrees along the shortest arc. The one-eighth-per-tick easing approximates
the PW capture; its exact integer rule remains unmapped. The actor root
position stays unchanged, as observed in PW. Harness orientation follows
the two bone points during attachment, then stays fixed while its origin
follows bone 2 during lift. Live testing found rotation broadly similar to
PW. Following a frame-linked PW trace, the lift clip, balloon sound and
growth start together at 260/375 of the attach, the harness stays upright
and grows with the balloon, and both follow the soldier's spine into the
sky. The visible body slide still needs animation/root-bone analysis. Missing
or invalid bone matrices use the saved enemy position and a rigid fallback.

Provide a local asset directory with `fult_item_balloon.mdp/.txp` and
`fult_item_body.mdp/.txp` from PW PSP, `fult_enemy.mtar`, and optionally
`segrp_75_furuton.sep`, `fult_place.mtar`, `fulton-lift.sep` and
`fulton-foley.sep`. The optional
placement archive comes from metal-gear's `mtar.py`, `--clip 156` of PW's
`snake_coop.mtar`; it loads through PO's resource handler once per stage,
without increasing the fixed-size stage ZAR. The script converts the TXPs to PO's layout, renames
textures the two packs share, puts the packs before the models, and
renumbers the sound bank. Prepare selected stages from an extracted PO disc:

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
