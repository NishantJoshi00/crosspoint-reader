#!/usr/bin/env python3
"""Fetch pinned upstream tools and regenerate the vendored embedded runtime."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
URLS = {
    'engine': 'https://github.com/arcprize/ARCEngine.git',
    'micropython': 'https://github.com/micropython/micropython.git',
    'ulab': 'https://github.com/v923z/micropython-ulab.git',
}


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def bootstrap(upstream, jobs, host=True):
    pins = json.loads((HERE / 'sources.json').read_text())
    upstream.mkdir(parents=True, exist_ok=True)
    for name, url in URLS.items():
        destination = upstream / name
        commit = pins[name + '_commit']
        if not destination.exists():
            run('git', 'clone', '--filter=blob:none', '--no-checkout', url, destination)
            run('git', '-C', destination, 'fetch', '--depth', '1', 'origin', commit)
            run('git', '-C', destination, 'checkout', '--detach', commit)
        actual = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != commit:
            raise RuntimeError(f'{destination} is at {actual}; expected {commit}. Use a fresh cache directory.')
    for patch in sorted((HERE / 'patches').glob('*.patch')):
        name = patch.name.split('-')[0]
        destination = upstream / name
        applied = subprocess.run(['git', '-C', str(destination), 'apply', '--reverse', '--check', str(patch)], capture_output=True).returncode == 0
        if not applied:
            run('git', '-C', destination, 'apply', '--check', patch)
            run('git', '-C', destination, 'apply', patch)
    modules = upstream / 'usermods'
    modules.mkdir(exist_ok=True)
    for name, target in [('arc', HERE / 'native'), ('ulab', upstream / 'ulab/code')]:
        link = modules / name
        if not link.exists():
            link.symlink_to(target, target_is_directory=True)
        elif link.resolve() != target.resolve():
            raise RuntimeError('Unexpected module link: ' + str(link))
    run('make', '-C', upstream / 'micropython/mpy-cross', f'-j{jobs}')
    vendor = ROOT / 'lib/ArcPlayer/vendor'
    # QSTR discovery does not track includes in the forwarding C++ module.
    # Generate from scratch so newly added native entry points are included.
    shutil.rmtree(HERE / 'embed/build-embed', ignore_errors=True)
    build_env = dict(os.environ)
    build_env['SOURCE_DATE_EPOCH'] = subprocess.check_output(['git', '-C', str(upstream / 'micropython'),
                                                            'show', '-s', '--format=%ct', 'HEAD'], text=True).strip()
    run('make', '-C', HERE / 'embed', f'MICROPYTHON_TOP={upstream / "micropython"}',
        f'USER_C_MODULES={modules}', f'PACKAGE_DIR={vendor / "micropython"}', env=build_env)
    shutil.copytree(upstream / 'ulab/code', vendor / 'ulab', dirs_exist_ok=True)
    for name in ('micropython', 'ulab'):
        shutil.copyfile(upstream / name / 'LICENSE', vendor / name / 'LICENSE')
    port = ROOT / 'lib/ArcPlayer/port'
    port.mkdir(exist_ok=True)
    for name in ('mpconfigport.h', 'mphalport.h'):
        shutil.copyfile(HERE / 'embed' / name, port / name)
    if host:
        # The QSTR scanner also needs a fresh pass for included native sources.
        shutil.rmtree(upstream / 'micropython/ports/unix/build-host', ignore_errors=True)
        run('make', '-C', upstream / 'micropython/ports/unix', f'VARIANT_DIR={HERE / "host"}',
            f'USER_C_MODULES={modules}', f'-j{jobs}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, default=ROOT / '.cache/arc/upstream')
    parser.add_argument('-j', '--jobs', type=int, default=8)
    parser.add_argument('--no-host', action='store_true')
    args = parser.parse_args()
    bootstrap(args.upstream.resolve(), args.jobs, not args.no_host)
