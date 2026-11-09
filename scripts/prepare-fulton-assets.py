#!/usr/bin/env python3
"""Add local Fulton donors to stage archives without changing disc file sizes.

STAGES is an extracted PO stage directory. ASSETS contains the PW PSP balloon
MDP and TXP (the TXP is converted to PO's layout) and fult_enemy.mtar, the
soldier lift clips converted by tools/portable-ops/mtar.py. No donor data is shipped.
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


def texture_names(txp):
    _, _, _, _, textures, table, _, _ = struct.unpack_from('<8I', txp)
    return [struct.unpack_from('<I', txp, table + 0x30 * i + 4)[0] for i in range(textures)]


def rename_textures(txp, mdp, mapping):
    """Rename textures in a PO-layout TXP and the one material reference to
    each in its MDP. PW scopes a model's texture names to its own pack (MDP
    +0x10 names it); PO looks names up in one table, so a later pack reusing a
    name would replace the earlier pack's texture."""
    txp, mdp = bytearray(txp), bytearray(mdp)
    _, _, _, _, textures, table, _, _ = struct.unpack_from('<8I', txp)
    for i in range(textures):
        at = table + 0x30 * i + 4
        old = struct.unpack_from('<I', txp, at)[0]
        if old not in mapping:
            continue
        struct.pack_into('<I', txp, at, mapping[old])
        refs = [o for o in range(0, len(mdp) - 3, 4) if struct.unpack_from('<I', mdp, o)[0] == old]
        if len(refs) != 1:
            raise ValueError(f'texture {old:#x}: {len(refs)} model references, expected one')
        struct.pack_into('<I', mdp, refs[0], mapping[old])
    return bytes(txp), bytes(mdp)


def renumber_bank(sep, bank):
    """A sound bank (SP + id, ..., EP + id at its declared size) as bank id
    `bank`. PO's SHDS table gives bank 0x31 (segrp_75) one sound, id 0x271."""
    size = struct.unpack_from('<I', sep, 4)[0]
    if sep[:2] != b'SP' or size > len(sep) or sep[size - 4:size - 2] != b'EP':
        raise ValueError('not a sound bank')
    sep = bytearray(sep)
    struct.pack_into('<H', sep, 2, bank)
    struct.pack_into('<H', sep, size - 2, bank)
    return bytes(sep)


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
    models = ('fult_item_balloon', 'fult_item_body')  # balloon; harness and tie
    donors = {name: (args.assets / name).read_bytes() for name in (
        *(m + ext for m in models for ext in ('.mdp', '.txp')), 'fult_enemy.mtar')}
    placement_bytes = None
    placement = args.assets / 'fult_place.mtar'
    if placement.exists():
        data = placement.read_bytes()
        if len(data) < 32 or struct.unpack_from('<3I', data) != (0x7261744d, 19, 1):
            parser.error('fult_place.mtar must be PO-format snake_coop clip 156 (tools/portable-ops/mtar.py)')
        placement_bytes = data
    for model in models:
        if donors[model + '.mdp'][:4] != b'MDP ':
            parser.error(f'{model} must be the PSP MDP donor')
        # PO's .txp loader expects its own layout; PW's crashes it (pointers
        # relocated twice). Convert unless the file is already PO layout.
        if struct.unpack_from('<I', donors[model + '.txp'], 4)[0] != 1:
            donors[model + '.txp'] = convert_txp(donors[model + '.txp'])
    # Later packs give up names an earlier one already uses (the harness
    # reuses two of the balloon's names for different images).
    taken = set()
    for model in models:
        names = texture_names(donors[model + '.txp'])
        mapping = {}
        for name in names:
            if name in taken:
                new = name
                while new in taken or new in names:
                    new = (new + 0x10000) & 0xffffff
                mapping[name] = new
            taken.add(mapping.get(name, name))
        if mapping:
            donors[model + '.txp'], donors[model + '.mdp'] = rename_textures(
                donors[model + '.txp'], donors[model + '.mdp'], mapping)
            print(model + ':', ', '.join(f'texture {a:#x} -> {b:#x}' for a, b in mapping.items()))
    # Texture packs first: a model binds the pack its header names (MDP +0x10)
    # when it loads, so a pack added after its model leaves it untextured.
    donors = {name: donors[name] for name in sorted(donors, key=lambda n: not n.endswith('.txp'))}
    if struct.unpack_from('<3I', donors['fult_enemy.mtar']) != (0x7261744d, 19, 2):
        parser.error('fult_enemy.mtar must be PO-format (tools/portable-ops/mtar.py, enedam clips 101 103)')
    args.output.mkdir(parents=True, exist_ok=True)
    if placement_bytes is not None:
        # Loaded through PO's own resource handler once per stage; its ZAR
        # need not grow to fit another motion archive.
        (args.output / 'fult_place.mtar').write_bytes(placement_bytes)
        print('fult_place.mtar: PW attachment as a standalone PO archive')
    # The Fulton sound bank is filed by the host from memory, not from a stage.
    sound = args.assets / 'segrp_75_furuton.sep'
    if sound.exists():
        (args.output / 'fulton.sep').write_bytes(renumber_bank(sound.read_bytes(), 0x31))
        print('fulton.sep: PW segrp_75_furuton as bank 0x31')
    lift_sound = args.assets / 'fulton-lift.sep'
    if lift_sound.exists():
        data = lift_sound.read_bytes()
        if len(data) < 0x80 or len(data) > 0x11000 or data[:4] != b'SP2\0' or data[-4:] != b'EP2\0' or \
                struct.unpack_from('<3I', data, 4) != (len(data), 0x30, 0x70):
            parser.error('fulton-lift.sep must be PO bank 0x32 with two cues (tools/portable-ops/fulton_sfx.py)')
        (args.output / 'fulton-lift.sep').write_bytes(data)
        print('fulton-lift.sep: two common extraction effects as bank 0x32')
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
    if not rows:
        parser.exit(1, 'no stages prepared\n')
    (args.output / 'index.txt').write_text('\n'.join(rows) + '\n')


if __name__ == '__main__':
    main()
