# Candidate import audit

Checked 2026-11-07 against candidate port `9dd16fc`, PortableKit `5a522cc`,
North American PO 1.02 (ULUS10202) and PO+ 1.01 (ULUS10290). This is a static
audit plus existing development logs, not a new gameplay test. No game code,
disc files or saves accompany this document.

## Identified gaps

These imports have no implementation in the candidate. Five are absent from
the NID table; `sceUtilityMsgDialogAbort` is named there but has no handler.
The fallback logs its first call and returns zero without performing the
requested operation.

| Library | NID | Function | Imported by surveyed modules |
| --- | --- | --- | --- |
| `sceRtc` | `0x27F98543` | `sceRtcFormatRFC3339LocalTime` | PO title, PO+ title and briefing |
| `ThreadManForUser` | `0xFCCFAD26` | `sceKernelCancelWakeupThread` | PO title, PO+ briefing |
| `sceUtility` | `0x4928BD96` | `sceUtilityMsgDialogAbort` | PO title, PO+ main executable |
| `sceNetApctl` | `0x04776994` | `sceNetApctlGetBSSDescEntryUser` | PO+ briefing |
| `sceNetApctl` | `0x6BDDCB8C` | `sceNetApctlGetBSSDescIDListUser` | PO+ briefing |
| `sceNetApctl` | `0xE9B2E5E6` | `sceNetApctlScanUser` | PO+ briefing |

Names and NIDs are confirmed by PSPSDK's
[RTC imports](https://github.com/pspdev/pspsdk/blob/master/src/rtc/sceRtc.S),
[thread imports](https://github.com/pspdev/pspsdk/blob/master/src/user/ThreadManForUser.S)
and [utility imports](https://github.com/pspdev/pspsdk/blob/master/src/utility/sceUtility.S),
and PPSSPP's public
[network API documentation](https://www.ppsspp.org/docs/development/adhoc/api/scenetinet/).
Only interface facts were used; no implementation code was copied.

## Relation to observed logs

The PO+ Wine development smoke reports one new unresolved import in `title`
and four in `briefing`. The title gap is the RTC formatter. Briefing adds the
three access-point functions and wakeup cancellation. Its RTC import already
has the fallback registered by title, so it is not counted again. Unresolved
counts describe newly registered fallbacks, not the total unsupported imports
in each module.

The same log reaches `sceKernelCancelWakeupThread` with `a0=0x0000016C` and
`ra=0x08B1AA64` after entering a later stage. This proves execution reached
the fallback. It does not establish whether pending wakeups existed or whether
returning zero affected gameplay. The other five calls are not observed in
that log; that does not prove they are unreachable.

PO's title has three gaps in the table, consistent with older development
logs reporting three unresolved imports there. Those older logs do not record
an exact source revision, so this is not a claim about their complete binding
state. PO+ also imports message-dialog abort from its main executable; that
fallback is installed at bootstrap, outside the stage-loader counts.

## Evidence and reproduction

Read stage PRXs from the selected disc's `PSP_GAME/USRDIR/STAGE` directories:
`title/title.prx` for both editions, `briefing/briefing.prx` for PO+. Analyze
them with PortableKit's `psp_analyze` at load base `0x08B72F00` for PO and
`0x08B10600` for PO+. The three analyses report zero invalid or unsupported
relocations, with 55, 43 and 36 imports respectively. Analyze both main
executables too, to distinguish imports already given a bootstrap fallback.

| Module | PRX SHA-256 | Relocations | Loaded code hash |
| --- | --- | ---: | --- |
| PO title | `f71020e7bbd09d0c9714f1d25333bdb4042800dd55e21a1fe8c4eb72d4f5b1f7` | 7467 | `69858283E4AAD72B` |
| PO+ title | `b35082d85b8b87f5efeb169a40560d109afd7c4e5f4759a595ffb87fb21aabf5` | 7348 | `D354E97164914C54` |
| PO+ briefing | `1f770730b84ce6ce2b951c3c249a7d82fc1d12f7eec1e4d7523eebae18503228` | 30080 | `CA3FEA8E956055C5` |

Loaded code hashes use `scripts/stage-images.py`'s `stage_code` with an empty
engine-relocation list, followed by `fnv1a64`. Both PO+ hashes exactly match
the initial module identities in the Wine log. Applying the stage's engine
relocations changes the hashes; all three resulting identities also match
the candidate's generated module corpus.

The runtime binding code and NID table are unchanged between the Wine build's
PortableKit revision `b43cc2b` and candidate revision `5a522cc`. Raw PRXs,
analyzer reports and full logs remain private. No runtime fix is claimed by
this audit; save dialogs, date formatting, thread wakeups and advertised
network modes still need targeted acceptance checks.
