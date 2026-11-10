# AGENTS.md

## Peace Walker backports

These rules apply to Fulton and every future Peace Walker feature backported
to Portable Ops Native.

- Every PW feature is off by default. Importing assets must not enable it.
  Normal PO play must work without a PW ISO, installation or imported assets.
- Asset import must accept either a user-supplied PW PSP ISO or a PW Master
  Collection installation. Neither source may require the other. Source
  formats must be translated into the same PO-compatible asset representation;
  gameplay must not have separate PSP and MC implementations.
- Store converted assets in the port's user data. Keep donor sources
  read-only and game assets out of source control and
  release packages. After import, playing must not require the original PW
  source to remain present or mounted.
- Give each feature its own persistent toggle in the Game tab. Enable a
  toggle only when that feature's required converted assets are present and
  validated. Show missing or unsupported assets with an import action or a
  clear explanation; missing PW data must not block normal PO startup or play.
- Keep feature selection separate from asset availability. Assets being
  installed is not consent to turn a feature on. If required assets disappear
  or fail validation, leave the feature inactive and keep PO playable.
- Environment variables may support developer experiments and diagnostics,
  but are not the normal player-facing feature or import workflow.
- Before calling a backport production-ready, verify a clean PO profile with
  no PW source or assets, default-off behavior after import, equivalent
  conversion from both sources, operation after removing the donor source,
  and Game-tab enable/disable behavior with valid and missing assets.

Fulton has an experimental Game-tab toggle and a Game-tab import that
converts a PW PSP disc image or Master Collection installation in the port
itself (`pw_slot.hpp`, `fulton_convert.hpp`, `fulton_audio_convert.hpp`;
`scripts/test-pw-slot.cpp` checks local sources). MC import includes the
confirmed soldier grunt and encodes Ogg effects as PSP ADPCM. PSP import
copies its confirmed grunt and voice unchanged alongside sequenced foleys.
MC's balloon uses 12 kHz to fit PO's existing 32 KiB bank. Donors load from memory in
every stage; no stage file is replaced. Both imports pass offline checks;
the complete MC bundle and its Game-tab import still need live validation.
