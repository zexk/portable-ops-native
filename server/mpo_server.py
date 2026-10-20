#!/usr/bin/env python3
"""A server for Metal Gear Solid: Portable Ops+ online play (PROTOCOL.md).

usage: server/mpo_server.py [--address PUBLIC_IP] [--web-port PORT] [--data DIR] [--game EBOOT]
       server/mpo_server.py --data DIR adduser NAME PASSWORD PLAYER_NAME

Runs the gate (TCP 5731), account (5732) and lobby (5734) servers, STUN (UDP
3478) and the web pages (HTTP, port 80 by default). --address is what clients
are told to connect to; the default is this host's address on the default
route. DIR (default server-data/) holds accounts.json and the operator's
content: terms.txt, news.txt, faq.txt, ngwords.txt, banner.bin,
weapon_params.bin; any of them may be absent, except that the game will not go
on line without a weapon_params.bin. With --game (the game's decrypted
executable: EBOOT.ELF from an installed port, or PSP_GAME/SYSDIR/BOOT.BIN) one
that changes nothing is made from the game's own defaults.

Point a client at it with <PREFIX>_ONLINE_SERVER=host[:web port].
"""
import asyncio
import hashlib
import hmac
import json
import os
import pathlib
import secrets
import socket
import string
import struct
import zlib
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from blowfish import Blowfish  # noqa: E402

MASK = bytes.fromhex('5a7085af')
MAC_KEY = b'Z7/biJ46TzGF-8yx'
# The game's Blowfish key (EBOOT 0x16d998, 56 bytes).
CIPHER = Blowfish(bytes.fromhex(
    '537ef941c6273a8bdd0c654ca51694b12072ed41b948b6180f984346fcb70972'
    '65cb4a37985b1ac8dfed5127103f770ec0a9165cd938e4f2'))
GATE_PORT, ACCOUNT_PORT, LOBBY_PORT, STUN_PORT = 5731, 5732, 5734, 3478
# The bit SaveMGO's plugin sets on the user id in the lobby join (savempo.hpp).
SAVEMPO_MARK = 0x10000000


def weapon_params_cipher(data, key):
    """The keystream the game decrypts weapon_params.bin's sections with (the
    online menu stage, set up at 0x8b182dc, applied at 0x8b18308); it is its own
    inverse. `key` is the file's three header words at 8."""
    a, b, c = struct.unpack('<3I', key)
    a ^= b
    state, step = (a | ((a ^ 0x6576) << 16)) & 0xFFFFFFFF, (a * c) & 0xFFFFFFFF
    out = bytearray(data)
    for at in range(0, len(out) - len(out) % 4, 4):
        struct.pack_into('<I', out, at, struct.unpack_from('<I', out, at)[0] ^ state)
        state = (state * 0x02E90EDD + step) & 0xFFFFFFFF
    return bytes(out)


def neutral_weapon_params(executable):
    """A weapon_params.bin that leaves the game as it is. The game reads five
    sections, each {size, CRC-32 of the plain data, offset} from 0x14, after the
    magic 0x00ed617a, a zero word and a 12-byte key. Four are copied to override
    buffers with their size, so empty ones override nothing. The fifth is copied
    over a 0xd0-byte table whatever its size (EBOOT 0xa1cdc), so it carries
    that table as the executable has it: 0x3f90 into the data segment (US)."""
    phoff = struct.unpack_from('<I', executable, 0x1C)[0]
    data = struct.unpack_from('<8I', executable, phoff + 32)  # second program header
    table = executable[data[1] + 0x3F90:data[1] + 0x3F90 + 0xD0]
    key = fixed('NEUTRAL', 12)
    header_size = 0x14 + 6 * 12
    entries = b''.join(struct.pack('<3I', 0, 0, header_size) for _ in range(4))
    entries += struct.pack('<3I', len(table), zlib.crc32(table), header_size)
    entries += struct.pack('<3I', 0, 0, header_size + len(table))
    return struct.pack('<II', 0x00ED617A, 0) + key + entries + weapon_params_cipher(table, key)


def log(*parts):
    print(*parts, flush=True)


def masked(data):
    return bytes(b ^ MASK[i % 4] for i, b in enumerate(data))


