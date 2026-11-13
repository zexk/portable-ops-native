# PO / PO+ feature crosscheck

Static audit on 2026-11-12, port revision `5aa29a5`. Targets: PO US
ULUS-10202 1.02 and PO+ US ULUS-10290 1.01. EU/JP are outside the
current release scope. Regional notes below are informational, not release
gates. No new gameplay acceptance performed.

## Missing features versus missing evidence

| Feature | PO | PO+ | Gap |
| --- | --- | --- | --- |
| Fulton and crouchwalking | Experimental, optional | Not installed or exposed | Actual feature gap, documented PO-only scope |
| Detail distance | LOD and foot shadow fade | LOD and US stage-local foot shadow fade | Offline checks pass; live PO+ verification pending |
| Analog stalking | Implemented, checked in game | Same implementation and toggle | PO+ player offsets and threshold behavior need gameplay verification |
| Camera, follow toggle, first-person look | Fixed engine hooks | Per-stage signature discovery | Follow toggle confirmed live in a US mission on 2026-11-13; input and look need gameplay checks |
| Widescreen and FOV | Installed | Installed for all three regions | Shared synthetic projection check does not validate region addresses |
| Interpolation | Shared thresholds | Shared thresholds | No missing profile wiring found |
| Startup logo/movie skips | Installed | Installed for all regions | Profile presence alone does not establish gameplay acceptance |
| Audio music/effects/voices | Installed | Installed | PO+ US bank mapping partly inferred; EU/JP bank ids explicitly unverified |
| Cutscene mix slider | Present | Absent | Intentional profile difference; not evidence of broken ordinary voice mixing |
| PO save recruitment import | Source game | US cross-game folder mapping | EU/JP have no cross-game mapping; shipped PO source profile is US only |
| Online server integration | Host/URL callbacks | Host/URL plus lobby mark | Full multiplayer acceptance remains open |
| Stage presence observer | Installed | Installed | Discord transport absent in both |

Sources: [PO profile](../profiles/po/host/po_profile.cpp),
[PO+ profile](../profiles/pop/host/pop_profile.cpp),
[feature research](feature-research.md), [README](../README.md).

## Highest-value test gaps

1. PO+ camera installation: `install_camera_in_module`,
   `install_camera_follow_in_module` and `install_look_in_module` silently
   skip zero or ambiguous signature matches. Existing
   [camera test](../scripts/test-camera.cpp) calls the fixed-address follow
   installer and tests shared math; it never calls these module installers.
   Test unique, missing and ambiguous matches, then replacement by a second
   stage. Validate signatures against actual playable stages in each region.
   A successful US hook log proves only that particular module matched.
2. PO+ analog stalking and detail distance: both appear in settings, but
   documented live verification belongs to PO. Check walking threshold,
   Triangle press/release, first-person controls and transitions; compare LOD
   at 100% and 400% after loading a new area.
3. PO+ audio: compare music, effects and voices independently, including new
   character banks and radio. Do not treat US bank-layout inference as EU/JP
   verification. The current classifier applies the same ids to all regions.
4. Save and network workflows: test PO-unit import on US, separate save/load
   for each edition, and a real two-client match with disconnect/rejoin.
   The [candidate import audit](release-import-audit.md) records missing
   RTC, wakeup cancellation, dialog abort and access-point operations.
   No implementations with those function names were found in the current
   PortableKit C++ tree; that name search is not a complete NID-binding audit.
5. Regional coverage: EU/JP compile success is not gameplay acceptance.
   Repeat startup, playable-stage controls, audio and save round-trip there.

## Evidence limits

The Linux RC1 evidence for [PO](release-evidence-po-linux-rc1.json) and
[PO+](release-evidence-pop-linux-rc1.json) refers to `9dd16fc`, not this audit's
revision. Both record brief user-reported smoke success, with full acceptance
checks still `NOT_TESTED`. They cannot certify later controls/audio changes.

No game inputs, memory writes, launches or restarts were performed. No
feature implementation was changed. This crosscheck separates missing wiring
from unverified behavior; it does not declare either edition release-ready.

## Follow-up checks

The camera test now exercises PO+ follow-hook discovery with relocated JAL
targets, absent and duplicate signatures, two successive module addresses,
and repeated installation. Built and passed in the project Nix shell on
2026-11-12. Input/look discovery, real stage coverage and shadow discovery
remain open. Multiplayer and settings acceptance follow those checks.

## PO+ shadow discovery, 2026-11-12

The earlier engine-only search missed stage-local contact-shadow code.
Read-only disassembly of the existing US linked-stage artifact
`mod08B10600_mgp_stage_C0B6D0A6DC8EF6D5.bin` found:

- Constructor at guest `0x08B2F7A8`, loading texture hash `0x7DF176` at
  `0x08B2F800`, allocating two primitives of type `0xB0206`, and storing
  them at work offsets `+0x68` and `+0x6C`.
- Wrapper at `0x08B2FAB4` allocates `0x90` bytes and registers update
  `0x08B2EDA0` and destructor `0x08B2F748`.
- Update computes camera distance, clamps it using floats at `0x08B44248`
  and `0x08B4424C`, then computes a fade using the first threshold.
  These are stage-local data references, not engine-global addresses.

