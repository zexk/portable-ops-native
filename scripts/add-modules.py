#!/usr/bin/env python3
"""Recompile dumped game modules, one library each.

usage: scripts/add-modules.py po|pop DUMP_DIR

A run with <prefix>_DUMP_MODULES=DUMP_DIR writes the code of every module it
loads, as it is in memory at its first run (the game patches stage code after
loading it), to DUMP_DIR/mod<BASE>_<NAME>_<HASH>.bin. Each dump not yet in
profiles/<edition>/overlays/ is wrapped in an ELF, recompiled at its base with
psp_recomp, and given the meta.txt (size = code_size = the dump's size, hash =
its FNV-1a) the build turns into a library the host installs when that code
runs again. Then rebuild: cmake -S . -B out/<edition> && cmake --build out/<edition>.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NAME = re.compile(r'^mod([0-9A-F]{8})_(.+)_([0-9A-F]{16})$')


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def main():
    edition, dumps = sys.argv[1:3]
    recomp = ROOT / 'out' / edition / 'portablekit' / 'psp_recomp'
    wrap = ROOT / 'portablekit' / 'tools' / 'wrap_overlay.py'
    overlays = ROOT / 'profiles' / edition / 'overlays'
    for dump in sorted(pathlib.Path(dumps).glob('mod*.bin')):
        match = NAME.match(dump.stem)
        if not match:
            continue
        base, name, digest = int(match.group(1), 16), match.group(2), int(match.group(3), 16)
        data = dump.read_bytes()
        assert fnv1a64(data) == digest, '%s: hash does not match its name' % dump.name
        prefix = re.sub(r'\W', '_', dump.stem)
        target = overlays / prefix
        if (target / 'meta.txt').exists():
            continue
        target.mkdir(parents=True, exist_ok=True)
        elf = target / 'module.elf'
        subprocess.run([sys.executable, str(wrap), str(dump), '0x%08X' % base, str(elf)], check=True)
        subprocess.run([str(recomp), str(elf), '--auto', str(target), '0x%08X' % base, '--prefix', prefix],
                       check=True, stdout=subprocess.DEVNULL)
        elf.unlink()
        (target / 'meta.txt').write_text('base=0x%08X\nname=%s\nsize=%d\ncode_size=%d\nhash=0x%016X\nsource=%s\n'
                                         % (base, name, len(data), len(data), digest, dump.name))
        print('%s: %d bytes at 0x%08X' % (prefix, len(data), base))


if __name__ == '__main__':
    main()
