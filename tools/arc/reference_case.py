"""One deterministic SDK trace; invoked in a fresh CPython process by verify.py."""
import importlib.util
import json
import sys
import zlib

import numpy as np
from arcengine import ActionInput, GameAction

source, level, steps = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
name = source.rsplit('/', 1)[-1].split('-')[0]
spec = importlib.util.spec_from_file_location(name, source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def open_game():
    np.random.seed(0)
    game = getattr(module, name[0].upper() + name[1:])()
    if level:
        game.set_level(level)
        game._score = level
    return game


def emit(game, frame):
    pixels = bytes(int(value) & 255 for row in frame for value in row)
    state = {'NOT_FINISHED': 0, 'NOT_PLAYED': 0, 'WIN': 1, 'GAME_OVER': 2}[game._state.value]
    print(json.dumps(dict(level=game.level_index, state=state, moves=game._action_count, crc=zlib.crc32(pixels))))


game = open_game()
frame = game.camera.render(game.current_level.get_sprites())
emit(game, frame)
for i in range(steps):
    actions = sorted(game._available_actions)
    action = 0 if i == 12 else actions[i % len(actions)]
    result = game.perform_action(ActionInput(id=GameAction.from_id(action),
                                data={'x': (i * 17 + 10) % 64, 'y': (i * 23 + 10) % 64}), raw=True)
    if result.frame:
        frame = result.frame[-1]
    emit(game, frame)
game = open_game()
emit(game, game.camera.render(game.current_level.get_sprites()))
