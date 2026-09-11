"""Local-game subset of ARCEngine enums, without API validation dependencies."""


class _Value:
    def __init__(self, value):
        self.value = value
        self._value_ = value

    def __eq__(self, other):
        return type(other) is type(self) and other.value == self.value

    def __hash__(self):
        return hash(self.value)


class BlockingMode(_Value):
    pass


BlockingMode.NOT_BLOCKED = BlockingMode(1)
BlockingMode.BOUNDING_BOX = BlockingMode(2)
BlockingMode.PIXEL_PERFECT = BlockingMode(3)


class InteractionMode(_Value):
    pass


InteractionMode.TANGIBLE = InteractionMode(1)
InteractionMode.INTANGIBLE = InteractionMode(2)
InteractionMode.INVISIBLE = InteractionMode(3)
InteractionMode.REMOVED = InteractionMode(4)


class GameAction(_Value):
    @staticmethod
    def from_id(value):
        return _actions[value]


_actions = [GameAction(i) for i in range(8)]
GameAction.RESET = _actions[0]
GameAction.ACTION1 = _actions[1]
GameAction.ACTION2 = _actions[2]
GameAction.ACTION3 = _actions[3]
GameAction.ACTION4 = _actions[4]
GameAction.ACTION5 = _actions[5]
GameAction.ACTION6 = _actions[6]
GameAction.ACTION7 = _actions[7]


class GameState(_Value):
    pass


GameState.NOT_PLAYED = GameState("NOT_PLAYED")
GameState.NOT_FINISHED = GameState("NOT_FINISHED")
GameState.WIN = GameState("WIN")
GameState.GAME_OVER = GameState("GAME_OVER")


class ActionInput:
    def __init__(self, id=GameAction.RESET, data=None, **kwargs):
        self.id = GameAction.from_id(id) if isinstance(id, int) else id
        self.data = {} if data is None else data


class FrameData:
    def __init__(self, **kwargs):
        for name, value in kwargs.items():
            setattr(self, name, value)


FrameDataRaw = FrameData
SimpleAction = FrameData
ComplexAction = FrameData
PlaceableArea = FrameData
MAX_REASONING_BYTES = 16384
