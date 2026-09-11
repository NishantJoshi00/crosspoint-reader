def deepcopy(value, memo=None):
    if memo is None:
        memo = {}
    key = id(value)
    if key in memo:
        return memo[key]
    if value is None or isinstance(value, (int, float, str, bytes, bool)):
        return value
    if isinstance(value, dict):
        result = {}
        memo[key] = result
        for k, v in value.items():
            result[deepcopy(k, memo)] = deepcopy(v, memo)
        return result
    if isinstance(value, list):
        result = []
        memo[key] = result
        result.extend(deepcopy(v, memo) for v in value)
        return result
    if isinstance(value, tuple):
        result = tuple(deepcopy(v, memo) for v in value)
        if hasattr(value, '_fields'):
            result = type(value)(*result)
    elif isinstance(value, set):
        result = set(deepcopy(v, memo) for v in value)
    elif hasattr(value, "dtype"):
        result = value.copy()
    elif hasattr(value, "clone"):
        result = value.clone()
    else:
        result = __import__('_arc_native').new_instance(type(value))
        memo[key] = result
        for name in value.__dict__:
            setattr(result, name, deepcopy(getattr(value, name), memo))
    memo[key] = result
    return result


def copy(value):
    return value.copy() if hasattr(value, "copy") else deepcopy(value)
