#!/usr/bin/env python3
"""Boot every stage on the disc once, with the interpreter off.

usage: scripts/stage-sweep.py po|pop DATA_DIR [SECONDS] [JOBS] [STAGE...]

Each stage is reached by redirecting the title's stage request to it
(<prefix>_STAGE_REDIRECT=title=<stage>), headless (<prefix>_NO_RENDER,
<prefix>_NO_AUDIO) and with PSPRECOMP_NO_INTERPRETER=1, for SECONDS (40) of
real time, JOBS (2) at once. A stage passes when the game asked for it, a
stage image found its corpus after that, and nothing stopped the run; code
without a recompiled entry stops it with "No recompiled function registered"
(a missing corpus, or a module library built from a compact one).
DATA_DIR is an installed user-data folder (EBOOT.ELF, settings.ini with the
disc image, ms0); every run works on a copy of it. The disc's stages come
from that image; STAGE names limit the sweep to those.
"""
import concurrent.futures
import importlib.util
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
EDITIONS = {'po': ('MGSPO_', 'PortableOpsNative'), 'pop': ('MGSPOP_', 'PortableOpsPlusNative')}


def disc_stages(disc):
    spec = importlib.util.spec_from_file_location('stages', ROOT / 'scripts/stage-images.py')
    stages = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stages)
    _, files = stages.iso_files(disc)
    return sorted({path.split('/')[-2] for path in files
                   if path.startswith('/psp_game/usrdir/stage/') and path.endswith('.prx')})


def run(program, prefix, profile, stage, seconds):
    env = dict(os.environ, PSPRECOMP_NO_INTERPRETER='1')
    env.update({prefix + 'DATA_DIR': str(profile), prefix + 'NO_RENDER': '1', prefix + 'NO_AUDIO': '1',
                prefix + 'STAGE_REDIRECT': 'title=' + stage})
    process = subprocess.Popen([str(program)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors='replace')
    try:
        output, _ = process.communicate(timeout=seconds)
        alive = False
    except subprocess.TimeoutExpired:
        process.kill()
        output, _ = process.communicate()
        alive = True
    lines = output.splitlines()
    requested = next((i for i, line in enumerate(lines) if line == '[presence] stage ' + stage
                      or line.startswith('[presence] stage ' + stage + ' ')), None)
    missing = [line for line in lines if 'No recompiled function registered' in line]
    stopped = [line for line in lines if line.startswith('Runtime stopped:')]
    if missing:
        return 'FAIL', missing[0]
    if requested is None:
        return 'NOT REACHED', stopped[0] if stopped else 'no request for the stage'
    # The loader says so when a stage image found its corpus (user_modules.cpp).
    if not any(line.startswith('[module] mgp_stage at ') and 'recompiled code' in line for line in lines[requested:]):
        return 'NO MODULE', stopped[0] if stopped else 'no stage module installed'
    if not alive:
        return 'STOPPED', stopped[0] if stopped else 'exited %d' % process.returncode
    return 'PASS', ''


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in EDITIONS:
        sys.exit(__doc__)
    edition, data = sys.argv[1], pathlib.Path(sys.argv[2])
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    prefix, name = EDITIONS[edition]
    program = ROOT / 'out' / edition / 'bin' / name
    settings = dict(line.split('=', 1) for line in (data / 'settings.ini').read_text().splitlines() if '=' in line)
    stages = sys.argv[5:] or disc_stages(settings['disc_image'])
    with tempfile.TemporaryDirectory(prefix='stage-sweep-') as scratch:
        # One copy of the profile per job: runs at once must not share saves.
        profiles = []
        for job in range(jobs):
            copy = pathlib.Path(scratch) / str(job)
            shutil.copytree(data, copy, ignore=shutil.ignore_patterns('screenshots', 'logs', 'pw', '*.log'))
            profiles.append(copy)
        free = list(profiles)
        results = {}

        def sweep(stage):
            profile = free.pop()
            try:
                return stage, run(program, prefix, profile, stage, seconds)
            finally:
                free.append(profile)

        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            for stage, (verdict, detail) in pool.map(sweep, stages):
                results[stage] = verdict
                print('%-12s %-11s %s' % (stage, verdict, detail), flush=True)
    counts = {}
    for verdict in results.values():
        counts[verdict] = counts.get(verdict, 0) + 1
    print('%d stages: %s' % (len(results), ', '.join('%d %s' % (n, v) for v, n in sorted(counts.items()))))
    sys.exit(1 if counts.get('FAIL') else 0)


if __name__ == '__main__':
    main()
