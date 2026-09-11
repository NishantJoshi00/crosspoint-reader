#!/usr/bin/env python3
"""Build the host pack player with the same VM configuration as the device."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def build(destination, jobs=8, word_size=None):
    destination.mkdir(parents=True, exist_ok=True)
    core = ROOT / 'lib/ArcPlayer'
    mp = core / 'vendor/micropython'
    ulab = core / 'vendor/ulab'
    flags = ['-O2', '-ffunction-sections', '-fdata-sections', '-DARC_HOST_TRACE', '-ffp-contract=off']
    architecture = ['-m32', '-msse2', '-mfpmath=sse'] if word_size == 32 else []
    flags += architecture
    flags += ['-I' + str(path) for path in (core, core / 'port', mp, ulab)]
    sources = sorted((mp / 'py').glob('*.c'))
    sources += sorted(path for path in ulab.rglob('*.c') if path.name != 'io.c')
    sources += [mp / 'shared/runtime/gchelper_generic.c'] + sorted(core.glob('*.cpp'))

    def compile(source):
        target = destination / (str(source.relative_to(core)).replace('/', '_') + '.o')
        cpp = source.suffix == '.cpp'
        compiler = os.environ.get('CXX' if cpp else 'CC', 'c++' if cpp else 'cc')
        command = [compiler, *flags, '-std=c++20' if cpp else '-std=gnu99', '-c', str(source), '-o', str(target)]
        subprocess.run(command, check=True)
        return target

    with ThreadPoolExecutor(jobs) as pool:
        objects = list(pool.map(compile, sources))
    executable = destination / 'arc-player'
    subprocess.run([os.environ.get('CXX', 'c++'), *architecture, '-std=c++20', '-O2', '-I' + str(core),
                    str(ROOT / 'tools/arc/native_host.cpp'), *(str(obj) for obj in objects), '-lm', '-o', str(executable)], check=True)
    return executable


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / '.cache/arc/native')
    parser.add_argument('-j', '--jobs', type=int, default=8)
    parser.add_argument('--word-size', type=int, choices=[32], help='Linux multilib build for MCU-sized pointers')
    args = parser.parse_args()
    print(build(args.output.resolve(), args.jobs, args.word_size))
