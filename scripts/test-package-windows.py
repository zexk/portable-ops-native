#!/usr/bin/env python3
"""Run with Python 3.11+ on any host; no compiler or Windows binaries needed."""
import importlib.util
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('packager', Path(__file__).with_name('package-windows.py'))
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def rejects(work, message):
    try:
        work()
    except ValueError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError('Invalid package accepted')


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    for edition, target in packager.TARGETS.items():
        build, profile = root / edition, root / ('profile-' + edition)
        (build / 'bin/overlays').mkdir(parents=True)
        (profile / 'generated').mkdir(parents=True)
        (profile / 'overlays/module').mkdir(parents=True)
        (profile / 'generated/generated_unit_0000.cpp').touch()
        (profile / 'overlays/module/meta.txt').touch()
        cache = (f'MGS_EDITION:STRING={edition}\nPORTABLEKIT_RELEASE:BOOL=ON\n'
                 'PORTABLEKIT_RENDERER:BOOL=ON\nPORTABLEKIT_FFMPEG:STRING=bundled\n'
                 'CMAKE_BUILD_TYPE:STRING=Release\n')
        (build / 'CMakeCache.txt').write_text(cache)
        command = [{'file': 'C:/repo/portablekit/host/main.cpp',
                    'command': 'clang++ -DPORTABLEKIT_HAS_RENDERER=1 -DPORTABLEKIT_HAS_FFMPEG=1'}]
        (build / 'compile_commands.json').write_text(json.dumps(command))
        (build / 'bin' / (target + '.exe')).touch()
        server = edition.startswith('pop')
        if server:
            (build / 'bin/mpo-server.exe').touch()
        module = build / 'bin/overlays/module.dll'
        module.touch()
        assert len(packager.validate_build(build, edition, profile)) == 2 + server
        module.unlink()
        rejects(lambda: packager.validate_build(build, edition, profile), 'Missing native binary')
        module.touch()
        (build / 'CMakeCache.txt').write_text(cache.replace('RELEASE:BOOL=ON', 'RELEASE:BOOL=OFF'))
        rejects(lambda: packager.validate_build(build, edition, profile), 'PORTABLEKIT_RELEASE=ON')
        (build / 'CMakeCache.txt').write_text(cache)
        command[0]['command'] += ' -DPORTABLEKIT_DEFAULT_GAME_DIR=checkout'
        (build / 'compile_commands.json').write_text(json.dumps(command))
        rejects(lambda: packager.validate_build(build, edition, profile), 'checkout fallback')
    stage, libraries, system = (root / name for name in ('stage', 'libraries', 'System32'))
    for directory in (stage, libraries, system):
        directory.mkdir()
    program = stage / 'Game.exe'
    program.touch()
    for name in ('SDL3.dll', 'libc++.dll'):
        (libraries / name).touch()
    (system / 'KERNEL32.dll').touch()
    dependencies = {'Game.exe': ['SDL3.dll', 'KERNEL32.dll', 'api-ms-win-crt-runtime-l1-1-0.dll'],
                    'SDL3.dll': ['libc++.dll'], 'libc++.dll': ['SDL3.dll']}
    with patch.object(packager, 'imports', side_effect=lambda _, path: dependencies[path.name]):
        packager.copy_dependencies(stage, [program], None, [libraries], system)
    assert {p.name for p in stage.iterdir()} == {'Game.exe', 'SDL3.dll', 'libc++.dll'}
    with patch.object(packager, 'imports', return_value=['missing.dll']):
        rejects(lambda: packager.copy_dependencies(stage, [program], None, [libraries], system),
                'missing dependency missing.dll')
    (system / 'vcruntime140.dll').touch()
    with patch.object(packager, 'imports', return_value=['vcruntime140.dll']):
        rejects(lambda: packager.copy_dependencies(stage, [program], None, [libraries], system),
                'missing dependency vcruntime140.dll')
    with patch.object(packager.subprocess, 'check_output', return_value='file format coff-i386'):
        rejects(lambda: packager.imports('objdump', program), 'Not a Windows x64 binary')
    with patch.object(packager.subprocess, 'check_output',
                      return_value='file format coff-x86-64\n DLL Name: SDL3.dll\n'):
        assert packager.imports('objdump', program) == ['SDL3.dll']
print('Windows package checks passed: all editions, missing modules, release flags, DLL closure and x64 format')
