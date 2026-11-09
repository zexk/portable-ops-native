#!/usr/bin/env python3
"""Add local Fulton donors to stage archives without changing disc file sizes.

STAGES is an extracted PO stage directory. ASSETS contains the PW PSP balloon
MDP and TXP (the TXP is converted to PO's layout). No donor data is shipped.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import zlib

spec = importlib.util.spec_from_file_location('stage_images', Path(__file__).with_name('stage-images.py'))
stage_images = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = stage_images
spec.loader.exec_module(stage_images)


def pack(files):
    data = bytearray(struct.pack('<I', len(files)))
    for name, payload in files.items():
        data += name.encode('latin1') + b'\0'
        data += bytes(-len(data) % 4)
        data += struct.pack('<I', len(payload))
        data += bytes(-len(data) % 16)
        data += payload + b'\0'
    return bytes(data)


def convert_txp(pw):
    """PW PSP texture pack to PO's layout (the one stage cache.qar files use).

    PW: header {0, name hash, images, textures, ?, image table, texture table,
    data}; image {u16 format, u16 w, u16 h, u16 0, u32 0, u32 pixels, u32 0};
    texture (0x28) {0, hash, image, clut, u, v, su, sv, h << 16 | w, 0}.
    PO: header {0, 1, images, image table, textures, texture table, 0, 0};
    image (0x10) {h << 16 | w, format, pixels, clut}; texture (0x30)
    {0x10, hash, 0, h << 16 | w, 0, image, 0, u, v, su, sv, 0}. Every PO image
    owns its CLUT, so each PW texture gets its own image entry.
    """
    _, _, images, textures, _, image_table, texture_table, data = struct.unpack_from('<8I', pw)
    if images < 1 or textures < 1 or not (0x20 <= image_table < texture_table < data <= len(pw)):
        raise ValueError('not a PW PSP texture pack')
    new_data = (0x20 + textures * (0x10 + 0x30) + 15) & ~15
    out = bytearray(struct.pack('<8I', 0, 1, textures, 0x20, textures, 0x20 + textures * 0x10, 0, 0))
    records = bytearray()
    for i in range(textures):
        _, hash_, image, clut, u, v, su, sv, wh, _ = struct.unpack_from('<10I', pw, texture_table + 0x28 * i)
        fmt, w, h, _, _, pixels, _ = struct.unpack_from('<4H3I', pw, image)
        if not (data <= pixels < len(pw) and data <= clut < len(pw)):
            raise ValueError(f'texture {i}: offsets outside data')
        out += struct.pack('<4I', h << 16 | w, fmt, pixels - data + new_data, clut - data + new_data)
        records += struct.pack('<12I', 0x10, hash_, 0, wh, 0, 0x20 + 0x10 * i, 0, u, v, su, sv, 0)
    out += records
    out += bytes(new_data - len(out))
    out += pw[data:]
    return bytes(out)


def unpack(data):
    return stage_images.zar_files(struct.pack('<I', len(data)) + zlib.compress(data))


def patch(raw, donors):
    files = stage_images.zar_files(raw)
    if 'cache.dar' not in files:
        raise ValueError('stage has no named cache.dar')
    assets = unpack(files['cache.dar'])
    if any(name in assets for name in donors):
        raise ValueError('donor name already exists in stage')
    assets.update(donors)
    files['cache.dar'] = pack(assets)
    data = pack(files)
    compressed = struct.pack('<I', len(data)) + zlib.compress(data, 9)
    if len(compressed) > len(raw):
        raise ValueError(f'needs {len(compressed) - len(raw)} extra compressed bytes')
    # The game can use the original ISO file size before opening it. Padding
    # avoids changing those size contracts; the zlib stream ignores its tail.
    output = compressed + bytes(len(raw) - len(compressed))
    checked = stage_images.zar_files(output)
    original = stage_images.zar_files(raw)
    assert all(checked[k] == v for k, v in original.items() if k != 'cache.dar')
    assert unpack(checked['cache.dar']) == assets
    return output


def self_test():
    original = {'cache.dar': pack({'old.bin': b'unchanged'}), 'stage.rlc': b'code stays unchanged'}
    plain = pack(original)
    raw = struct.pack('<I', len(plain)) + zlib.compress(plain) + bytes(1024)
    output = patch(raw, {'new.mtar': b'new data'})
    assert len(raw) == len(output)
    assert stage_images.zar_files(output)['stage.rlc'] == b'code stays unchanged'
    assert unpack(stage_images.zar_files(output)['cache.dar'])['old.bin'] == b'unchanged'
    try:
        patch(raw, {'old.bin': b'overwrite'})
    except ValueError:
        pass
    else:
        raise AssertionError('existing resource overwritten')
    # One 64x64 4bpp image, two textures with their own CLUTs.
    pw = bytearray(struct.pack('<8I', 0, 0xABCDEF, 1, 2, 0x40, 0x20, 0x34, 0x90))
    pw += struct.pack('<4H3I', 4, 0x6040, 0x6040, 0, 0, 0x110, 0)
    for i in range(2):
        pw += struct.pack('<10I', 0, 0x100 + i, 0x20, 0x90 + 0x40 * i, 0, 0, 0x3f800000, 0x3f800000, 0x400040, 0)
    pw += bytes(0x90 - len(pw)) + bytes(range(0x80)) + bytes(0x800)
    po = convert_txp(bytes(pw))
    _, flags, images, image_table, textures, texture_table, _, _ = struct.unpack_from('<8I', po)
    assert (flags, images, textures) == (1, 2, 2)
    for i in range(2):
        wh, fmt, pixels, clut = struct.unpack_from('<4I', po, image_table + 0x10 * i)
        assert (wh, fmt) == (0x60406040, 4) and pixels % 16 == 0 and clut % 16 == 0
        assert po[clut:clut + 0x40] == bytes(range(0x40 * i, 0x40 * i + 0x40))
        record = struct.unpack_from('<12I', po, texture_table + 0x30 * i)
        assert record[:6] == (0x10, 0x100 + i, 0, 0x400040, 0, image_table + 0x10 * i)
    print('Fulton archive round-trip, fixed-size, code-preservation and TXP conversion checks passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stages', type=Path, nargs='?')
    parser.add_argument('assets', type=Path, nargs='?')
    parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--stage', action='append', default=[])
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not all((args.stages, args.assets, args.output)):
        parser.error('provide STAGES ASSETS OUTPUT or --self-test')
    donors = {name: (args.assets / name).read_bytes() for name in (
        'fult_item_balloon.txp', 'fult_item_balloon.mdp')}
    if donors['fult_item_balloon.mdp'][:4] != b'MDP ':
        parser.error('balloon must be the PSP MDP donor')
    # PO's .txp loader expects its own layout; PW's crashes it (pointers
    # relocated twice). Convert unless the file is already PO layout.
    if struct.unpack_from('<I', donors['fult_item_balloon.txp'], 4)[0] != 1:
        donors['fult_item_balloon.txp'] = convert_txp(donors['fult_item_balloon.txp'])
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for folder in sorted(args.stages.iterdir()):
        if not folder.is_dir() or (args.stage and folder.name not in args.stage):
            continue
        path = folder / '_zar'
        if not path.exists():
            continue
        raw = path.read_bytes()
        try:
            output = patch(raw, donors)
        except ValueError as error:
            print(f'{folder.name}: skipped, {error}', file=sys.stderr)
            continue
        filename = folder.name + '.zar'
        (args.output / filename).write_bytes(output)
        rows.append(' '.join((hashlib.sha256(raw).hexdigest(), str(len(raw)),
                              raw[:64].hex(), hashlib.sha256(output).hexdigest(), filename)))
        print(f'{folder.name}: donors added, disc size unchanged ({len(output)} bytes)')
    (args.output / 'index.txt').write_text('\n'.join(rows) + '\n')
    if not rows:
        parser.exit(1, 'no stages prepared\n')


if __name__ == '__main__':
    main()
