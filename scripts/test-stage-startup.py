#!/usr/bin/env python3
"""Synthetic PRX: preserve startup entry and separate PRX/engine relocation.

Run: nix develop ../tools --command python3 scripts/test-stage-startup.py
"""
import importlib.util
import pathlib
import struct
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stages = load('stages', ROOT / 'scripts/stage-images.py')
modules = load('modules', ROOT / 'scripts/add-modules.py')
wrap = load('wrap', ROOT / 'portablekit/tools/wrap_overlay.py')
# One load segment, its own J26 relocation, an engine J26 relocation,
# and module_start without a stack prologue at offset 8.
prx = bytearray(512)
struct.pack_into('<I', prx, 0x18, 8)
struct.pack_into('<II', prx, 0x1C, 52, 256)
struct.pack_into('<HxxHH', prx, 0x2C, 1, 4, 1)
struct.pack_into('<8I', prx, 52, 1, 96, 0, 0, 32, 32, 5, 4)
struct.pack_into('<4I', prx, 96, 0x0C000010, 0x0C000020, 0x03E00008, 0)
prx[160:176] = b'\0.sceStub.text\0\0'
struct.pack_into('<10I', prx, 296, 0, 3, 0, 0, 160, 16, 0, 0, 1, 0)
struct.pack_into('<10I', prx, 336, 1, 1, 6, 16, 112, 0, 0, 0, 4, 0)
struct.pack_into('<10I', prx, 376, 0, 0x700000A0, 0, 0, 200, 8, 0, 0, 4, 8)
struct.pack_into('<II', prx, 200, 0, 4)
rlc = struct.pack('<II', 4, 4)
startup = stages.stage_code(prx, rlc, 'po', linked=False)
linked = stages.stage_code(prx, rlc, 'po')
base = stages.EDITIONS['po']['stage']
assert len(startup) == len(linked) == 16
assert struct.unpack_from('<I', startup)[0] == 0x0C000010 + (base >> 2)
assert struct.unpack_from('<I', startup, 4)[0] == 0x0C000020
assert struct.unpack_from('<I', linked, 4)[0] == 0x0C000020 + (stages.EDITIONS['po']['engine'][0] >> 2)
assert stages.fnv1a64(startup) != stages.fnv1a64(linked)
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    dump, elf = root / 'startup.bin', root / 'startup.elf'
    dump.write_bytes(startup)
    assert wrap.main(['wrap_overlay.py', str(dump), hex(base), str(elf),
                      '--entry', hex(base + 8)]) == 0
    assert struct.unpack_from('<I', elf.read_bytes(), 0x18)[0] == base + 8
    assert wrap.main(['wrap_overlay.py', str(dump), hex(base), str(elf),
                      '--entry', hex(base + 1)]) == 1
    # Pooling must retain startup images even when other code still contains
    # calls that the engine has not linked yet.
    module = root / 'overlays/mod_mgp_stage_startup'
    module.mkdir(parents=True)
    (module / 'meta.txt').write_text('name=startup\n')
    (module / 'generated_unit_0000.cpp').write_text(
        'ctx.pc = 0x00100000u;\n    if (rt.invoke_chained_call(ctx)) {}\n')
    (module / 'mod_mgp_stage_startup_registry.cpp').write_text(
        'register_mod_mgp_stage_startup_unit_0(runtime);\n')
    modules.pool_units(root / 'overlays')
    assert (module / 'meta.txt').exists()
    assert len((module / 'units.txt').read_text().split()) == 1
print('stage startup image checks passed')
