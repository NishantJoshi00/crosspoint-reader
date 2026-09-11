"""Device entry points. Rule steps and every intermediate render still execute."""
from arcengine import ActionInput, GameAction, GameState
import gc

_game = None
_frame = None
_compact = None


def open_game(name, level=0):
    global _game, _frame, _compact
    # Collect before short-lived render allocations fragment the small device
    # heap. Waiting for allocation failure leaves no contiguous frame buffer.
    gc.threshold(4096)
    module = __import__(name)
    _compact = getattr(module, '_arc_compact', None)
    _game = getattr(module, name[0].upper() + name[1:])()
    if level:
        _game.set_level(level)
        _game._score = level
    _frame = _game.camera.render(_game.current_level.get_sprites())
    gc.collect()
    return _frame


def act(action, x=0, y=0):
    global _frame
    result = _game.perform_action(ActionInput(id=GameAction.from_id(action), data={'x': x, 'y': y}), raw=True)
    if result.frame:
        _frame = result.frame[-1]
    if _compact:
        _compact(_game)
    gc.collect()
    return _frame


def info():
    state = 1 if _game._state == GameState.WIN else 2 if _game._state == GameState.GAME_OVER else 0
    actions = 0
    for action in _game._available_actions:
        actions |= 1 << action
    return state, _game.level_index, _game.win_score, actions, _game._action_count
