#!/usr/bin/env python3
"""Compile portable MicroPython bytecode and assemble SD game packs."""
import argparse
import ast
import json
from pathlib import Path
import struct
import subprocess
import zlib

HERE = Path(__file__).resolve().parent
HEADER_SIZE = 72
ENTRY_SIZE = 60


def assemble(game_id, levels, source_sha, modules):
    payload, directory = bytearray(), bytearray()
    offset = HEADER_SIZE + ENTRY_SIZE * len(modules)
    if not 0 < len(modules) <= 64 or not 0 < levels <= 65535 or len(game_id.encode('ascii')) >= 16:
        raise ValueError('Pack metadata exceeds player limits')
    for module, data in sorted(modules.items()):
        encoded = module.encode('ascii')
        if len(encoded) >= 48 or data[:3] != b'M\x06\x00':
            raise ValueError('Invalid module: ' + module)
        directory.extend(struct.pack('<48sIII', encoded, offset, len(data), zlib.crc32(data)))
        payload.extend(data)
        offset += len(data)
    body = directory + payload
    header = struct.pack('<8sHHIHH16s32sI', b'ARCPACK1', 1, 1, offset, len(modules), levels,
                         game_id.encode('ascii'), bytes.fromhex(source_sha), zlib.crc32(body))
    if len(header + body) > 3 * 1024 * 1024:
        raise ValueError('Pack exceeds player size limit')
    return header + body


def build(embedded, sources, cross, output):
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((HERE / "sources.json").read_text())
    game_names = {g['id'].split('-')[0] for g in manifest['games']}
    compiled = {}
    for source in sorted(embedded.rglob('*.py')):
        target = source.with_suffix('.mpy')
        name = source.relative_to(embedded).as_posix()
        subprocess.run([str(cross), '-msmall-int-bits=31', '-s', name, '-o', str(target), str(source)], check=True)
        compiled[name[:-3] + '.mpy'] = target.read_bytes()
    common = {name: data for name, data in compiled.items() if name[:-4] not in game_names}
    inventory = []
    for game in manifest['games']:
        name = game['id'].split('-')[0]
        modules = dict(common)
        modules[name + '.mpy'] = compiled[name + '.mpy']
        tree = ast.parse((sources / (game['id'] + '.py')).read_text())
        levels = next(len(node.value.elts) for node in tree.body if isinstance(node, ast.Assign)
                      and any(isinstance(target, ast.Name) and target.id == 'levels' for target in node.targets))
        target = output / (game['id'] + '.bin')
        target.write_bytes(assemble(game['id'], levels, game['sha256'], modules))
        inventory.append({'game': game['id'], 'levels': levels, 'bytes': target.stat().st_size})
    (output / 'games.json').write_text(json.dumps(inventory, indent=2) + '\n')
    return inventory


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embedded', type=Path, required=True)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--mpy-cross', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.embedded, args.sources, args.mpy_cross, args.output), indent=2))