def fixed(text, size):
    data = text.encode('latin-1')[:size - 1] if isinstance(text, str) else text[:size]
    return data + bytes(size - len(data))


def u32(value):
    return struct.pack('>I', value & 0xFFFFFFFF)


class Accounts:
    """Accounts in DIR/accounts.json: login name -> password, user id, player name."""

    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.data = json.loads(path.read_text()) if path.exists() else {'next_id': 1, 'users': {}}

    def save(self):
        self.path.write_text(json.dumps(self.data, indent=1))

    def add(self, name, password, player):
        with self.lock:
            if name in self.data['users']:
                return None
            user = {'password': password, 'id': self.data['next_id'], 'player': player}
            self.data['users'][name] = user
            self.data['next_id'] += 1
            self.save()
            return user

    def get(self, name):
        return self.data['users'].get(name)

    def by_id(self, user_id):
        return next((u for u in self.data['users'].values() if u['id'] == user_id), None)


class Server:
    def __init__(self, address, data_dir):
        self.address = address
        self.data_dir = data_dir
        self.accounts = Accounts(data_dir / 'accounts.json')
        self.sessions = {}  # token -> user
        self.games = {}  # game id -> game
        self.next_game = 1

    def content(self, name):
        path = self.data_dir / name
        return path.read_bytes() if path.exists() else None

    # --- News, for the gate: blocks in news.txt separated by blank lines, each
    # "date | title" then the body.
    def news(self):
        text = (self.content('news.txt') or b'').decode('utf-8', 'replace').strip()
        items = []
        for block in filter(None, (b.strip() for b in text.split('\n\n'))):
            first, _, body = block.partition('\n')
            date, _, title = first.partition('|')
            items.append((date.strip(), title.strip(), body.strip()))
        return items


class Connection:
    """One client on one of the TCP servers."""

    def __init__(self, server, reader, writer, name):
        self.server, self.reader, self.writer, self.name = server, reader, writer, name
        self.sequence = 0
        self.peer = writer.get_extra_info('peername')
        self.user = None
        self.challenge = b''

    def send(self, command, payload=b''):
        self.sequence += 1
        header = struct.pack('>HHI', command, len(payload), self.sequence)
        mac = hmac.new(MAC_KEY, header + payload, hashlib.md5).digest()
        self.writer.write(masked(header + mac + payload))

    async def messages(self):
        while True:
            header = masked(await self.reader.readexactly(24))
            command, length, _sequence = struct.unpack('>HHI', header[:8])
            payload = masked(header + await self.reader.readexactly(length))[24:] if length else b''
            if hmac.new(MAC_KEY, header[:8] + payload, hashlib.md5).digest() != header[8:24]:
                log(self.name, self.peer, 'bad MAC on %04x' % command)
                return
            yield command, payload

    async def run(self, handler):
        log(self.name, self.peer, 'connected')
        try:
            async for command, payload in self.messages():
                if command == 0x0003:
                    break
                if command == 0x0005:
                    self.send(0x0005)
                elif not handler(self, command, payload):
                    log(self.name, self.peer, 'unhandled %04x %s' % (command, payload.hex()))
                await self.writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            log(self.name, self.peer, 'closed')
            self.writer.close()


# --- Gate ------------------------------------------------------------------

def gate(c, command, payload):
    s = c.server
    if command == 0x2805:
        c.send(0x2802, u32(0))
        entries = b''
        for index, (name, port, kind) in enumerate(
                (('GATE', GATE_PORT, 1), ('ACCOUNT', ACCOUNT_PORT, 2), ('SOLID_SNAKE', LOBBY_PORT, 4))):
            # 49 bytes, unaligned: the address field is 15 bytes.
            entries += u32(index) + u32(index) + fixed(name, 16) + fixed(s.address, 15)
            entries += struct.pack('>HHH', port, 1, kind) + bytes(4)
        c.send(0x2803, fixed(entries, 160))
        c.send(0x2804, u32(0))
    elif command == 0x2008:
        c.send(0x2009, u32(0))
        news = s.news()
        for index, (date, title, body) in enumerate(news):
            number = len(news) - index
            c.send(0x200a, u32(number) + bytes([1]) + fixed(date, 19) + fixed(title, 128)
                   + body.replace('\n', '\r\n').encode('latin-1', 'replace') + b'\0')
        c.send(0x200b, u32(0))
    else:
        return False
    return True


