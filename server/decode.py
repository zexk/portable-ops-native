#!/usr/bin/env python3
"""Decode a <PREFIX>_NET_CAPTURE file's lobby traffic (TCP to the game's servers).

usage: server/decode.py CAPTURE [--payload]

Each TCP connection is a byte stream each way. Every message on it is masked,
from its own first byte, by XOR with the repeating key 5a 70 85 af; it then reads
as a 24-byte big-endian header
(command u16, payload length u16, sequence u32, MAC 16 bytes) and the payload.
The MAC is HMAC-MD5 of the first 8 header bytes and the payload, keyed with a
16-byte string in the game's executable (EBOOT data, by the XOR key's table).
"""
import hashlib
import hmac
import sys

KEY = bytes.fromhex('5a7085af')
MAC_KEY = b'Z7/biJ46TzGF-8yx'


def mac(header, payload):
    return hmac.new(MAC_KEY, header[:8] + payload, hashlib.md5).digest()


def unmask(data):
    return bytes(b ^ KEY[i % 4] for i, b in enumerate(data))


def messages(stream):
    """(command, sequence, MAC good, payload) for each whole message in a stream.
    The mask starts over at each message."""
    at = 0
    while at + 24 <= len(stream):
        header = unmask(stream[at:at + 24])
        command = int.from_bytes(header[0:2], 'big')
        length = int.from_bytes(header[2:4], 'big')
        sequence = int.from_bytes(header[4:8], 'big')
        if at + 24 + length > len(stream):
            break
        payload = unmask(stream[at:at + 24 + length])[24:]
        yield command, sequence, header[8:24] == mac(header, payload), payload
        at += 24 + length


def connections(path):
    """Per TCP connection: its peer and the ordered (time, direction, bytes) chunks."""
    current, out = {}, []
    for line in open(path):
        parts = line.split()
        time, sock, direction, peer = int(parts[0]), parts[1], parts[2], parts[3]
        if direction == '@':
            current[sock] = {'peer': peer, 'chunks': []}
            out.append(current[sock])
        elif peer == '-' and sock in current:
            current[sock]['chunks'].append((time, direction, bytes.fromhex(parts[4] if len(parts) > 4 else '')))
    return out


def main():
    show_payload = '--payload' in sys.argv
    limit = 4096 if '--full' in sys.argv else 64
    for connection in connections(sys.argv[1]):
        print('== %s' % connection['peer'])
        # Decode each direction's whole stream, then put messages back in time order
        # by the chunk that completed them.
        timeline = []
        for direction in '><':
            stream, ends = b'', []
            for time, d, data in connection['chunks']:
                if d == direction:
                    stream += data
                    ends.append((len(stream), time))
            consumed = 0
            for command, sequence, mac_ok, payload in messages(stream):
                consumed += 24 + len(payload)
                time = next(t for end, t in ends if end >= consumed)
                timeline.append((time, direction, command, sequence, mac_ok, payload))
        for time, direction, command, sequence, mac_ok, payload in sorted(timeline, key=lambda m: m[0]):
            line = '%7d %s %04x seq=%-4d len=%-5d%s' % (time, 'C->S' if direction == '>' else 'S->C', command,
                                                      sequence, len(payload), '' if mac_ok else ' BAD MAC')
            if show_payload:
                line += " " + payload[:limit].hex() + ("..." if len(payload) > limit else "")
            print(line)


if __name__ == '__main__':
    main()
