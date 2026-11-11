# Remaining feature work: static findings

This pass uses framework baseline `699404d` and PO ULUS-10202 1.02's
decrypted executable, SHA-256
`ca700ea516a98949763410da537a75a08f9723718fab302bcd7ef1e6185cadf8`.
Guest addresses equal ELF addresses plus `0x08804000`. PO+ needs its own
verified addresses; the existing profile already separates its regions.
Static candidates below are not live-confirmed feature hooks.

## 1. Rumble

The renderer already exposes its selected controller through
`VulkanRenderer::gamepad()`. Controller lifetime is owned by its open/close
and hot-plug paths in
[`vulkan_renderer.cpp`](../portablekit/host/gpu/vulkan_renderer.cpp).
There is no existing rumble event mapping in the profile.

The engine's action setter is a useful RE starting point: PO ELF `0xCB58C`,
with actor index in `a0` and action numbers through `t3`, terminated by -1.
The local actor must be identified through the actor table, not assumed to
have index zero. Its 793 direct engine call candidates include 733 with a
locally constant first action number. The build-aware action scanner is
documented in [engine feature hooks](../../docs/portable-ops/feature-hooks.md).
It includes delay-slot arguments and leaves uncertain registers unknown.
Sampled caller bodies contain repeated state and motion requests as well as
transitions. Their frequency alone does not distinguish a confirmed shot
from holding an aim or firing state; live event correlation is still needed.

Next static step: classify the containing functions for damage application,
projectile creation and CQC transitions. A flag set each frame is not a new
event each frame; a firing flag alone can also miss individual automatic
shots. Device output should follow confirmed event boundaries, stop when
paused/disconnected, and remain optional. Motor strength still needs a
controller check.

## 2. Controller prompts

Premise unverified: no gameplay evidence yet establishes controller button
glyphs in PO or its online mode. Inspected shared init fonts, codec icons,
briefing bottom-menu images and one mission HUD pack show text and menu/HUD
art, without identified controller glyphs. This does not prove their absence
elsewhere. Confirm an actual prompt before pursuing controller replacements.
The layout work remains relevant to HUD classification and scaling.

Framework menu hints already select controller labels using
`SDL_GetGamepadType` in
[`widgets.cpp`](../portablekit/host/ui/widgets.cpp). The remaining item is
the game's own prompt textures. Existing texture-pack replacement and dump
paths can supply replacements once the atlas and rectangles are identified.

A scan of the named PO stage archives found layout candidates such as
`lay_codecicon.la2`, `lay_itemlist.la2`, `lay_property.la2` and
`lay_bottommenu.la2`. It did not establish a dedicated button texture pack;
button artwork remains unidentified. A global atlas swap could replace text
or unrelated icons alongside any buttons.

