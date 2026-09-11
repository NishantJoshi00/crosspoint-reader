#!/usr/bin/env python3
"""Exercise VM compatibility and compact history inside a real bytecode pack."""
import argparse
import hashlib
from pathlib import Path
import random
import subprocess

import numpy as np
from pack import assemble

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mpy-cross', type=Path, required=True)
parser.add_argument('--player', type=Path, default=ROOT / '.cache/arc/native/arc-player')
args = parser.parse_args()
work = ROOT / '.cache/arc/primitives'
work.mkdir(parents=True, exist_ok=True)
python_random = random.Random(73)
expected_random = [python_random.randint(-17, 431) for _ in range(100)]
np.random.seed(51)
expected_shuffle = list(range(50))
np.random.shuffle(expected_shuffle)
source = '''
import numpy as np
import _arc_native as native
import _arc_compat as compat
from arc_history import Frozen, History
from arc_random import Random
from copy import deepcopy

class Snapshot:
    def __init__(self):
        self.values = {'x': [1, 2, 3], 'text': 'same', 'float': 0.125}
        self.alias = self.values['x']
        self.pixels = np.array([[1, 1, -2], [4, 5, 5]], dtype=np.int8)[:, ::-1]

def open_game(name, level):
    rng = Random(73)
    assert [rng.randint(-17, 431) for _ in range(100)] == EXPECTED_RANDOM
    np.random.seed(51)
    shuffled = list(range(50))
    np.random.shuffle(shuffled)
    assert shuffled == EXPECTED_SHUFFLE
    d = {str(i): i for i in range(100)}
    assert list(d.values()) == list(range(100))
    del d['5']
    d['5'] = 105
    assert list(d.values())[-1] == 105
    values = [(i % 3, i) for i in range(30)]
    assert compat.sorted(values, key=lambda v: v[0], reverse=True) == [v for key in (2, 1, 0) for v in values if v[0] == key]
    a = np.array([[0, 1], [2, 3]], dtype=np.int8)
    assert 1 in a and 7 not in a
    a[a > 1] = 9
    assert np.tolist(a) == [[0, 1], [9, 9]]
    a[:, 2:] = 7
    a[0, 0] = 8
    assert a[0, 0] == 8
    assert list(np.round(np.array([-2.5, -1.5, -0.5, 0.5, 1.5, 2.5]))) == [-2, -2, 0, 0, 2, 2]
    for dtype in (np.int8, np.uint8, np.int16, np.uint16, float, bool):
        for shape in ((0, 2), (2, 3), (32, 32)):
            original = np.ones(shape, dtype=dtype)
            restored = native.unpack_array(native.pack_array(original))
            assert original.shape == restored.shape
            assert original.dtype == restored.dtype
            assert np.array_equal(original, restored)
    snapshot = Snapshot()
    restored = Frozen(snapshot).thaw()
    assert type(restored) is Snapshot
    assert restored.alias is restored.values['x']
    assert np.array_equal(restored.pixels, snapshot.pixels)
    assert restored.values == snapshot.values
    cycle = []
    cycle.append(cycle)
    restored_cycle = Frozen(cycle).thaw()
    assert restored_cycle[0] is restored_cycle
    copied = deepcopy(snapshot)
    assert copied.alias is copied.values['x']
    assert copied.alias is not snapshot.alias
    history = History()
    history.append(snapshot)
    history.compact()
    history[0].values['x'][0] = 99
    history.compact()
    assert history.pop().values['x'][0] == 99
    return np.zeros((64, 64), dtype=np.int8)

def info():
    return 0, 0, 1, 2, 0
'''.replace('EXPECTED_RANDOM', repr(expected_random)).replace('EXPECTED_SHUFFLE', repr(expected_shuffle))
modules = {}
for path in (HERE / 'compat').glob('*.py'):
    if path.name == '_arc_boot.py':
        continue
    target = work / path.with_suffix('.mpy').name
    subprocess.run([str(args.mpy_cross), '-msmall-int-bits=31', '-s', path.name, '-o', str(target), str(path)], check=True)
    modules[target.name] = target.read_bytes()
boot = work / '_arc_boot.py'
boot.write_text(source)
target = boot.with_suffix('.mpy')
subprocess.run([str(args.mpy_cross), '-msmall-int-bits=31', '-s', boot.name, '-o', str(target), str(boot)], check=True)
modules[target.name] = target.read_bytes()
pack = work / 'primitives.bin'
pack.write_bytes(assemble('test-core', 1, hashlib.sha256(source.encode()).hexdigest(), modules))
subprocess.run([str(args.player), str(pack), '1048576', '0', '0'], check=True)
print('PASS: random, dictionary order, stable sort, arrays, history aliases/cycles, copy and reopen')
