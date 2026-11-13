# Changelog

## 0.1.0-rc2 (2026-11-13, prerelease)

### New

- **Peace Walker features** (PO, experimental, off by default): Game > Peace
  Walker. Import from a PW PSP disc image or Master Collection folder, turn
  the feature on, then Restart to apply. The source is not needed afterwards.
  - **Fulton recovery**: Triangle beside an unconscious or held-up prone
    soldier recovers him on the spot, without the truck. Snake is free once
    the balloon is attached, so recoveries chain.
  - **Crouchwalking**: move while crouched; hold Triangle to stalk. With the
    Mk22 out, the upper body uses PW's Mk.22 clips.
- **Picture**: Video > Picture.
  - **Field of view**: 75 to 150%.
  - **Detail distance**: 100 to 400%. Full-detail soldiers and their shadows
    reach farther.
- **Frame interpolation**
  - **Prediction**: Video > Timing. Cuts the delay interpolation adds.
  - HUD, menus and fades are interpolated too.
  - **Show frames early** and **Read the controller late**: Game > Response.
    Both lower input delay.
- **Controls**
  - First-person aim follows the stick's direction instead of snapping to
    eight directions, and weapon aim turns smoothly.
  - **Analog stalking**: Game > Controls. A slight tilt stalks; the threshold
    is adjustable.
- **Sound mix**: Audio > Mix sets music, effects and voices. PO adds
  cutscenes.
- **Startup skips**: Game > Startup skips the logos and the opening movie.
- **PO units in PO+**: New Game imports them from Portable Ops Native's save,
  with a list when there are several saves. A portable folder is not read.
- **Interface**
  - **F11** switches between fullscreen and a window.
  - **F12** saves a PNG screenshot to the data folder's `screenshots/`;
    System > Open the screenshots folder.
  - **FPS only**: a small counter, under Video > Timing > Performance.
  - **Open the menu when switching away**: Game > Menu.
- **Data**
  - **Portable mode**: a `portable` folder beside the executable holds the
    data, settings and saves.
  - Each slot's previous save is kept and loaded if the newer one is broken.
  - Each run writes a log to the data folder; the previous one is kept.

### Changed

- Every stage on the disc is compiled to native code; rc1 left some to the
  interpreter.
- New players start fullscreen with Fill aspect, 4x MSAA, 16x anisotropic
  filtering, 200% detail distance, and interpolation at display rate with
  full Prediction. Existing settings are kept.
- New Game tab: Game speed, Fast loading and the menu pause switches moved
  there.
- Audio > Volume is now Audio > Master.

### Fixed

- **Disable camera follow** (was Camera follows behind) had no effect.
- PO+ online: camera and control options had no effect in the lobby and maps.
- L + D-pad left or right looped an animation with Swap sticks in first
  person on.
- Thermal goggles showed black; night vision flickered.
- Interpolation
  - Smoke-filled rooms ran at the game's frame rate.
  - Scenery at the screen edge appeared a frame late.
  - Characters showed lit on the black screen after a loading screen.
- Saves
  - Import, export and backup missed the three save slots.
  - A crash right after saving could break the save.
  - On Windows, a file held by another program failed the save.
  - An unreadable save stopped the game.
- Menu
  - Clicking a setting's name changed it instead of selecting it.
  - Folder buttons did nothing on Windows.
  - Some text described another game.
  - Greyed rows gave no reason; Restore video defaults kept the text font.
- `mpo-server`: lowercase login names were refused, and `adduser` accepted
  passwords under 6 characters, which the game rejects.

### Removed

- The Network tab (ad hoc play), until ad hoc is tested.

### Not in this release

- PO online, ad hoc play in both games, PO+ EU and JP packages.

## 0.1.0-rc1 (2026-11-08, prerelease)

- Prepared Linux x86-64 and Windows x64 packaging for North American PO and PO+.
- Reused first-run ISO setup; packages are intended to contain native game
  code and runtime dependencies, with no player-side compiler.
- Added module identity, dependency, architecture and package-content checks.
- Added package manifests, source-only CI, support and acceptance documentation.
- Removed the development shell's dependency on a private local workspace.
- Rewrote the online server in C (`mpo-server`), built with the game and
  packaged beside Portable Ops+ for Linux and Windows.
- Reduced generated-code size and generation cost, and removed unnecessary
  host exports and framework-test builds in ports.
- Added minimal Windows FFmpeg cross-builds and independent module linking.
- Confirmed gameplay smoke tests for all four local candidate archives:
  PO and PO+ on Linux through `steam-run`, and Windows builds through Wine.
- Confirmed PO+ Windows ISO copy/import, restart without setup and save
  reload under Wine, using an initially empty test data directory.
- Licensed original port code under MIT and included its license in packages.
- Recorded candidate archive hashes, source/corpus fingerprints, dependency
  notices and loader checks separately from gameplay acceptance.
- Identified six unimplemented imports affecting date formatting, dialog
  cancellation, thread wakeups and access-point scanning. Only wakeup
  cancellation is confirmed reached in the existing Wine smoke; effects on
  gameplay remain unverified. See [import audit](docs/release-import-audit.md).

Preliminary prerelease. Native Windows and clean-machine setup, full
gameplay coverage and multiplayer acceptance remain pending. Hardware
requirements are not final. Original port code is MIT; game and third-party code
retain separate terms.
