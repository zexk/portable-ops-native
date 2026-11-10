#!/usr/bin/env python3
"""Check or install converted Fulton assets, without enabling the feature.

Accepts the source-independent output of prepare-fulton-assets.py, not a raw
PW ISO or MC installation. Both source converters must eventually produce
this same layout. No donor source is accessed during validation or play.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import zlib

spec = importlib.util.spec_from_file_location(
    'prepare_fulton', Path(__file__).with_name('prepare-fulton-assets.py'))
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)

MAX_FILE = 32 * 1024 * 1024
MAX_ARCHIVE = 64 * 1024 * 1024
# ponytail: snapshot install is capped at 512 MiB; stream into staging if
# whole-game bundles outgrow the current prepared-stage experiment.
MAX_BUNDLE = 512 * 1024 * 1024
HEX = re.compile(r'[0-9a-f]+\Z')
EXTRAS = {'fult_place.mtar': 0x10000, 'fulton.sep': 0x8000,
          'fulton-lift.sep': 0x11000, 'fulton-foley.sep': 0x9000}
CORE = ('fult_item_balloon.mdp', 'fult_item_balloon.txp',
        'fult_item_body.mdp', 'fult_item_body.txp', 'fult_enemy.mtar')


def read_file(root, name, limit):
    path = root / name
    if name != Path(name).name or any(c in name for c in '/\\:') or name in ('.', '..'):
        raise ValueError(f'unsafe asset filename: {name!r}')
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise ValueError(f'missing, oversized or linked asset: {name}')
    data = path.read_bytes()
    if not data or len(data) > limit:
        raise ValueError(f'empty or oversized asset: {name}')
    return data


def stage_assets(raw):
    if len(raw) < 4 or not 4 <= struct.unpack_from('<I', raw)[0] <= MAX_ARCHIVE:
        raise ValueError('invalid ZAR size')
    stream = zlib.decompressobj()
    plain = stream.decompress(raw[4:], MAX_ARCHIVE + 1)
    if not stream.eof or len(plain) != struct.unpack_from('<I', raw)[0]:
        raise ValueError('invalid or oversized ZAR stream')
    files = prepare.stage_images.zar_files(raw)
    if 'cache.dar' not in files:
        raise ValueError('stage has no cache.dar')
    assets = prepare.unpack(files['cache.dar'])
    for name in CORE:
        data = assets.get(name, b'')
        if name.endswith('.mdp') and (len(data) < 0x50 or data[:4] != b'MDP '):
            raise ValueError(f'missing or invalid converted model: {name}')
        if name.endswith('.txp') and (len(data) < 0x20 or struct.unpack_from('<I', data, 4)[0] != 1):
            raise ValueError(f'missing or unconverted texture pack: {name}')
        if name.endswith('.mtar'):
            if len(data) < 32 or struct.unpack_from('<3I', data) != (0x7261744d, 19, 2):
                raise ValueError(f'missing or unconverted motion archive: {name}')
            if struct.unpack_from('<I', data, 16)[0] != 0x10000:
                raise ValueError(f'prepared or invalid motion archive: {name}')


def check(root):
    """Return a verified snapshot of files needed to run this prepared bundle."""
    root = Path(root)
    index = read_file(root, 'index.txt', 256 * 1024)
    result = {'index.txt': index}
    lines = index.decode('ascii').splitlines()
    if not 1 <= len(lines) <= 256:
        raise ValueError('expected 1 to 256 prepared stages')
    for line in lines:
        fields = line.split()
        if len(fields) != 5:
            raise ValueError('invalid stage index row')
        original, size, prefix, replacement, name = fields
        if any(len(value) != count or not HEX.fullmatch(value)
               for value, count in ((original, 64), (prefix, 128), (replacement, 64))):
            raise ValueError('invalid stage index hash or prefix')
        if not size.isdecimal():
            raise ValueError('invalid stage size')
        size = int(size)
        if not 64 <= size <= MAX_FILE or not name.endswith('.zar') or name == '.zar' or name in result:
            raise ValueError('invalid stage size, name or duplicate')
        data = read_file(root, name, MAX_FILE)
        if len(data) != size or hashlib.sha256(data).hexdigest() != replacement:
            raise ValueError(f'stage size/hash mismatch: {name}')
        stage_assets(data)
        result[name] = data
        if sum(len(data) for data in result.values()) > MAX_BUNDLE:
            raise ValueError('bundle exceeds 512 MiB snapshot limit')
    for name, limit in EXTRAS.items():
        if not (root / name).exists() and not (root / name).is_symlink():
            continue
        data = read_file(root, name, limit)
        if name.endswith('.mtar'):
            if len(data) < 192 or struct.unpack_from('<3I', data) != (0x7261744d, 19, 1):
                raise ValueError('invalid converted placement archive')
            if struct.unpack_from('<I', data, 16)[0] != 0x10000:
                raise ValueError('prepared or invalid placement archive')
        else:
            bank = {'fulton.sep': 0x31, 'fulton-lift.sep': 0x32, 'fulton-foley.sep': 0x144}[name]
            if (len(data) < 0x30 or data[:4] != b'SP' + struct.pack('<H', bank) or
                    data[-4:] != b'EP' + struct.pack('<H', bank) or
                    struct.unpack_from('<I', data, 4)[0] != len(data)):
                raise ValueError(f'invalid converted sound bank: {name}')
        result[name] = data
    return result


def install(source, data_dir):
    files = check(source)
    destination = Path(data_dir) / 'pw' / 'fulton'
    if destination.exists() or destination.is_symlink():
        raise ValueError(f'destination already exists: {destination}')
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Publish a verified snapshot only after every file has been copied. An
    # existing installation is never replaced by this initial importer.
    with tempfile.TemporaryDirectory(prefix='.fulton-', dir=destination.parent) as temporary:
        staged = Path(temporary) / 'fulton'
        staged.mkdir()
        for name, data in files.items():
            (staged / name).write_bytes(data)
        check(staged)
        staged.rename(destination)
    return destination


def self_test():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / 'source'
        source.mkdir()
        donors = {name: b'MDP ' + bytes(76) if name.endswith('.mdp') else
                  struct.pack('<8I', 0, 1, 0, 32, 0, 32, 0, 0) if name.endswith('.txp') else
                  struct.pack('<8I', 0x7261744d, 19, 2, 0, 0x10000, 0, 0, 0)
                  for name in CORE}
        plain = prepare.pack({'cache.dar': prepare.pack(donors)})
        data = struct.pack('<I', len(plain)) + zlib.compress(plain)
        data += bytes(max(0, 64 - len(data)))
        (source / 'st01.zar').write_bytes(data)
        row = f'{"a" * 64} {len(data)} {"b" * 128} {hashlib.sha256(data).hexdigest()} st01.zar\n'
        (source / 'index.txt').write_text(row)
        expected = check(source)
        destination = install(source, root / 'profile')
        assert check(destination) == expected
        assert not (root / 'profile/settings.ini').exists()  # Import never enables a feature.
        for bad in (row.replace('st01.zar', '../st01.zar'), row + row, row.replace('a' * 64, 'z' * 64)):
            (source / 'index.txt').write_text(bad)
            try:
                check(source)
            except ValueError:
                pass
            else:
                raise AssertionError('unsafe or invalid index accepted')
        (source / 'index.txt').write_text(row)
        (source / 'st01.zar').write_bytes(data + b'changed')
        try:
            check(source)
        except ValueError:
            pass
        else:
            raise AssertionError('tampered stage accepted')
        for path in source.iterdir():
            path.unlink()
        source.rmdir()
        assert check(destination) == expected  # No donor path remains necessary.
        try:
            install(destination, root / 'profile')
        except ValueError:
            pass
        else:
            raise AssertionError('existing installation overwritten')
    print('Fulton bundle validation, isolated install and source independence checks passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('command', nargs='?', choices=('check', 'install'))
    parser.add_argument('bundle', nargs='?', type=Path)
    parser.add_argument('--data-dir', type=Path, help='port user-data directory, not its saves directory')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.command or not args.bundle or (args.command == 'install' and not args.data_dir):
        parser.error('provide check BUNDLE or install BUNDLE --data-dir PORT_DATA')
    try:
        if args.command == 'install':
            print(f'Installed: {install(args.bundle, args.data_dir)}; feature settings unchanged')
        else:
            files = check(args.bundle)
            print(f'Valid Fulton bundle: {len(files) - 1} files, {sum(n.endswith(".zar") for n in files)} stages')
    except (OSError, ValueError, UnicodeError, struct.error, zlib.error) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