# --- Account ---------------------------------------------------------------

def account(c, command, payload):
    s = c.server
    if command == 0x3001:
        c.challenge = secrets.token_bytes(16)
        c.send(0x3002, c.challenge)
    elif command == 0x3003:
        plain = CIPHER.decrypt(payload)
        name = plain[:16].split(b'\0')[0].decode('latin-1')
        user = s.accounts.get(name)
        expected = user and hmac.new(MAC_KEY, name.encode('latin-1') + c.challenge + user['password'].encode('latin-1'),
                                     hashlib.md5).digest()
        if not user or expected != plain[16:32]:
            log('account', c.peer, 'login refused for', repr(name))
            c.send(0x3004, u32(0xFFFFFFFF) + bytes(16))
            return True
        token = ''.join(secrets.choice(string.ascii_letters) for _ in range(16))
        s.sessions[token] = user
        c.user = user
        log('account', c.peer, 'login', repr(name), 'id', user['id'])
        c.send(0x3004, u32(0) + token.encode())
    elif command == 0x3840 and c.user:
        c.send(0x3841, u32(0) + u32(c.user['id']) + fixed(c.user['player'], 16) + bytes(8))
    elif command == 0x3042:
        c.send(0x3043, u32(0))
        c.send(0x3045, u32(0))
    else:
        return False
    return True


# --- Lobby -----------------------------------------------------------------

def player_record(user):
    """4901's and 4903's player block: user id, player name, emblem, stats (zero)."""
    return fixed(u32(user['id']) + fixed(user['player'], 16) + bytes(13) + fixed('emblem', 16), 1024)


def lobby(c, command, payload):
    s = c.server
    if command == 0x3003:
        plain = CIPHER.decrypt(payload)
        user_id = struct.unpack('>I', plain[:4])[0] & ~SAVEMPO_MARK
        token = plain[4:20].decode('latin-1')
        user = s.sessions.get(token)
        if not user or user['id'] != user_id:
            log('lobby', c.peer, 'join refused for id', user_id)
            c.send(0x3004, fixed(u32(0xFFFFFFFF), 256))
            return True
        c.user = user
        c.send(0x3004, fixed(u32(0) + token.encode() + fixed('RECORDING', 16), 256))
    elif not c.user:
        return False
    elif command == 0x4900:
        c.send(0x4901, player_record(c.user))
        for page in range(3):
            c.send(0x4904, fixed(u32(page), 700))
            c.send(0x4905, fixed(bytes(4) + u32(page), 700))
    elif command == 0x4700:
        c.lan = payload
        c.send(0x4701, u32(0))
    elif command == 0x4702:
        c.send(0x4703, u32(0))
    elif command == 0x4b00:
        c.send(0x4b01, u32(len(s.games)))
        for game_id, game in s.games.items():
            c.send(0x4b02, fixed(u32(game_id) + fixed(game['name'], 16), 58))
        c.send(0x4b03, u32(0))
    elif command == 0x4902:
        user = s.accounts.by_id(struct.unpack('>I', payload[:4])[0])
        c.send(0x4903, fixed(bytes(4) + (player_record(user)[:508] if user else b''), 512))
    elif command in (0x43b0, 0x43b2, 0x43b4):
        c.send(command + 1, u32(0))
    elif command == 0x43a0:
        c.send(0x43a1)
    elif command == 0x4b04:
        # The host's saved game settings (read at EBOOT 0x13b5a4): none yet.
        c.send(0x4b05, CIPHER.encrypt(bytes(0x318)))
    elif command == 0x4b06:
        # More saved settings (0x13be6c), unencrypted: none yet.
        c.send(0x4b07, bytes(0xd8))
    elif 0x4000 <= command < 0x5000 and command % 2 == 0:
        # Not understood yet: answer "ok" with nothing, and say so.
        log('lobby', c.peer, 'auto-reply %04x to %04x %s' % (command + 1, command, payload.hex()))
        c.send(command + 1, bytes(0x100))
    else:
        return False
    return True


# --- STUN (RFC 5389 binding requests only) -----------------------------------

