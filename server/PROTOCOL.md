# Portable Ops online protocol

What the game (PO+ US, ULUS10290) speaks to its servers, as observed from our
own client with `MGSPOP_NET_CAPTURE` and read from the game's code. EBOOT
addresses are ELF virtual addresses (add 0x08804000 for guest memory).
`server/decode.py` decodes a capture.

## Servers

The client resolves Konami's names (SaveMGO's redirections in
`common/host/savempo.hpp`) and then follows the gate's server list:

| Port | Role | Notes |
|---|---|---|
| TCP 5731 | gate | server list, news |
| TCP 5732 | account | login |
| TCP 5734 | lobby | lobby, games, rankings |
| UDP 3478 | STUN | `mpostun{us,eu,jp}` |
| HTTP 80 | web | registration, rankings, text files |
| UDP (peer) | match | player to player, not through the server |

## Framing (TCP)

Each message is masked by XOR with the repeating key `5a 70 85 af`, starting
over at the message's first byte (the key is stored at EBOOT 0x17f754; the
loop is at 0x142284). Unmasked, it is a 24-byte big-endian header and the
payload:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | command |
| 2 | 2 | payload length |
| 4 | 4 | sequence, counted per direction from 1 |
| 8 | 16 | HMAC-MD5 of bytes 0-7 and the payload |

The HMAC key is the 16-byte ASCII string `Z7/biJ46TzGF-8yx` (EBOOT 0x16da30;
computed at 0x1421ec by 0x142d10). A reply's command is usually the request's
plus one.

Some payloads are Blowfish-encrypted (ECB, each 8-byte block as two big-endian
words; key: 56 bytes at EBOOT 0x16d998; 0x142b7c, mode 0 encrypts and 1
decrypts). `server/mpo_server.c` implements it.

## Gate (5731)

| Command | Dir | Payload |
|---|---|---|
| 2805 | C→S | empty: hello |
| 2802 | S→C | u32 0 |
| 2803 | S→C | server list, 160 bytes: 49-byte entries {u32 index, u32 id, name[16], address[15] (ASCII), u16 port, u16 1, u16 type (1 gate, 2 account, 4 lobby), 4 zero}: GATE 5731, ACCOUNT 5732, SOLID_SNAKE (lobby) 5734 |
| 2804 | S→C | u32 0: end of list |
| 2008 | C→S | u8 (8 seen) |
| 2009 | S→C | u32 0 |
| 200a | S→C | one news item: u32 index (counting down), u8 flag, date "YYYY-MM-DD hh:mm:ss", title, body |
| 200b | S→C | u32 0: end of news |
| 0003 | C→S | empty: goodbye |

## Account (5732)

| Command | Dir | Payload |
|---|---|---|
| 3001 | C→S | empty |
| 3002 | S→C | 16 bytes: challenge |
| 3003 | C→S | Blowfish(login name, 16 bytes zero-padded, then HMAC-MD5(key, login name + challenge + password)) (built at 0x135d84) |
| 3004 | S→C | u32 result (0 = ok), session token: 16 ASCII characters |
| 3840 | C→S | empty |
| 3841 | S→C | u32 0, u32 user id, player name[16], ... (32 bytes) |
| 3042 | C→S | u8 (8 seen) |
| 3043 | S→C | u32 0 |
| 3045 | S→C | u32 0 |
| 0003 | C→S | empty: goodbye |

The challenge's place in the HMAC is read from the code but not yet confirmed
against a known password.

## Lobby (5734)