Follow-up static pass resolved the container and texture-reference tables:
all 5,138 references in 311 layouts across 100 PO stages match their QAR
resource name plus texture hash. The scanner and format are documented in
[layout texture references](../../docs/portable-ops/feature-hooks.md#layout-texture-references-and-qar-resources).
Briefing bottom-menu and item-list resources are now located, including their
texture records and UV words. Packed image descriptors are preserved rather
than guessed as plain dimensions. Specific controller glyphs and layout
node transforms remain unidentified. Replacement work is deferred until an
actual gameplay prompt is confirmed.

## 4. Removed: cutscene and codec skipping

The user reports that holding Triangle already provides the intended
fast-forward behavior. No additional feature or static skip-path research
is needed for this item. End-credit behavior is unverified and does not
justify a separate feature without a concrete missing behavior.

## 5. Startup skips

PO has Game > Startup toggles for the logo screens and the opening movie
([`boot_skip.hpp`](../profiles/po/host/boot_skip.hpp); the title scenario's
flow is in [stage identity](../../docs/portable-ops/feature-hooks.md#title-startup-flow)).
Bounded boots with the two environment overrides: the title actor starts
1 s into the title stage instead of 13 s with the logos skipped, and with
the movie skipped a captured frame shows the title menu, where the
unskipped run shows the movie's Press Start screen. The toggles themselves
(drawing, saving) are compiled but not exercised, nor is a return to the
title from a game. PO+ has its own title module and is not covered.

## 6. Save states and quick resume

Guest RAM alone omits state that affects the next instruction or frame:

- [`Kernel`](../portablekit/host/kernel/kernel.hpp): thread contexts, waits,
  semaphores, callbacks, virtual time and pending guest calls. Guest-return
  and host-wait callbacks contain `std::function` closures; these cannot be
  persisted as raw object bytes.
- [`hle_io.cpp`](../portablekit/host/hle/hle_io.cpp): open descriptor IDs,
  paths, positions, directories, async completion and file filters. Host
  `fstream` and shared filter objects need explicit reconstruction.
- [`SasCore`](../portablekit/host/audio/sas_core.hpp): voice decoder history,
  playback position and envelopes. ATRAC and AVC decoders also own opaque
  FFmpeg state; queued host audio must not survive a restore unchanged.
- Renderer: GE state, render targets/depth, write-back ownership, textures
  and in-flight fences. Current screenshot capture is not a renderer state
  snapshot. Interpolation history must be invalidated on restore.
- Runtime/profile: loaded modules, heap ownership, hook-owned state and
  pending continuations. An outer dispatcher boundary removes the native
  stack problem but does not serialize callback closures.

First implementation should be a versioned offline snapshot at a supported
quiescent boundary, with explicit subsystem capture/restore. Active network
sessions and in-flight file writes are unsuitable starting points. Rewind
additionally needs a policy for disk and other external side effects; it
does not become correct from a RAM ring buffer alone.

## 7. MSAA and anisotropic filtering

Already implemented: settings `video.msaa` / `video.anisotropy`, Video menu
rows and renderer support. Both apply after restart. This entry was stale.

## 8. HUD scaling and safe areas

The framework's Android safe-area/content rectangle handles the whole
picture; it is not independent game-HUD scaling. PO's prior live trace
classified the item HUD as through-mode sprites.

The draw path already expands sprites into `GpuVertex` positions before
upload. That is a possible adjustment boundary, but through-mode also
contains full-screen backgrounds, fades and render-to-texture work. Scaling
all through-mode vertices would change more than the HUD. Identify HUD
layout/atlas classes first; transform their positions, anchoring and scissor
rectangles together, preserving UVs and the 3D viewport. Game-specific
classification belongs in the profile.

Follow-up static RE found PO's LA2 constructors and runtime node tables:
layout objects are `0x120` bytes, node count is at `+0x54`, and the pointer
table at `+0x60` uses indices starting at 1. The file-node parser now checks
all 32,291 nodes in 311 layouts, including their parent references. Layout tuple/rectangle setters and dirty flags are located, but their full
consumer-side transform behavior and live HUD ownership remain unverified.
See [LA2 construction](../../docs/portable-ops/feature-hooks.md#la2-construction-and-node-records).
This provides a narrower engine-level research path than changing every
through-mode vertex; it does not yet establish that all HUD elements use LA2.

The consumer side is now traced to the layout's render command list. The
rect at `+0xFC` sets GE scissor, viewport and an orthographic mapping, so
layout units are PSP pixels clipped to that rect. The tuple at `+0x114` is
`(tx, ty, angle, scale)`: translation in 1/16 pixel, `0x1000` per turn and
8.8 scale. It is a per-layout affine applied by the engine before vertices
reach the GPU path, rewritten only when its dirty flag is set. Scaling a
known HUD layout about a screen anchor needs no vertex classification, but
still moves that layout's backgrounds and text and is clipped by the rect.
See [LA2 render commands](../../docs/portable-ops/feature-hooks.md#la2-render-commands).

HUD layouts are now named statically. The engine builds `lay_life_stmn`,
`lay_radar`, `lay_itemwin`, `lay_wpnwin` and `lay_itemchange` from the
resident archive, mostly through layout actor factory `0xA9F18`; weapon
sights (`scope`, `dragunov`, `rpg7`, `gl`, `moshinnagan`) are separate
full-screen layouts that must stay unscaled. A profile hook can match the
factory's name hash and set that layout's tuple, leaving through-mode
vertices alone. Stage-built `lay_msg`, `lay_call`, `lay_codecfooter` and
`lay_fulton` need the same treatment from stage code. See
[engine-built layouts](../../docs/portable-ops/feature-hooks.md#engine-built-layouts).
A live read in `st01` confirmed these HUD layouts at identity tuple and
full-screen rect. A live write of the tuple plus dirty flag scaled the radar
and life gauges to 0.5 about their corners; the life layout's name text
is drawn outside every layout (translation tests), so it needs a separate
boundary.


An Interface size setting was built on this boundary and verified live
(tuple written at layout creation; a per-frame variant corrupted the heap by
writing into layouts freed on an area change), then dropped: the HUD already
scales with the picture at any resolution, Fill already scales it to the
window, and shrinking it is taste rather than a fix.
## 10. Rich presence

Discord rich presence is planned for a later release.

Existing ad hoc diagnostics expose group, nickname and connection state:
[`client.hpp`](../portablekit/host/adhoc/client.hpp) and
[`session.hpp`](../portablekit/host/adhoc/session.hpp). These can describe
network activity without guessing it from arbitrary RAM.

The stage module's shared name is `mgp_stage`; module-name callbacks alone
cannot distinguish stages. A stage asset path or a verified engine stage ID
must supply that identity. Existing file-open wrappers demonstrate an
observation boundary; a presence observer must chain the previous handler
and preserve any installed Fulton file filter.

Separate a small, local state provider from any optional Discord transport.
Follow-up static RE located a more direct PO boundary: `0x39A20(name)`
receives the requested stage name, with wrapper `0x3B08(name)`. Loader
`0x396E4` case 5 reaches stage startup `0x1E1C` after pending work drains.
Observe request and initialization separately; neither alone proves mission
completion or ordinary gameplay. See
[stage identity](../../docs/portable-ops/feature-hooks.md#stage-identity-for-observers).
This can avoid an additional file-open wrapper for PO. PO+ US 1.01 equivalents
are now mapped statically: request `0x41498`, wrapper `0x453C`, loader
`0x4115C` and startup `0x20B8`; EU and JP are in the same table. Human
mission names and application integration still need mapping.

A local state provider now exists: [`presence.hpp`](../common/host/presence.hpp)
wraps the stage request in PO and all three PO+ editions and keeps the
last requested stage name. A bounded PO boot logged `init`, `r_main01` and
`title` in that order, and so did PO+ US; EU and JP are compiled only. Area
names come from the
twenty-entry table every scenario carries; that entry N names `st<N+1>`
is inferred from the online maps and confirmed in game for one area, `st01t2`
as the Communications Base, on 2026-11-11 (see the stage
identity section linked above). Nothing consumes the state yet: a Discord
transport needs an application id and an opt-in setting.

## 12. Draw distance, shadows and LOD

Static view findings are in
[engine feature hooks](../../docs/portable-ops/feature-hooks.md#view-limits).
PO setter `0x40FBC` stores near/far distances in view `+0x1C0/+0x1C4`;
initializer `0x41510` supplies 300/150000 units. The main projection and clip
planes use those values. Another projection at view `+0x360` uses near
distance `+0x3A0` (default 50) and a separate far constant (150000). Rigid
and skinned objects with flag `0x80000` at `+0x5C` use this alternate
projection, so one field cannot describe all passes.

Current model traces already observe model pointers, part masks and flags
with camera distance. A conservative experiment would modify the main
view's far limit around the builder, then restore the source field, as the
FOV hook restores its temporary changes. Scene visibility lists, LOD
selection, fog and shadows remain separate controls. Renderer `NO_CULL`
only disables GPU back-face culling; it cannot restore objects the engine
never submitted. Shadow improvement also needs to distinguish shadow
geometry/projection from texture resolution before choosing a control.

Static [contact-shadow research](../../docs/portable-ops/shadows.md) now maps
the two-quad component: constructor `0x150104`, update `0x14F70C`, texture
hash `0x7DF176` (32 by 32 indexed footprint), reverse-subtract blending,
5000-to-10000-unit distance fade and height-dependent spread/opacity.
Per-component opacity and size controls exist; the shared GE blend helper
also serves unrelated effects. Bone identities, contact-vector provenance
and other shadow paths remain open. Live checks are on that page; the only
runtime change is the fade range under Detail distance (item 12).

Follow-up static RE mapped the rejection pipeline in
[model rejection and part masks](../../docs/portable-ops/feature-hooks.md#model-rejection-and-part-masks).
Object `+0x6E` receives an eight-corner clip result from `0x4A7C4`; result 2
is rejected before draw. A separate visibility intersection uses object
`+0x60` and view `+0x1E8`. Hidden-bone masks at `+0xF0/+0xF4` have named-bone
set/clear helpers and hierarchy propagation, so globally clearing them is
not a justified LOD override. Rigid parts also have separate clip results.
No scalar distance-based LOD threshold has been verified in these paths.

A separate distance LOD switch exists: per-actor squared thresholds from a
64-entry table at `0x20D650`, filled by GCL command `0xC8C4F3` (handler
`0x15324`) in 68 story stages. A live read in `st01` found two records:
`ene_def` and `ind_def` swap to `ene_lod`/`ind_lod` beyond 7000 units. A
profile can raise those distances at registration `0x15210` to keep full
soldier models further out; a live write of the per-actor threshold
switched soldiers to their full models. See
[distance LOD table](../../docs/portable-ops/feature-hooks.md#distance-lod-table).


**Implemented** ([`detail_distance.hpp`](../profiles/po/host/detail_distance.hpp)):
Video's Detail distance (100-400%) wraps GCL handler `0x15324` and scales the
squared distances of the records that call wrote. The per-actor update
`0x15C48` cannot be wrapped: every caller is in its own generated unit, where
calls compile to direct jumps. Applies from the next area loaded. Live: 300%
gave 21000/36000 in place of 7000/12000. The same setting scales the foot
shadow fade (floats `0x1EA680`/`0x1EA684`, 5000 and 10000 units, used only
by update `0x14F70C`), written at the same moment. Live, 2026-11-11: 400%
read back 20000/40000 and soldiers up to 39000 units away kept shadows.
## 13. Achievements

Achievements and RetroAchievements support are planned for the next release.
Save states and quick resume remain later work.

PO GCL has global and link storage plus a variable assignment boundary at
ELF `0x3EEA8`. Offsets need semantic identification before they can become
unlock conditions. Known recruitment evidence is stronger: permanent roster
insertion is `0x2EA70`; pending append `0x35D88` can be rejected or discarded
before admission, so it must not award permanent-recruit achievements.

Use confirmed game events and read-only state checks. Loading an old save
must initialize the observation baseline rather than emit every existing
flag as a new event. Achievement definitions, persistence and remaining
mission/unlock flags still need work.

Follow-up static RE separated script snapshots from disk saves. Command
`0x5B316E` at engine `0x8EA4` copies link/global banks into RAM snapshots,
or updates selected snapshot variables. Inverse copy `0x3E8D8` restores them
without passing through assignment hook `0x3EEA8`. Any progress observer must
rebaseline at restore/initialization boundaries; command names alone do not
prove a save or unlock. Bank sizes and addresses are recorded in
[script-bank snapshots](../../docs/portable-ops/gcl.md#script-bank-snapshots).

The existing GCX decompiler produced partial `ending`/`st20` listings but
terminated with `unpack requires a buffer of 1 bytes`. Those listings were
used only to locate candidates; no completion-flag meaning was accepted from
them. Native copy paths and initializers independently establish the bank
spans. Flag semantics still need stronger script/control-flow evidence or
live correlation.

The disk save path is in the `title`/`briefing`/`ending` stage modules: a
GCL-driven savedata actor saves a `0xA610`-byte buffer, the size of the two
snapshot banks plus 4 bytes. A live briefing autosave confirmed it: the
saved buffer is a header word followed by exactly the link and global
snapshot banks. Achievements can therefore be driven from snapshot diffs at
`0x3E73C` (the copy made before a save) instead of every assignment; naming
the individual flags is the remaining work. See [disk save](../../docs/portable-ops/gcl.md#disk-save).

## 14. Through-mode interpolation

[`mark_eligible`](../portablekit/host/gpu/frame_interpolation.cpp) requires
perspective, or orthographic plus transformed vertices. Through-mode draws
fail both. The replay path iterates older-frame groups and ordinarily
replays unmatched HUD groups unchanged. `GpuVertex::w` and normal fields
also carry sprite UV-clamp bounds; blending the entire vertex structure
would corrupt texture sampling.

The static implementation boundary is explicit: classify displayed-target,
non-clear through draws; retain decoded positions; match compatible groups;
blend x/y in replay scratch storage. Match guards must include topology,
UVs, texture identity and scissor, not only recycled guest vertex addresses.
Reject ambiguous or discontinuous pairs and preserve draw order, depth and
off-screen render targets. Newly appearing glyphs should not slide from a
previous glyph's position. This needs a new vertex path, not the existing
orthographic option. A separate newest-HUD compositing experiment could
reduce UI delay, but cannot reorder arbitrary screen-space effects safely.

Implemented on 2026-11-10 as `CutThresholds::through`, on for PO and PO+,
off with `MGSPO_INTERPOLATION_NO_THROUGH`. Through-mode draws keep a CPU
copy of their vertices; a draw call pairs with the next frame's call of
the same pipeline, texture, palette, viewport, scissor, blend, push
constants and size when both frames have as many of those. Lighting
offsets are left out of the comparison: they lie in each frame's own
buffer region and rejected every pair at first. Position and vertex
colour blend when every vertex keeps its UVs and moves at most 48 pixels.
In a mission trace about 94 percent of roughly 51 sprite draw calls a
frame paired, and up to 8 moved per present. The user confirmed the
weapon wheel's fade as better. Blending frame pairs without 3D was tried
and removed: a replay draws only draw calls into the displayed
framebuffer, and the splash screens came out dark and flickering, so
full-screen 2D screens stay at 30. No pixel comparison was run.

## 15. Interpolation latency

[`analyze-presentation-delay.cpp`](../scripts/analyze-presentation-delay.cpp)
drives the existing `PresentClock` with deterministic 30 and 20 fps source
cadences, 3 ms of work and the clock's existing work margin. It needs no
window, game, GPU or sleep:

```sh
c++ -std=c++20 -Iportablekit/host scripts/analyze-presentation-delay.cpp \
    portablekit/host/gpu/frame_pacing.cpp -o out/analyze-presentation-delay
out/analyze-presentation-delay
```

| Source cadence | Presents | Buffer | Work allowance | Total modelled delay |
| --- | --- | ---: | ---: | ---: |
| 30 fps | 60 fps | 16.683 ms | 4 ms | 20.683 ms |
| 30 fps | 144 fps | 31.976 ms | 4 ms | 35.976 ms |
| 20 fps | 60 fps | 33.366 ms | 4 ms | 37.366 ms |
| 20 fps | 144 fps | 48.659 ms | 4 ms | 52.659 ms |

Interpolation off bypasses the present clock. These are model results,
not physical button-to-screen measurements. The existing live comparison
found similar mapped-button-to-motion timing with interpolation on/off.

The grid delay is `frame_us - first_step_us + work_us`: at fractional
present/source ratios the smallest grid step can be very short, forcing
nearly a full source-frame delay. Reducing the work allowance alone cannot
remove that term. A 60 fps cap is an existing lower-delay comparison at both
source cadences. Further work must choose a tradeoff: less uniform pacing,
newest-frame/UI presentation, or extrapolation with additional artifact
guards. No default pacing policy was changed by this static pass.


**Cause.** Interpolating to a grid moment needs the frame after it, so each
present waits `frame - first step + code time`, where the first step is the
smallest offset of a grid moment into its frame. That is at least half a
frame (60) and grows with the rate: at a display reporting 143.98 Hz the grid
never quite repeats against 30 fps frames, the first step falls to tens of
microseconds and the wait is a whole game frame, about 33 ms on top of the
code time, against none without interpolation.

**Prediction** (PortableKit `PresentClock::set_lead`, Video > Prediction,
`video.interpolation_lead`): presents come up to that fraction of the wait
earlier, and grid moments whose next frame is not there yet are shown by
carrying the newest frame on (blend factor above 1): the newer frame is
replayed with each matched draw continued from its older partner along the
same blends, unmatched draws follow the camera, flipbooks hold the newer
cell. Each present still shows its own grid moment, so pacing stays even;
the cost is a brief overshoot when motion stops or turns. The lead is taken
out to the next grid offset that needs the next frame, so at 60 any lead
removes the whole wait. Model (3 ms code, 1 ms margin), total delay:

| Present rate | Lead 0 | Lead 50% | Lead 100% |
| --- | ---: | ---: | ---: |
| 60 | 20.7 ms | 4.0 ms | 4.0 ms |
| 120 | 29.0 ms | 12.3 ms | 4.0 ms |
| 143.98 | 37.3 ms | 20.7 ms | 4.0 ms |

`CHECK_REPLAY` now also replays the newer frame through the carrying path at
a factor just above 1 and counts pixels differing from its own picture.
Live, PO NA 1.02 at 144 Hz, 2026-11-11: the full lead carried a 20 fps frame
up to 48 ms on and juddered with every change of motion, though pacing and
the replay checks were clean (camera turn deltas change by up to 1.3 degrees
a frame). Carrying is now capped at one vblank (`kMaxCarryUs`, PortableKit
97fc08d): at 100% the lead reads 16.7 ms, the delay about 36 ms against 61
at 0%, and the user reports no judder. The table above predates the cap.
Physical button-to-screen latency is still unmeasured.

**A frame held by the game itself** (2026-11-10, ULUS-10202 1.02,
`MGSPO_TRACE_PACING`). Apart from interpolation, the game's main loop does
all its work in the first 2 ms after a vblank: flip to the buffer drawn
last frame, draw the other buffer, read the pad, `sceGeDrawSync`, then
wait (3 vblanks in play, 2 in menus). A finished picture is therefore
shown one whole game frame after it was drawn: 50 ms in play, 33 ms in
menus. Game > Show frames early (`video.present_at_draw_sync`, or
`MGSPO_PRESENT_AT_DRAW_SYNC=1`; PortableKit, off by default) shows
it at the draw sync; the trace confirms one early present per frame and
the user reports snappier input, most in menus. The pad is read after the
draw, so input still waits one frame to be drawn.

**That frame, without changing the game** (Game > Read the controller
late, `video.pace_at_pad_read` or `MGSPO_PACE_AT_PAD_READ=1`, with
Show frames early; PortableKit, off by default). In real time the loop is
"read the pad, update, wait about 48 ms, flip, draw": the wait sits
between the read and the picture made from it. Returning from the vblank
waits at once does not work, the game loops on its own vblank counter.
Instead emulated time runs ahead of real time through those waits and the
next pad read shows the frame just drawn and waits for real time there.
Trace: speed 100%, one early present per frame right before the read. The
user reports snappier input with sound and speed unchanged.

**Frame grid phase.** The interpolator's trace showed frames arriving 19
to 30 ms after the moment they were stamped with ("code time"), which it
adds as delay and which made Prediction judder. The frame-moment grid
assumed two vblanks a frame and stayed one vblank early once the cadence
shifted; the same happened without the new modes in 20 fps play (code
time 12 ms against 2 ms in menus, the "14 ms" of earlier notes). Two flips
the same distance apart now re-anchor the grid. After the fix code time
in 30 fps menus is 2 to 5 ms and the user reports no judder. Early frames
are stamped with their own vblank and skip the grid.

The 16 ms of code time that remains in 20 fps play is real: with real
time in the pacing trace, one frame takes about 11 ms from the pad read
to the finished picture (update about 5 ms, display lists about 4 ms),
against 2 to 3 ms in menus. The pad's age at the finished picture went
from two game frames (100 ms in play) to that work time. Physical latency
remains unmeasured.

**Camera sampling with controller pacing** (2026-11-10, ULUS-10202
1.02, Linux). With both response settings enabled, the host camera now
samples input and applies its turn after the real-time wait at the
controller read, before the game's next update. Previously it applied
the previous turn at presentation, before that wait. The normal path
keeps its camera step at presentation. The user reports the camera and
other behavior look good; Prediction still introduces judder.

The pacing trace now separates display-list and intervening-present
time. In 692 gameplay frames, pad-to-picture time was 10.1 ms median,
12.1 ms p90 and 14.6 ms p99. Mean 9.7 ms comprised 3.9 ms display
lists, 0.4 ms intervening presents and 5.3 ms other work. Steady gameplay
held about 144 presents/s at normal speed, with no skipped presents in
the 100% Prediction section. Prediction corrections remain a suspected
cause of judder, not established by this trace. Physical latency,
other games and other platforms remain unverified.

## 16. Camera follows behind

PO ULUS-10202 1.02, 2026-11-10. The previous hook at the swing helper
(`0x10E1DC`, relative to the image) was bypassed by generated same-unit
calls. The hook now wraps the camera actor's update (`0x10D304`), setting
the actor's follow rule at `+0x68` to zero while the update runs when
Controls > Camera follows behind is off. It restores the stage's rule
afterwards; manual camera input and the recentre button still run through
the game's camera code.

PO+ discovers the update entry in stage modules instead of the swing
helper. The shape matched uniquely in all eight local US 1.01 module
dumps containing the old swing signature; six other US dumps and the
local EU/JP dumps matched neither signature. Follow toggling, manual
orbit, recentre and rule restoration pass the stand-in runtime check in
`scripts/test-camera.cpp`. The user confirmed the fix works in PO on
Linux with both response settings enabled. PO+ and other platforms remain
unverified live.

## 17. Thermal and night-vision goggles

PO ULUS-10202 1.02, Linux, 2026-11-10. Thermal produced a black image and
night vision flashed black and white. Both paths sample rendered buffers
as unswizzled CLUT16 textures, including screen buffers `0x04000000` and
`0x04044000` and scratch buffer `0x04088000`. The renderer previously
rejected indexed framebuffer views and decoded stale guest memory.

PortableKit now samples matching CLUT16/CLUT32 framebuffer views on the
GPU, reconstructs packed source pixels and applies the palette's shift,
mask and bank. Palette lookup precedes linear filtering. Palettes use
the existing texture cache and remain bound during interpolation replay.
`MGSPO_NO_FB_PALETTES=1` restores the previous path for comparison.

The user confirmed both effects render with the fix, but reported poor
visibility. Original-rendering contrast and brightness remain unverified;
neither was tuned. The trace maintained about 144 presents/s at normal
game speed during steady play, with no renderer errors logged. PO build,
renderer helper, texture-pack and interpolation tests pass. Other games,
platforms, swizzled framebuffer views and CLUT4/CLUT8 framebuffer views
remain unverified or unsupported by the new path.

## 18. L plus D-pad movement with stick swapping

PO ULUS-10202 1.02, 2026-11-10. Holding L plus D-pad left or right caused
an animation loop with slow sliding. The user reproduced it on the release
build and confirmed disabling Controls > Swap sticks in first person
stopped it. The hook treated action flag `0x16` as sufficient to swap, but
the game's locomotion code also sets it outside first person for native
D-pad movement. First-person state (`0xBA` or `7`) is now checked before
`0x16`, retaining the game's other stick-selection conditions.

`scripts/test-camera.cpp` checks native D-pad movement stays unswapped,
first-person swapping still activates, and native control settings and
special-state exceptions remain respected. The user confirmed the fix on
Linux with the regular save and stick swapping enabled.