class Stun(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, address):
        if len(data) < 20 or data[0:2] != b'\x00\x01':
            return
        transaction = data[4:20]
        ip, port = address[0], address[1]
        cookie = 0x2112A442
        xport = port ^ (cookie >> 16)
        xip = struct.unpack('>I', socket.inet_aton(ip))[0] ^ cookie
        mapped = struct.pack('>HHBBH', 0x0001, 8, 0, 1, port) + socket.inet_aton(ip)
        xor_mapped = struct.pack('>HHBBHI', 0x0020, 8, 0, 1, xport, xip)
        body = mapped + xor_mapped
        self.transport.sendto(struct.pack('>HH', 0x0101, len(body)) + transaction + body, address)


# --- Web -------------------------------------------------------------------

def web_handler(server):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, status, body=b'', kind='application/octet-stream'):
            self.send_response(status)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urllib.parse.urlparse(self.path).path
            name = path.rsplit('/', 1)[-1]
            if name.startswith('policy'):
                body = server.content('terms.txt') or b'Play fair.\n'
                return self.reply(200, body, 'text/plain')
            if name == 'list_ngword_utf8_lf.bin':
                words = (server.content('ngwords.txt') or b'').decode('utf-8').split()
                listing = b''.join(w.encode('utf-8') + b'\n' for w in words)
                # Observed empty form: u32 0, u32 24 (header size), 16 zero bytes, then the words.
                return self.reply(200, u32(len(words))[::-1] + struct.pack('<I', 24) + bytes(16) + listing)
            if name == 'weapon_params.bin' and server.content('weapon_params.bin'):
                return self.reply(200, server.content('weapon_params.bin'))
            if name.startswith('lobby') and server.content('banner.bin'):
                return self.reply(200, server.content('banner.bin'))
            if name.startswith('online_faq') and server.content('faq.txt'):
                return self.reply(200, server.content('faq.txt'), 'text/plain')
            log('web', 'not found', path)
            self.reply(404)

        def do_POST(self):
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length)
            log('web', 'POST', self.path, body[:200])
            self.reply(404)

        def log_message(self, *args):
            pass

    return Handler


def default_address():
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(('192.0.2.1', 9))
        return probe.getsockname()[0]
    finally:
        probe.close()


async def main_async(server, web_port):
    for name, port, handler in (('gate', GATE_PORT, gate), ('account', ACCOUNT_PORT, account),
                                ('lobby', LOBBY_PORT, lobby)):
        async def accept(reader, writer, name=name, handler=handler):
            await Connection(server, reader, writer, name).run(handler)
        await asyncio.start_server(accept, '0.0.0.0', port)
    await asyncio.get_running_loop().create_datagram_endpoint(Stun, local_addr=('0.0.0.0', STUN_PORT))
    web = ThreadingHTTPServer(('0.0.0.0', web_port), web_handler(server))
    threading.Thread(target=web.serve_forever, daemon=True).start()
    log('serving as %s: gate %d, account %d, lobby %d, STUN %d, web %d' %
        (server.address, GATE_PORT, ACCOUNT_PORT, LOBBY_PORT, STUN_PORT, web_port))
    await asyncio.Event().wait()


def main():
    args = sys.argv[1:]

    def option(name, default):
        if name in args:
            i = args.index(name)
            value = args[i + 1]
            del args[i:i + 2]
            return value
        return default

    data_dir = pathlib.Path(option('--data', 'server-data'))
    game = option('--game', None)
    address = option('--address', None)
    web_port = int(option('--web-port', '80'))
    data_dir.mkdir(parents=True, exist_ok=True)
    if game and not (data_dir / 'weapon_params.bin').exists():
        (data_dir / 'weapon_params.bin').write_bytes(neutral_weapon_params(pathlib.Path(game).read_bytes()))
        print('made', data_dir / 'weapon_params.bin', 'from', game)
    server = Server(address or default_address(), data_dir)
    if args[:1] == ['adduser'] and len(args) == 4:
        user = server.accounts.add(args[1], args[2], args[3])
        print('added' if user else 'exists', args[1])
        return
    if args:
        sys.exit(__doc__)
    asyncio.run(main_async(server, web_port))


if __name__ == '__main__':
    main()
