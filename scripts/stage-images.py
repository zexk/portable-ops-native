#!/usr/bin/env python3
"""Make every stage's code from the disc, as the game has it when it runs.

usage: scripts/stage-images.py po|pop DISC OUT_DIR

The engine loads a stage PRX, starts it, then links it to itself with the
stage's .rlc file (from its _zar archive): offsets into the engine's text and
data, relocated by the engine's own loader (PO 0x1E04 stores the table, 0x1C0C
applies it) with the same entry format as a PRX's relocations. Doing both
here gives, for every stage on the disc, the code the port identifies a stage
by, written as OUT_DIR/mod<BASE>_mgp_stage_<HASH>.bin like a dump, without
playing to it. Then: scripts/add-modules.py <edition> OUT_DIR
"""
import pathlib
import struct
import sys
import zlib

# Where the port loads the engine and the stages (the first is the engine's
# text, the second its data segment's address), per edition.
EDITIONS = {
    'po': {'engine': (0x08804000, 0x08804000 + 0x1EED40), 'stage': 0x08B72F00},
    'pop': {'engine': (0x08804000, 0x08804000 + 0x170480), 'stage': 0x08B10600},
}


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def iso_files(disc):
    """{path: (offset, size)} of every file on an ISO 9660 image."""
    image = open(disc, 'rb')

    def read(sector, size):
        image.seek(sector * 2048)
        return image.read(size)

    files = {}

    def walk(sector, size, prefix):
        data = read(sector, size)
        position = 0
        while position < len(data):
            length = data[position]
            if length == 0:
                position = (position // 2048 + 1) * 2048
                continue
            extent, extent_size = struct.unpack_from('<I', data, position + 2)[0], struct.unpack_from('<I', data, position + 10)[0]
            flags, name_length = data[position + 25], data[position + 32]
            name = data[position + 33:position + 33 + name_length].decode('latin1').split(';')[0].lower()
            if name not in ('\0', '\1'):
                path = prefix + '/' + name
                if flags & 2:
                    walk(extent, extent_size, path)
                else:
                    files[path] = (extent * 2048, extent_size)
            position += length

    root = read(16, 2048)[156:190]
    walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')
    return image, files


def relocate(memory, entries, patch_bases, target_bases, engine):
    """Apply PSP relocation entries (offset, info) to memory, which holds the
    image from patch_bases[0]. A PRX's own HI16 pairs with the next LO16 of
    the same segments (as the port's loader does); the engine's loader pairs
    it with the next LO16 of any and reads that low half at the HI16's
    segment. (The engine also knows 0xFA, a chain of HI16s; no stage uses it.)"""
    def high(value):
        low = value & 0xFFFF
        return ((value - (low - 0x10000 if low & 0x8000 else low)) >> 16) & 0xFFFF

    original = memory if engine else bytes(memory)
    for i, (offset, info) in enumerate(entries):
        kind, segment = info & 0xFF, (info >> 8) & 0xFF
        where = patch_bases[segment] - patch_bases[0] + offset
        base = target_bases[(info >> 16) & 0xFF]
        op = struct.unpack_from('<I', original, where)[0]
        if kind == 2:
            op = (op + base) & 0xFFFFFFFF
        elif kind == 4:
            op = (op & 0xFC000000) | (((op & 0x3FFFFFF) + (base >> 2)) & 0x3FFFFFF)
        elif kind == 5:
            pair = next((j for j in range(i + 1, len(entries)) if entries[j][1] & 0xFF == 6 and
                         (engine or entries[j][1] >> 8 == info >> 8)), None)
            if pair is None and not engine:
                continue
            low_at = patch_bases[segment if engine else (entries[pair][1] >> 8) & 0xFF] - patch_bases[0] + \
                entries[pair if pair is not None else i][0]
            low = struct.unpack_from('<I', original, low_at)[0] & 0xFFFF
            op = (op & 0xFFFF0000) | high(((op & 0xFFFF) << 16) + (low - 0x10000 if low & 0x8000 else low) + base)
        elif kind == 6:
            op = (op & 0xFFFF0000) | ((op + base) & 0xFFFF)
        else:
            continue
        struct.pack_into('<I', memory, where, op)


def stage_code(prx, rlc, edition):
    """The stage's code once loaded at its base and linked to the engine."""
    shoff, = struct.unpack_from('<I', prx, 0x20)
    phoff, = struct.unpack_from('<I', prx, 0x1C)
    phnum, shnum, shstrndx = struct.unpack_from('<HxxHH', prx, 0x2C)
    base = EDITIONS[edition]['stage']
    segments = [struct.unpack_from('<8I', prx, phoff + i * 32) for i in range(phnum)]
    loads = [segment for segment in segments if segment[0] == 1]
    bases = [base + segment[2] for segment in loads]
    size = max(segment[2] + segment[5] for segment in loads)
    memory = bytearray(size)
    for segment in loads:
        memory[segment[2]:segment[2] + segment[4]] = prx[segment[1]:segment[1] + segment[4]]
    sections = [struct.unpack_from('<10I', prx, shoff + i * 40) for i in range(shnum)]
    names = sections[shstrndx][4]

    def name(section):
        return prx[names + section[0]:prx.index(b'\0', names + section[0])].decode()

    for section in sections:
        if section[1] == 0x700000A0:
            entries = list(struct.iter_unpack('<II', prx[section[4]:section[4] + section[5]]))
            relocate(memory, entries, bases, bases, False)
    relocate(memory, list(struct.iter_unpack('<II', rlc[:len(rlc) & ~7])), bases, EDITIONS[edition]['engine'], True)
    by_name = {name(section): section for section in sections}
    # The port's code: the first segment, up to the import stubs.
    end = min(loads[0][5], by_name['.sceStub.text'][3] if '.sceStub.text' in by_name else loads[0][5])
    return bytes(memory[:end])


def zar_files(zar):
    data = zlib.decompress(zar[4:])
    files, position = {}, 4
    while position < len(data):
        end = data.index(b'\0', position)
        name = data[position:end].decode('latin1')
        if not name:
            break
        position = (end + 1 + 3) & ~3
        size, = struct.unpack_from('<I', data, position)
        position = (position + 1 + 15) & ~15
        files[name] = data[position:position + size]
        position += size + 1
    return files


def main():
    edition, disc, out = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    image, files = iso_files(disc)

    def read(path):
        offset, size = files[path]
        image.seek(offset)
        return image.read(size)

    stages = sorted(path.split('/')[-2] for path in files if path.startswith('/psp_game/usrdir/stage/') and path.endswith('.prx'))
    for stage in stages:
        folder = '/psp_game/usrdir/stage/%s/' % stage
        rlc = zar_files(read(folder + '_zar')).get(stage + '.rlc', b'')
        code = stage_code(read(folder + stage + '.prx'), rlc, edition)
        path = out / ('mod%08X_mgp_stage_%016X.bin' % (EDITIONS[edition]['stage'], fnv1a64(code)))
        path.write_bytes(code)
        print('%s: %s' % (stage, path.name))


if __name__ == '__main__':
    main()
