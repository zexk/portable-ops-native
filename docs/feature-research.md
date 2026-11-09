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

## 10. Rich presence

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

## 13. Achievements

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
