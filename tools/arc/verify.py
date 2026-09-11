#!/usr/bin/env python3
"""Compare actual .bin pack execution against the pinned Python SDK on every level."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def verify(args):
    inventory = json.loads((args.packs / 'games.json').read_text())
    args.logs.mkdir(parents=True, exist_ok=True)
    cases = [(entry['game'], level) for entry in inventory for level in range(entry['levels'])
             if not args.game or entry['game'].startswith(args.game)]

    def case(item):
        game, level = item
        traces, errors, heap = [], [], 0
        commands = [
            ('sdk', [sys.executable, str(HERE / 'reference_case.py'), str(args.sources / (game + '.py')), str(level), str(args.steps)]),
            ('native', [str(args.player), str(args.packs / (game + '.bin')), str(args.heap), str(level), str(args.steps)]),
        ]
        for engine, command in commands:
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                (args.logs / f'{game}-{level}-{engine}.log').write_text(result.stdout + result.stderr)
                rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
                heap = max([heap] + [row.get('heap', 0) for row in rows])
                traces.append([{key: row[key] for key in ('level', 'state', 'moves', 'crc')} for row in rows])
                if result.returncode:
                    errors.append(engine + ': ' + (result.stdout + result.stderr)[-400:])
            except subprocess.TimeoutExpired:
                traces.append([])
                errors.append(engine + ': timeout')
        match = not errors and len(traces[0]) == args.steps + 2 and traces[0] == traces[1]
        return dict(game=game, level=level, match=match, heap=heap, errors=errors)

    with ThreadPoolExecutor(args.jobs) as pool:
        results = []
        for result in pool.map(case, cases):
            results.append(result)
            if not result['match']:
                print(json.dumps(result), flush=True)
            if len(results) % 10 == 0:
                print(f'{len(results)}/{len(cases)} checked', flush=True)
    (args.logs / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    passed = sum(result['match'] for result in results)
    print(f'{passed}/{len(results)} level traces match; max live host heap {max(r["heap"] for r in results)} bytes')
    return passed == len(cases) and bool(cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, default=ROOT / '.cache/arc/sources')
    parser.add_argument('--packs', type=Path, default=ROOT / '.cache/arc/packs')
    parser.add_argument('--player', type=Path, default=ROOT / '.cache/arc/native/arc-player')
    parser.add_argument('--logs', type=Path, default=ROOT / '.cache/arc/verification')
    parser.add_argument('--heap', type=int, default=2 * 1024 * 1024)
    parser.add_argument('--steps', type=int, default=24)
    parser.add_argument('--game')
    parser.add_argument('-j', '--jobs', type=int, default=4)
    raise SystemExit(0 if verify(parser.parse_args()) else 1)
