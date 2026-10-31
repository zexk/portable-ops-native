#!/usr/bin/env python3
from pathlib import Path
import tempfile
import json
from release_audit import check_file, check_modules, check_paths, fingerprint, sha256, verify


def rejects(work):
    try:
        work()
    except ValueError:
        return
    raise AssertionError('Unsafe package accepted')


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    profile, stage = root / 'profile', root / 'stage'
    (profile / 'overlays/required').mkdir(parents=True)
    (profile / 'overlays/required/meta.txt').touch()
    (stage / 'overlays').mkdir(parents=True)
    (stage / 'overlays/wrong.so').touch()
    rejects(lambda: check_modules(profile, stage, '.so'))  # Same count, wrong identity.
    (stage / 'overlays/wrong.so').rename(stage / 'overlays/required.so')
    assert check_modules(profile, stage, '.so') == ['required.so']
    file = root / 'renamed.dat'
    for header in (b'~PSP', b'\x00PBP', b'\x00PSF', b'\x7fELF' + bytes(60)):
        file.write_bytes(header)
        rejects(lambda: check_file(file))
    file.write_bytes(bytes(32769) + b'CD001')
    rejects(lambda: check_file(file))
    file.write_bytes(b'x' * (1024 * 1024 - 3) + b'/private/builder/repo')
    rejects(lambda: check_paths(file, [Path('/private/builder')]))
    file.write_bytes(b'license text')
    check_file(file)
    before = fingerprint([file], root)
    file.write_bytes(b'changed license text')
    assert fingerprint([file], root) != before
    manifest = {'files': {'overlays/required.so': sha256(stage / 'overlays/required.so')}}
    (stage / 'RELEASE-MANIFEST.json').write_text(json.dumps(manifest))
    assert verify(stage) == 1
    (stage / 'overlays/required.so').write_bytes(b'changed')
    rejects(lambda: verify(stage))
    manifest['files'] = {'../outside': '0' * 64}
    (stage / 'RELEASE-MANIFEST.json').write_text(json.dumps(manifest))
    rejects(lambda: verify(stage))
print('Release audit checks passed')
