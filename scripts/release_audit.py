#!/usr/bin/env python3
"""Audit staged release files and record source/corpus fingerprints without running a game."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

TARGETS = {'po': 'PortableOpsNative', 'pop': 'PortableOpsPlusNative'}


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_modules(profile, stage, suffix):
    expected = {p.parent.name + suffix for p in (profile / 'overlays').glob('*/meta.txt')}
    actual = {p.name for p in (stage / 'overlays').iterdir() if p.is_file()}
    if not expected or actual != expected:
        raise ValueError(f'Module mismatch: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}')
    return sorted(expected)


def check_paths(path, roots):
    needles = {text.encode(encoding).lower() for root in roots
               for text in (str(root), str(root).replace('\\', '/'))
               for encoding in ('utf-8', 'utf-16-le')}
    overlap = max(map(len, needles), default=1) - 1
    tail = b''
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            data = tail + chunk.lower()
            if any(needle in data for needle in needles):
                raise ValueError(f'Build-machine path in package: {path.name}')
            tail = data[-overlap:] if overlap else b''


def check_file(path):
    if path.is_symlink():
        raise ValueError(f'Package contains symlink: {path.name}')
    lower = path.name.lower()
    if (lower.startswith(('eboot', 'umd_data')) or lower == 'param.sfo'
            or path.suffix.lower() in {'.iso', '.cso', '.pbp', '.prx', '.elf', '.ovl', '.bin', '.cpp', '.hpp'}):
        raise ValueError(f'Unexpected game data or source: {path.name}')
    with path.open('rb') as stream:
        header = stream.read(64)
        if header[:4] in (b'\x00PBP', b'~PSP', b'\x00PSF'):
            raise ValueError(f'PSP data: {path.name}')
        if header[:4] == b'\x7fELF' and (len(header) < 20 or header[4:6] != b'\x02\x01'
                                            or int.from_bytes(header[18:20], 'little') != 62):
            raise ValueError(f'Not x86-64 ELF: {path.name}')
        if header[:2] == b'MZ':
            stream.seek(int.from_bytes(header[60:64], 'little'))
            if stream.read(6) != b'PE\x00\x00\x64\x86':
                raise ValueError(f'Not x64 PE: {path.name}')
        stream.seek(32769)
        if stream.read(5) == b'CD001':
            raise ValueError(f'Disc image: {path.name}')


def fingerprint(paths, root):
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(root).as_posix().encode() + b'\0' + sha256(path).encode() + b'\n')
    return digest.hexdigest()


def revision(root):
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True))
    return {'commit': commit, 'dirty': dirty}


def audit(root, edition, stage):
    profile = root / 'profiles' / edition
    suffix = '.dll' if (stage / (TARGETS[edition] + '.exe')).is_file() else '.so'
    program = stage / (TARGETS[edition] + ('.exe' if suffix == '.dll' else ''))
    if not program.is_file():
        raise ValueError('Missing game executable')
    modules = check_modules(profile, stage, suffix)
    files = {}
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Package contains symlink: {path.name}')
        if not path.is_file() or path.name == 'RELEASE-MANIFEST.json':
            continue
        if any(part.lower() in {'ms0', 'savedata', 'toolchain', '.git'} for part in path.relative_to(stage).parts):
            raise ValueError(f'Unexpected directory: {path.relative_to(stage)}')
        check_file(path)
        check_paths(path, (root.resolve(), Path.home()))
        files[path.relative_to(stage).as_posix()] = sha256(path)
    corpora = [p for folder in ('generated', 'overlays', 'units')
               for p in (profile / folder).rglob('*') if p.is_file()]
    if not any(p.parent.name == 'generated' and p.suffix == '.cpp' for p in corpora):
        raise ValueError('Missing main corpus')
    # Include local modifications and untracked release tooling, without local paths.
    source = []
    for checkout in (root, root / 'portablekit'):
        names = subprocess.check_output(['git', '-C', str(checkout), 'ls-files', '-co',
                                         '--exclude-standard', '-z']).decode().split('\0')
        source.extend(checkout / name for name in set(names) if name and (checkout / name).is_file())
    return {'schema': 1, 'edition': edition, 'source': revision(root),
            'portablekit': revision(root / 'portablekit'),
            'source_sha256': fingerprint(source, root),
            'corpus_sha256': fingerprint(corpora, profile), 'modules': modules, 'files': files,
            'evidence': 'Packaging-time source snapshot; does not prove a clean rebuild or gameplay compatibility.'}


def verify(stage):
    report = json.loads((stage / 'RELEASE-MANIFEST.json').read_text())
    for name, expected in report['files'].items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or '\\' in name:
            raise ValueError('Unsafe manifest path')
        path = stage / relative
        if (path.is_symlink() or not path.resolve().is_relative_to(stage.resolve())
                or not path.is_file() or sha256(path) != expected):
            raise ValueError(f'Missing or modified packaged file: {name}')
    # Tarballs add a README and launcher after staging. Runtime directories
    # must still contain exactly the files recorded by the audit.
    for directory in ('overlays', 'lib', 'fonts', 'licenses'):
        expected = {name for name in report['files'] if name.startswith(directory + '/')}
        actual = {p.relative_to(stage).as_posix() for p in (stage / directory).rglob('*') if p.is_file()}
        if actual != expected:
            raise ValueError(f'Unexpected contents in {directory}')
    return len(report['files'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edition', choices=TARGETS, nargs='?')
    parser.add_argument('--verify', action='store_true', help='Verify an extracted package without source corpora')
    parser.add_argument('--stage', type=Path, required=True)
    args = parser.parse_args()
    if args.verify:
        print(f'Verified {verify(args.stage)} staged file hashes')
        return
    if args.edition is None:
        parser.error('edition is required when creating a manifest')
    root = Path(__file__).resolve().parent.parent
    report = audit(root, args.edition, args.stage)
    (args.stage / 'RELEASE-MANIFEST.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Package audit passed: {len(report["modules"])} modules, {len(report["files"])} files')


if __name__ == '__main__':
    main()