Artifact SHA-256:
`aa639d8f34225c3474722cb7e874f8b60966478472e102823574d73bcc5b7b07`.
This identifies the inspected artifact, not independently verified disc
provenance. Stage name and source PRX identity still need confirmation.
The generated image contains code only; those float values lie outside it
and have not yet been read. Do not assume PO's 5000/10000 values or patch
these addresses in another stage. No runtime shadow change made yet.

Source verification completed against the US ISO: BOOT.BIN SHA-256 matches
profile `61eaaaa771e5dabcca07b6df62137e39709caa62030f5cc8ddc809e333a6943b`.
`gsmenu/gsmenu.prx` SHA-256 is
`07c8b9d922cdce1065d480280e64e9693e42ebf54e547b5ce11ff50fdd3615ca`.
Applying its PRX and engine relocations reproduces linked code identity
`C0B6D0A6DC8EF6D5`. The PRX data segment contains 5000.0 at guest
`0x08B44248`, 10000.0 at `0x08B4424C`, and 1.0 at `0x08B44250`.
This closes the source identity and default-value questions above for gsmenu;
other stages need their own resolved addresses.

Input and look module installers now also have passing synthetic checks for
missing/ambiguous/unique matches, repeated installation and replacement of
code at the same address. These check installer behavior, not gameplay or
actual US-stage signature coverage. The test callback stub deliberately
asserts if guest callback execution is attempted.

Existing US stage-image coverage: `out/test-camera out/stages-pop/*.bin`
passes over 128 images. 111 resolve all three camera signatures uniquely;
17 resolve none; no partial input/follow/look coverage found. This uses the
production C++ matcher and the profile's input signature, now shared from
`camera.hpp`. The test accepts optional images and rejects partial coverage.
All-three-absent is not proof of a non-gameplay stage; source-stage mapping
and expected coverage must still be checked before treating this as complete
acceptance. These are existing artifacts, not freshly regenerated corpora.


## Stage-local shadow implementation

US PO+ now resolves the fade constants from the relocated clamp/arithmetic
signature on module identification. It requires a unique match, consistent
adjacent references and expected float values before changing them. Missing
stages clear the old address. Re-identification does not compound scaling.
Both initial stage load and later LOD registration apply Detail distance.
EU/JP remain outside this release scope and do not enable this new path.

`test-shadow.cpp` passes synthetic checks for signed address reconstruction,
400% scaling, repeated identification, returning to 100%, missing/duplicate
signatures, inconsistent references and invalid constants. Fresh extraction
from the hash-verified US ISO also passes production discovery/scaling for:

| Stage | Linked code identity | Fade start guest address |
| --- | --- | --- |
| gsmenu | C0B6D0A6DC8EF6D5 | 0x08B44248 |
| n01 | 9A6269646FE4EC6F | 0x08BFF764 |
| bossrush01pt | 26A86E37D128DC96 | 0x08C14254 |
| briefing | 34174714B66A90B2 | 0x08C0FB80 |

All four use 5000/10000 defaults and yield 20000/40000 at 400%. The three
playable-context samples also match all camera signatures; gsmenu matches
none. The named PortableOpsPlusNative target builds successfully. No live
visual result is claimed.

The 17 existing images without camera matches map to cyberval2, addscvr,
init, trade, r_main01, tr_ending, title, editmsg, train_select, gsmenu,
ntconnect, frvsmenu, gsclimenu, gpstest, browser, apscan and ntmenu2.
This is a build-log mapping, not proof that every context is non-playable.

Regression follow-up: camera tests now cover analog-stalk threshold edges,
real Triangle holds, preservation of button edges, game-written held changes
and disabled behavior. The shadow test also drives the LOD wrapper through
its guest-return continuation: squared-distance scaling, repeated
registration, untouched records, 100% reset, return value and shadow refresh
all pass. Both named US executable targets build.

Fresh-disc sample checks are reproducible with
`scripts/check-pop-stage-hooks.py DISC.iso` after building `out/test-camera`
and `out/test-shadow`. It checks the US BOOT hash, extracts four stages to a
temporary directory, verifies expected camera coverage and runs the production
shadow resolver against their scalar data. No game launch or corpus rebuild.

Live testing is pending. User selected two local clients plus a local server.
Separate copied profiles and accounts are prepared under the ignored output
folder; original saves/settings are untouched. Acceptance requires both
clients to join a match, exchange movement/actions, end the round and
leave/rejoin, followed by settings checks (shadow/LOD at 100% and 400% after
area reload; stalking; camera follow/recentre/first person; audio channels;
startup skips and persistence). Local success will not prove Internet NAT
traversal or remote SaveMGO compatibility.

Live test start: both isolated clients launched at the user's explicit
request. Logs confirm logo/movie skips and successful loading of each copied
slot-1 save (201132 decrypted bytes). Both processes remain running. Server
has two test accounts and listens on the expected gate/account/lobby/STUN/web
ports, but no client login is recorded yet. These observations establish
startup/save-read execution only, not menu behavior, save round-trip,
multiplayer or visual settings acceptance.