| Command | Dir | Payload |
|---|---|---|
| 0005 | both | empty: keepalive, about every 50 s from the client; the server answers |
| 3003 | C→S | join, 24 bytes, Blowfish (carries the session token; user id gets 0x10000000 under SaveMGO, see savempo.hpp) |
| 3004 | S→C | 256 bytes: u32 result, session token, then the player's records ("RECORDING", rank data) |
| 4900 | C→S | empty: lobby information |
| 4901 | S→C | 1024 bytes: u32 user id, player name[16], ..., "emblem", stats |
| 4904, 4905 | S→C | 700 bytes each, three of each (u32 page 0-2 at the start): lists, contents not yet understood |
| 4700 | C→S | 22 bytes: u16, LAN address[16], u16 game port, u16 |
| 4701 | S→C | u32 0 |
| 4702 | C→S | u16 status (2, 3 seen) |
| 4703 | S→C | u32 0 |
| 4b00 | C→S | game list request (sent at 0x13aa98) |
| 4b01 | S→C | u32 result, 0 starts the list (0x13ab34) |
| 4b02 | S→C | games, 58 bytes each, read until the message ends (0x13ac24); see below |
| 4b03 | S→C | u32 result: end of list (0x13aed0) |
| 4b10 | C→S | create a game: Blowfish(settings), 320 bytes (0x13c6a4); see below |
| 4b11 | S→C | u32 result (0x13ca80) |
| 4b12 | C→S | u32 game id: game details |
| 4b13 | S→C | u32 result, then the game (0x13d1f8); see below |
| 4316 | C→S | empty, after 4b11 (0x13d7ec) |
| 4317 | S→C | u32 result |
| 4344 | C→S | the host, about a player: u32 user id, u8 (0x13e210) |
| 4345 | S→C | u32 result, u32 user id (0x13e320) |
| 4398 | C→S | the host, periodically: u32, then (u32 user id?, u32) pairs (0x13f650) |
| 4399 | S→C | u32 result |
| 4902 | C→S | u32 user id: player details |
| 4903 | S→C | 512 bytes: u64 user id, name[16], emblem, stats; then three 4904 and 4905 pages as after 4901, which a joining client waits for before it joins |
| 4320 | C→S | join game, Blowfish(u32 game id, password[16], 4 zero) (0x13d960) |
| 4321 | S→C | u32 result; if 0, host public address[16] + u16 port, host LAN address[16] + u16 port (0x13daa0). The joiner then sends to the public address alone, about eight UDP packets over 15 s, so a joiner behind the host's own address needs the LAN address there |
| 4322 | C→S | empty: the joiner gave up reaching the host (0x13dc24) |
| 4323 | S→C | u32 result |
| 4340 | C→S | the host, when a player reaches it: u32 user id (0x13dd98) |
| 4341 | S→C | u32 result, u32 user id; the host answers the joiner only once a reply names it (0x13de8c) |
| 43b0 | C→S | empty: start of a list, sent by host and joiner alike |
| 43b1 | S→C | u32 0 |
| 43b2 | C→S | 240 bytes, three seen; each carries a name (DINGO, NIGHTHAWK): likely the player's squad members (unverified) |
| 43b3 | S→C | u32 0 |
| 43b4 | C→S | empty: end of the rule list |
| 43b5 | S→C | u32 0 |
| 43a0 | C→S | u32 user id, u32 (10 seen): result report? |
| 43a1 | S→C | empty |
| 0003 | C→S | empty: goodbye |

### Games

A game's settings are a struct in the client (offsets below are into it).
4b10 sends name[16] (+4), comment[128] (+0x15), u8 locked (+0x96),
password[16] (+0x97), u8 (+0xa7), then the rules: 16 (u8 +0x234+i, u8
+0x244+i) pairs (a round's mode and map, as far as seen); u8 +0x254..0x256;
u32 +0x258, +0x25c, +0x260; u8 +0x264, +0x265; u32 +0x268; u16 +0x270; u32
+0x274; u16 +0x27c; u8 +0x27e; u32 +0x280; five {u8, u32, u32, u16, u32} from
+0x284 (stride 0x14); bytes[2] +0x2f1; u8 +0x2f3; u16 +0x2f4, +0x2f6; u8
+0x2fc..0x300. That is 315 bytes, padded to 320.

4b13 is u32 result, u32 game id, name, comment, locked, u8 +0xa7, u32 +0x304,
+0x308, +0x30c, then the same rules with eight more fields (u8 +0x266 after
+0x265; u32 +0x26c after +0x268; u16 +0x272 after +0x270; u32 +0x278 after
+0x274; u16 +0x2e8, u32 +0x2ec, u8 +0x2f0 before +0x2f1; u32 +0x2f8 after
+0x2f6), 341 bytes in all; then up to nine players until the message ends:
u32 user id, name[16], u8, five u32 (41 bytes). The password is not sent.

A 4b02 entry: u32 game id, name[16], u8 locked, u8 (+0xa7), three u32, four
u8, bytes[2] (+0x2f1), u8, u32, two u8, u32, u16, u32, u8, stored in
0x44-byte slots (at most 1000). The online menu, a stage module, reads them;
from SaveMGO's one listed game, the first two u8 are a (mode, map) pair, the
round being played, and the rest is not yet understood.

SaveMGO's replies carry stale bytes from earlier messages in their unused
space (news text, other players' names); our server zeroes them.

## HTTP

Paths are relative to the web server's root (`/us/mpo/` for the US game).
Requests are POSTed as `application/x-www-form-urlencoded`, `data=...`.

| Path | Fields | Notes |
|---|---|---|
| reguser/reguser.html | name, passwd, pname, version | create account |
| reguser/deluser.html | name, passwd | delete account |
| reguser/chgpswd.html | name, passwd, pswdnew | change password |
| rank/mg3getrank.html | term, rule, skey, from, records, pid | rankings |
| cyberval_15/cyb_up.html, cyb_dl.html, cyb_get_rank.html | | uploads, downloads, rankings |
| policy_15/policy15_us_en.html | | terms of use, plain text |
| text/list_ngword_utf8_lf.bin | | banned words; SaveMGO serves an empty list (24 bytes) |
| text/weapon_params.bin | | balance overrides; optional |
| text/online_faq_us_en.txt | | FAQ |
| advertise/lobby%02d_img.html | | lobby banner image |

Each server's operator chooses the terms, news, banner, FAQ and banned words.
