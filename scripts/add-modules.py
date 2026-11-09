#!/usr/bin/env python3
"""Recompile dumped game modules, one library each.

usage: scripts/add-modules.py po|pop|pop-eu|pop-jp [DUMP_DIR]

A run with <prefix>_DUMP_MODULES=DUMP_DIR writes the code of every module it
loads, as it is in memory at its first run (the game patches stage code after
loading it), to DUMP_DIR/mod<BASE>_<NAME>_<HASH>.bin, with the import stubs
inside its code, if any, in a .stubs file beside it. Each dump not yet in
profiles/<edition>/overlays/ is wrapped in an ELF, recompiled at its base with
psp_recomp, and given the meta.txt (size = code_size = the dump's size, hash =
its FNV-1a) the build turns into a library the host installs when that code
runs again. Then its units are pooled (pool_units); without DUMP_DIR only that
runs. Then rebuild: cmake -S . -B out/<edition> && cmake --build out/<edition>.
"""
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Modules left to the interpreter.
SKIP = set()
NAME = re.compile(r'^mod([0-9A-F]{8})_(.+)_([0-9A-F]{16})$')

def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def main():
    edition, dumps = sys.argv[1], (sys.argv[2:] or [None])[0]
    recomp = ROOT / 'out' / edition / 'portablekit' / 'psp_recomp'
    wrap = ROOT / 'portablekit' / 'tools' / 'wrap_overlay.py'
    overlays = ROOT / 'profiles' / edition / 'overlays'
    for dump in sorted(pathlib.Path(dumps).glob('mod*.bin')) if dumps else []:
        match = NAME.match(dump.stem)
        if not match:
            continue
        base, name, digest = int(match.group(1), 16), match.group(2), int(match.group(3), 16)
        if name in SKIP:
            continue
        data = dump.read_bytes()
        assert fnv1a64(data) == digest, '%s: hash does not match its name' % dump.name
        prefix = re.sub(r'\W', '_', dump.stem)
        target = overlays / prefix
        if (target / 'meta.txt').exists():
            continue
        target.mkdir(parents=True, exist_ok=True)
        elf = target / 'module.elf'
        # Import stubs inside the code (<id>.stubs, offsets START-END a line)
        # stay out of the corpus: calls to them must reach the HLE.
        stubs = dump.with_suffix('.stubs')
        exclude = []
        if stubs.exists():
            for line in stubs.read_text().split():
                start, end = (int(text, 16) for text in line.split('-'))
                exclude += ['--exclude', '0x%X-0x%X' % (start, end)]
        entry = dump.with_suffix('.entry')
        if entry.exists():
            exclude += ['--entry', entry.read_text().strip()]
        subprocess.run([sys.executable, str(wrap), str(dump), '0x%08X' % base, str(elf)] + exclude, check=True)
        # --compact: only the code the analysis reaches, entries only where
        # control arrives; the rest runs in the interpreter.
        subprocess.run([str(recomp), str(elf), '--auto', str(target), '0x%08X' % base, '--prefix', prefix, '--compact'],
                       check=True, stdout=subprocess.DEVNULL)
        elf.unlink()
        (target / 'meta.txt').write_text('base=0x%08X\nname=%s\nsize=%d\ncode_size=%d\nhash=0x%016X\nsource=%s\n'
                                         % (base, name, len(data), len(data), digest, dump.name))
        print('%s: %d bytes at 0x%08X' % (prefix, len(data), base))
    pool_units(overlays)


def pool_units(overlays):
    """Compile each recompiled unit once however many modules carry it.

    Copies of a stage differ in a few units only (code the game patches per
    mission), so every unit moves to <profile>/units/, named by a hash of its
    code, into one directory per set of modules that use it. The build makes
    each directory one object library and links it into those modules;
    units.txt in a module lists its units, meta.txt the directories.
    """
    pool = overlays.parent / 'units'
    found = {path.stem: path for path in pool.glob('*/u*.cpp')}
    owners = {}
    for module in sorted(path for path in overlays.iterdir() if (path / 'meta.txt').exists()):
        prefix = module.name
        units = sorted(module.glob('generated_unit_*.cpp'))
        listing = module / 'units.txt'
        names = listing.read_text().split() if listing.exists() else []
        renames = {}
        for unit in units:
            index = int(unit.stem.rsplit('_', 1)[1])
            text = unit.read_text().replace('#include "%s_units.hpp"\n' % prefix, '').replace(prefix, '\0')
            name = 'u' + hashlib.sha1(text.encode()).hexdigest()[:16]
            if name not in found:
                found[name] = pool / 'new' / (name + '.cpp')
                found[name].parent.mkdir(parents=True, exist_ok=True)
                found[name].write_text(text.replace('\0', name))
            renames[index] = name
            names.append(name)
            unit.unlink()
        if renames:
            registry = module / ('%s_registry.cpp' % prefix)
            registry.write_text(re.sub(r'register_%s_unit_(\d+)\b' % prefix,
                                       lambda match: 'register_%s_unit_%s' % (renames[int(match.group(1))], match.group(1)),
                                       registry.read_text()))
            (module / ('%s_units.hpp' % prefix)).unlink(missing_ok=True)
            listing.write_text('\n'.join(names) + '\n')
        for name in names:
            owners.setdefault(name, []).append(prefix)
    groups = {}
    for name, users in owners.items():
        groups.setdefault('g' + hashlib.sha1(' '.join(users).encode()).hexdigest()[:12], []).append(name)
    for group, names in groups.items():
        (pool / group).mkdir(parents=True, exist_ok=True)
        for name in names:
            target = pool / group / (name + '.cpp')
            if found[name] != target:
                found[name].rename(target)
    for path in pool.glob('*/u*.cpp'):
        if path.stem not in groups.get(path.parent.name, ()):
            path.unlink()
    for directory in pool.glob('*'):
        if not any(directory.iterdir()):
            directory.rmdir()
    used = {}
    for group, names in groups.items():
        for user in owners[names[0]]:
            used.setdefault(user, []).append(group)
    for prefix, group_names in used.items():
        meta = overlays / prefix / 'meta.txt'
        text = meta.read_text()
        lines = [line for line in text.splitlines() if not line.startswith('units=')]
        updated = '\n'.join(lines + ['units=' + ' '.join(sorted(group_names))]) + '\n'
        if updated != text:
            meta.write_text(updated)


if __name__ == '__main__':
    main()
