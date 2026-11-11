# Portable Ops Native

Native PC ports of Metal Gear Solid: Portable Ops and Portable Ops+, built by
static recompilation on [PortableKit](https://github.com/zexk/PortableKit).
Linux and Windows, Vulkan and SDL3.

Work in progress. Public packages are not available yet.
[Build from source](docs/BUILDING.md) or check the [release status](docs/RELEASE.md).

## Features

- Right-stick camera, keyboard and controller bindings
- Widescreen, resolution scaling, anti-aliasing and anisotropic filtering
- Adjustable field of view, detail distance and camera speed
- Frame interpolation with latency prediction
- PPSSPP-format texture packs
- Optional Peace Walker Fulton recovery and crouchwalking in PO
- PO+ online play through SaveMGO or a private server

## Install

Requires a Vulkan-capable GPU and your own unmodified PSP ISO.

| Game | Disc | Version |
| --- | --- | --- |
| Portable Ops | ULUS-10202 | 1.02 |
| Portable Ops+ | ULUS-10290 | 1.01 |

1. Extract the entire package for your edition.
2. Run `PortableOpsNative` or `PortableOpsPlusNative` (`.exe` on Windows).
3. Select your ISO when setup opens.

Later launches start the game directly. Run with `--install` to reopen setup.
Keep the bundled libraries, overlays and fonts beside the executable.

Saves and settings live in the user-data folder. Replacing the program keeps
them. Create a `portable` folder beside the executable to store them there
instead. Paths and troubleshooting: [SUPPORT.md](SUPPORT.md).

## Controls and settings

Press **Esc** or **L3 + R3** to open the settings menu. Bindings are in
**Controls**, along with camera options. Graphics and interpolation are in
**Video**.

- **F3**: toggle the performance overlay
- **F11**: switch between fullscreen and a window
- **F12**: save a screenshot in the user-data folder's `screenshots/`
  (**System > Open the screenshots folder**)

**Game > Menu > Open the menu when switching away** (off by default) brings
up the pause menu when another window takes the focus.

Both games have **Game > Startup** toggles for skipping the logos and opening
movie. **Game > Controls > Analog stalking** (off by default) stalks while the
movement stick is tilted only slightly; it is checked in PO, not yet in PO+.

## Peace Walker features

PO only. Both are off by default, under **Game > Peace Walker**.

- **Fulton recovery**: press Triangle beside an unconscious or prone held-up
  soldier. Snake is free after attaching the device; the soldier keeps lifting,
  so you can recover another. The capture jingle plays without the banner.
- **Crouchwalking**: move while crouched. Hold Triangle for the slower stalking
  gait; release movement to return to crouch idle.

Use the feature's import action to select a Peace Walker PSP ISO or Master
Collection installation folder. Then enable it and restart the game. Importing
assets leaves the feature off. The source is read-only and is not needed after
import. Converted asset folders are accepted too.

## Online play

PO+ connects to SaveMGO's community server by default. The included
`mpo-server` runs a private server. To choose another server:

```sh
MGSPOP_ONLINE_SERVER=127.0.0.1:8080 ./PortableOpsPlusNative
```

Server setup: [build notes](docs/BUILDING.md#private-server).
Protocol: [server/PROTOCOL.md](server/PROTOCOL.md).

## Known issues

- Peace Walker features and multiplayer are experimental.
- Windows builds have been tested under Wine; native Windows acceptance and
  full campaign testing are pending.

For bug reports, include the build revision (`--version`), game edition, OS,
GPU/driver and steps to reproduce. See [SUPPORT.md](SUPPORT.md).

## Build

[Source builds, stage modules and packaging](docs/BUILDING.md).
PO+ EU and JP have source profiles (`pop-eu`, `pop-jp`); both are compile-tested.

[Changelog](CHANGELOG.md) · [Research](docs/feature-research.md)

Original port code is MIT licensed. See [LICENSE](LICENSE) and
[third-party notices](packaging/THIRD_PARTY_NOTICES.md). Disc images and generated
game code are not included. Unofficial project, not affiliated with Konami.
