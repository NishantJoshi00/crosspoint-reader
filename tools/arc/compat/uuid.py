_counter = 0


def uuid4():
    global _counter
    _counter += 1
    return "arc-sprite-%d" % _counter
