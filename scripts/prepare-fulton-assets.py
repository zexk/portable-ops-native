#!/usr/bin/env python3
"""Convert local PW PSP Fulton donors to the port's converted bundle.

ASSETS contains the PW PSP balloon and harness MDP and TXP (each TXP is
converted to PO's layout) and fult_enemy.mtar, the soldier lift clips
converted by tools/portable-ops/mtar.py. OUTPUT is the folder the Game tab's
"Import Fulton assets" takes. Developer tool: the port's own importer will
replace it. No donor data is shipped.
"""
import argparse
from pathlib import Path
import struct
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


def self_test():
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
    print('Fulton TXP conversion checks passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path, nargs='?')
    parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not all((args.assets, args.output)):
        parser.error('provide ASSETS OUTPUT or --self-test')
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
    if struct.unpack_from('<3I', donors['fult_enemy.mtar']) != (0x7261744d, 19, 2):
        parser.error('fult_enemy.mtar must be PO-format (tools/portable-ops/mtar.py, enedam clips 101 103)')
    args.output.mkdir(parents=True, exist_ok=True)
    if placement_bytes is not None:
        (args.output / 'fult_place.mtar').write_bytes(placement_bytes)
        print('fult_place.mtar: PW attachment as a standalone PO archive')
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
    foley = args.assets / 'fulton-foley.sep'
    if foley.exists():
        data = foley.read_bytes()
        if len(data) < 0xd0 or len(data) > 0x9000 or data[:4] != b'SP\x44\x01' or \
                data[-4:] != b'EP\x44\x01' or struct.unpack_from('<4I', data, 4) != (len(data), 0x30, 0xb0, 0xd0):
            parser.error('fulton-foley.sep must be private bank 0x144 with four cues (fulton_sfx.py --foley)')
        (args.output / 'fulton-foley.sep').write_bytes(data)
        print('fulton-foley.sep: three attachment effects and soldier grunt')
    for name, data in donors.items():
        (args.output / name).write_bytes(data)


if __name__ == '__main__':
    main()
