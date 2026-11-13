#!/usr/bin/env python3
"""Read-only US 1.01 stage check; run after building test-camera/test-shadow.

nix develop ../tools --command python3 scripts/check-pop-stage-hooks.py DISC.iso
Checks four representative stages, without rebuilding corpora or launching games.
"""
import hashlib
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    spec = importlib.util.spec_from_file_location('stages', ROOT / 'scripts/stage-images.py')
    stages = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stages)
    image, files = stages.iso_files(sys.argv[1])

    def read(path):
        offset, size = files[path]
        image.seek(offset)
        return image.read(size)

    with image, tempfile.TemporaryDirectory(prefix='pop-hooks-') as directory:
        boot = read('/psp_game/sysdir/boot.bin')
        if hashlib.sha256(boot).hexdigest() != '61eaaaa771e5dabcca07b6df62137e39709caa62030f5cc8ddc809e333a6943b':
            raise SystemExit('Expected PO+ US 1.01 BOOT.BIN')
        for name in ('gsmenu', 'n01', 'bossrush01pt', 'briefing'):
            folder = '/psp_game/usrdir/stage/' + name + '/'
            prx = read(folder + name + '.prx')
            rlc = stages.zar_files(read(folder + '_zar'))[name + '.rlc']
            code = stages.stage_code(prx, rlc, 'pop')
            phoff = struct.unpack_from('<I', prx, 28)[0]
            count = struct.unpack_from('<H', prx, 44)[0]
            segments = [struct.unpack_from('<8I', prx, phoff + 32 * i) for i in range(count)]
            loads = [segment for segment in segments if segment[0] == 1]
            data = bytearray(max(s[2] + s[5] for s in loads))
            for s in loads:
                data[s[2]:s[2] + s[4]] = prx[s[1]:s[1] + s[4]]
            code_path = pathlib.Path(directory) / (name + '.code')
            data_path = pathlib.Path(directory) / (name + '.data')
            code_path.write_bytes(code)
            data_path.write_bytes(data)
            print(name, 'PRX SHA256', hashlib.sha256(prx).hexdigest(), flush=True)
            camera = subprocess.run([ROOT / 'out/test-camera', code_path], check=True,
                                    capture_output=True, text=True)
            expected = 'input=0 follow=0 look=0' if name == 'gsmenu' else 'input=1 follow=1 look=1'
            if expected not in camera.stdout:
                raise SystemExit(f'{name}: unexpected camera coverage\n{camera.stdout}')
            subprocess.run([ROOT / 'out/test-shadow', code_path, data_path], check=True)


if __name__ == '__main__':
    main()
