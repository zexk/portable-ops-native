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
| 4700 | C→S | 22 bytes |
| 4701 | S→C | u32 0 |
| 4702 | C→S | u16 status (2, 3 seen) |
| 4703 | S→C | u32 0 |
| 4b00 | C→S | game list request |
| 4b01 | S→C | u32 count? |
| 4b02 | S→C | 58 bytes per game: u32 game id, name[16], ..., settings |
| 4b03 | S→C | u32 0: end of list |
| 4b12 | C→S | u32 game id: game details |
| 4b13 | S→C | 512 bytes: u64 game id, name[16], comment, rules, host and players |
| 4902 | C→S | u32 user id: player details |
| 4903 | S→C | 512 bytes: u64 user id, name[16], emblem, stats |
| 4320 | C→S | join game, Blowfish(u32 game id, zero padding) |
| 4321 | S→C | u32 result, host public address[16] + u16 port, host LAN address[16] + u16 port |
| 43b0 | C→S | empty: start of a host's rule list? |
| 43b1 | S→C | u32 0 |
| 43b2 | C→S | 240 bytes: one round's rules (map name, mode, limits) |
| 43b3 | S→C | u32 0 |
| 43b4 | C→S | empty: end of the rule list |
| 43b5 | S→C | u32 0 |
| 43a0 | C→S | u32 user id, u32 (10 seen): result report? |
| 43a1 | S→C | empty |
| 0003 | C→S | empty: goodbye |

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
