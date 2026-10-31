#!/usr/bin/env python3
"""Package existing Windows x64 release builds; never compile anything."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

from release_audit import audit

ROOT = Path(__file__).resolve().parent.parent
# Only OS/graphics-driver components may be resolved from System32. Builder-installed runtimes
# (VC++, OpenSSL, etc.) must not accidentally become player prerequisites.
SYSTEM_DLLS = set("""advapi32 avrt bcrypt cfgmgr32 combase comctl32 comdlg32 crypt32
cryptbase d3d11 d3d12 d3dcompiler_47 dbghelp dinput8 dwmapi dxgi dxguid gdi32 hid
imm32 iphlpapi kernel32 kernelbase mf mfplat mfreadwrite mfuuid mpr msvcrt ncrypt
ntdll ole32 oleaut32 powrprof propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi
ucrtbase user32 userenv usp10 version winhttp wininet winmm winspool wintrust
ws2_32 wtsapi32 xinput1_4 vulkan-1""".split())

TARGETS = {'po': 'PortableOpsNative', 'pop': 'PortableOpsPlusNative'}


def cache_values(path):
    return dict(re.findall(r'^([^/#][^:=]*):[^=]+=(.*)$', path.read_text(), re.M))


def validate_build(build, edition, profile):
    cache = cache_values(build / 'CMakeCache.txt')
    for key, value in {'MGS_EDITION': edition, 'PORTABLEKIT_RELEASE': 'ON',
                       'PORTABLEKIT_RENDERER': 'ON', 'PORTABLEKIT_FFMPEG': 'bundled',
                       'CMAKE_BUILD_TYPE': 'Release'}.items():
        if cache.get(key) != value:
            raise ValueError(f'{build}: requires {key}={value}')
    commands = json.loads((build / 'compile_commands.json').read_text())
    main = next((entry['command'] for entry in commands
                 if entry['file'].replace('\\', '/').endswith('/host/main.cpp')), '')
    if ('PORTABLEKIT_HAS_RENDERER=1' not in main or 'PORTABLEKIT_HAS_FFMPEG=1' not in main
            or 'PORTABLEKIT_DEFAULT_GAME_DIR' in main):
        raise ValueError(f'{build}: missing renderer/audio or checkout fallback is enabled')
    if not list((profile / 'generated').glob('generated_unit_*.cpp')):
        raise ValueError(f'{profile}: no generated main corpus')
    modules = sorted(p.parent.name for p in (profile / 'overlays').glob('*/meta.txt'))
    if not modules:
        raise ValueError(f'{profile}: no module corpus')
    files = [build / 'bin' / (TARGETS[edition] + '.exe')]
    files += [build / 'bin' / 'overlays' / (name + '.dll') for name in modules]
    for path in files:
        if not path.is_file():
            raise ValueError(f'Missing native binary: {path}')
    return files


def imports(objdump, path):
    report = subprocess.check_output([str(objdump), '-p', str(path)], text=True)
    if 'file format coff-x86-64' not in report and 'file format pei-x86-64' not in report:
        raise ValueError(f'Not a Windows x64 binary: {path}')
    return re.findall(r'DLL Name:\s*(\S+)', report, re.I)


def copy_dependencies(stage, binaries, objdump, search, system):
    available = {p.name.lower(): p for directory in reversed(search)
                 for p in directory.glob('*.dll')}
    pending = list(binaries)
    bundled = {p.name.lower() for p in binaries}
    while pending:
        binary = pending.pop()
        for name in imports(objdump, binary):
            lower = name.lower()
            if lower in bundled:
                continue
            if lower.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue  # Windows API sets are supplied by the operating system.
            source = available.get(lower)
            if source is None:
                if lower.removesuffix('.dll') in SYSTEM_DLLS and (system / name).is_file():
                    continue
                raise ValueError(f'{binary.name}: missing dependency {name}')
            target = stage / name
            shutil.copy2(source, target)
            bundled.add(lower)
            pending.append(target)


def package(args, edition):
    build = args.build_root.resolve() / edition
    profile = ROOT / 'profiles' / edition
    binaries = validate_build(build, edition, profile)
    name = TARGETS[edition]
    output = ROOT / 'out' / f'release-windows-{edition}'
    output.mkdir(parents=True, exist_ok=True)
    # Stage in a fresh directory. Failed checks cannot overwrite a previous ZIP.
    with tempfile.TemporaryDirectory(prefix='stage-', dir=output) as temporary:
        stage = Path(temporary) / name
        (stage / 'overlays').mkdir(parents=True)
        (stage / 'fonts').mkdir()
        (stage / 'licenses').mkdir()
        copied = []
        for source in binaries:
            target = stage / ('overlays' if source.suffix == '.dll' else '') / source.name
            shutil.copy2(source, target)
            copied.append(target)
        copy_dependencies(stage, copied, args.toolchain / 'bin' / 'llvm-objdump.exe',
                          [build / 'bin', build / 'bin' / 'lib', args.toolchain / 'bin'],
                          Path(os.environ['SystemRoot']) / 'System32')
        for required in ('SDL3.dll', 'avcodec-61.dll', 'avutil-59.dll', 'swresample-5.dll'):
            if not (stage / required).is_file():
                raise ValueError(f'Required runtime library was not bundled: {required}')
        shutil.copy2(args.font, stage / 'fonts' / 'NotoSansCJKjp-Regular.otf')
        licenses = {
            'PortableKit-LICENSE.txt': ROOT / 'portablekit/LICENSE',
            'DearImGui-LICENSE.txt': ROOT / 'portablekit/third_party/imgui/LICENSE.txt',
            'tiny-AES-c-UNLICENSE.txt': ROOT / 'portablekit/third_party/tiny_aes/UNLICENSE',
            'xxHash-LICENSE.txt': ROOT / 'portablekit/third_party/xxhash/LICENSE',
            'SDL3-LICENSE.txt': args.sdl_license,
            'NotoSansCJK-OFL.txt': args.font_license,
            'llvm-mingw-LICENSE.txt': args.toolchain / 'LICENSE.TXT',
            'FFmpeg-LICENSE.txt': build / 'bin/FFmpeg-LICENSE.txt',
            'FFmpeg-SOURCE.txt': build / 'bin/FFmpeg-SOURCE.txt',
            'THIRD_PARTY_NOTICES.md': ROOT / 'packaging/windows/THIRD_PARTY_NOTICES.md',
        }
        mingw = args.toolchain / 'x86_64-w64-mingw32/share/mingw32'
        for source in mingw.glob('COPYING*'):
            licenses['mingw-w64-' + source.name + '.txt'] = source
        for dest, source in licenses.items():
            shutil.copy2(source, stage / 'licenses' / dest)
        shutil.copy2(ROOT / 'packaging/windows/README.txt', stage / 'README.txt')
        # --version exits before asset discovery. No installed data is touched.
        env = dict(os.environ, PATH=str(stage) + os.pathsep + os.environ['SystemRoot'] + r'\System32')
        version = subprocess.check_output([str(stage / (name + '.exe')), '--version'],
                                          cwd=temporary, env=env, text=True, timeout=30).strip()
        revision = subprocess.check_output(['git', '-C', str(ROOT), 'describe', '--always', '--dirty'], text=True).strip()
        (stage / 'BUILDINFO.txt').write_text(
            f'{name}\n{version}\nEdition: {edition}\nNative modules: {len(binaries) - 1}\n'
            f'Package label: {args.version}\nSource: {revision}\n', encoding='utf-8')
        (stage / 'RELEASE-MANIFEST.json').write_text(json.dumps(audit(ROOT, edition, stage), indent=2) + '\n', encoding='utf-8')
        archive_name = f'{name}-{args.version}-windows-x64.zip'
        temporary_zip = Path(temporary) / archive_name
        with zipfile.ZipFile(temporary_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(stage.parent))
        with zipfile.ZipFile(temporary_zip) as archive:
            if archive.testzip() is not None:
                raise ValueError('ZIP integrity check failed')
        dist = output / 'dist'
        dist.mkdir(exist_ok=True)
        destination = dist / archive_name
        temporary_zip.replace(destination)
        with destination.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        destination.with_suffix('.zip.sha256').write_text(f'{digest}  {archive_name}\n', encoding='ascii')
        print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edition', choices=['po', 'pop', 'both'], nargs='?', default='both')
    parser.add_argument('--build-root', type=Path, default=ROOT / 'out/windows')
    parser.add_argument('--toolchain', type=Path, required=True, help='llvm-mingw x64 UCRT directory')
    parser.add_argument('--sdl-license', type=Path, required=True)
    parser.add_argument('--font', type=Path, required=True)
    parser.add_argument('--font-license', type=Path, required=True)
    parser.add_argument('--version', required=True, help='Archive label, for example 0.1.0-local')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.version):
        parser.error('version must contain only letters, digits, dots, underscores and hyphens')
    if os.name != 'nt':
        parser.error('Run on Windows x64: packaging verifies the executable starts')
    for edition in TARGETS if args.edition == 'both' else [args.edition]:
        package(args, edition)


if __name__ == '__main__':
    main()
