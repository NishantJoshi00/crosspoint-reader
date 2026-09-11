#!/usr/bin/env python3
"""Lower the pinned ARC Python sources to the embedded rule language.

Type annotations and typing-only declarations have no runtime effect. Match
statements are lowered to equivalent conditionals. Literal sprite matrices use
native byte buffers instead of thousands of temporary Python integer objects.
Game decisions, action handling and level definitions are preserved.
"""

import ast
import argparse
import hashlib
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
TYPES = {"Any", "Callable", "ClassVar", "Dict", "Final", "Iterator", "List", "Optional", "Set", "Tuple", "TypeAlias", "Union", "TypeVar", "Literal", "tuple", "dict", "list", "set"}


class Lower(ast.NodeTransformer):
    def visit_Import(self, node):
        for alias in node.names:
            if alias.name == "random":
                alias.name = "arc_random"
                alias.asname = alias.asname or "random"
        return node

    def visit_ImportFrom(self, node):
        if node.module in ("__future__", "typing", "typing_extensions", "enum"):
            return None
        if node.module == "collections" and any(alias.name == "deque" for alias in node.names):
            node.module = "arc_collections"
        return node

    def visit_Attribute(self, node):
        self.generic_visit(node)
        if node.attr == "__mro__":
            return ast.Call(func=ast.Attribute(value=ast.Name(id="_arc_compat", ctx=ast.Load()), attr="mro", ctx=ast.Load()), args=[node.value], keywords=[])
        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name) and node.value.func.id == "super":
            return ast.Call(func=ast.Attribute(value=ast.Name(id="_arc_native", ctx=ast.Load()), attr="super_value", ctx=ast.Load()), args=[node, ast.Name(id="self", ctx=ast.Load())], keywords=[])
        return node

    def visit_AnnAssign(self, node):
        if node.value is None:
            return None
        return self.visit(ast.Assign(targets=[node.target], value=node.value))

    def visit_Assign(self, node):
        if isinstance(node.value, ast.Subscript) and isinstance(node.value.value, ast.Name) and node.value.value.id in TYPES:
            return None
        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name) and node.value.func.id == "TypeVar":
            return None
        return self.generic_visit(node)

    def visit_FunctionDef(self, node):
        node.returns = None
        node.decorator_list = [d for d in node.decorator_list if not isinstance(d, ast.Name) or d.id not in ("final", "abstractmethod")]
        for arg in node.args.posonlyargs + node.args.args + node.args.kwonlyargs:
            arg.annotation = None
        if node.args.vararg:
            node.args.vararg.annotation = None
        if node.args.kwarg:
            node.args.kwarg.annotation = None
        self.generic_visit(node)
        if not node.body:
            node.body = [ast.Pass()]
        return node

    def visit_ClassDef(self, node):
        bases = [b.id for b in node.bases if isinstance(b, ast.Name)]
        if "TypedDict" in bases:
            return ast.Assign(targets=[ast.Name(id=node.name, ctx=ast.Store())], value=ast.Name(id="dict", ctx=ast.Load()))
        if "NamedTuple" in bases:
            fields = [n.target.id for n in node.body if isinstance(n, ast.AnnAssign)]
            return ast.parse(f"{node.name} = __import__('collections').namedtuple({node.name!r}, {fields!r})").body[0]
        if "Enum" in bases:
            # The two game-owned enums are str enums used as tag constants.
            if bases != ["str", "Enum"]:
                raise ValueError("Unsupported enum: " + node.name)
            node.bases = []
        self.generic_visit(node)
        if not node.body:
            node.body = [ast.Pass()]
        return node

    def visit_Expr(self, node):
        if isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
            return None
        return self.generic_visit(node)

    def visit_Call(self, node):
        self.generic_visit(node)
        if isinstance(node.func, ast.Name) and node.func.id == "cast":
            return node.args[1]
        if isinstance(node.func, ast.Name) and node.func.id == "Sprite":
            for kw in node.keywords:
                if kw.arg == "pixels" and isinstance(kw.value, ast.List):
                    try:
                        rows = ast.literal_eval(kw.value)
                    except (ValueError, TypeError):
                        continue
                    if rows and rows[0] and all(len(r) == len(rows[0]) for r in rows):
                        pixels = bytes(v & 255 for r in rows for v in r)
                        kw.value = ast.parse(f"_arc_native.matrix({pixels!r}, {len(rows)}, {len(rows[0])})", mode="eval").body
        if isinstance(node.func, ast.Attribute):
            if isinstance(node.func.value, ast.Name) and node.func.value.id == "math" and node.func.attr == "dist":
                node.func.value.id = "_arc_compat"
            elif node.func.attr == "sort":
                node.args.insert(0, node.func.value)
                node.func = ast.Attribute(value=ast.Name(id="_arc_compat", ctx=ast.Load()), attr="sort", ctx=ast.Load())
            elif isinstance(node.func.value, ast.Name) and node.func.value.id == "np" and node.func.attr in ("int8", "int16", "uint8", "uint16"):
                node.func.attr = "scalar_" + node.func.attr
            elif node.func.attr in ("astype", "ravel", "tolist", "fill"):
                name = node.func.attr
                node.args.insert(0, node.func.value)
                node.func = ast.Attribute(value=ast.Name(id="np", ctx=ast.Load()), attr=name, ctx=ast.Load())
        return node

    def visit_Match(self, node):
        subject = ast.Name(id="_arc_match_value", ctx=ast.Load())
        result = ast.Assign(targets=[ast.Name(id=subject.id, ctx=ast.Store())], value=self.visit(node.subject))
        chain = []
        for case in reversed(node.cases):
            body = [s for statement in case.body for s in self._statements(self.visit(statement))]
            if isinstance(case.pattern, ast.MatchAs) and case.pattern.name is None:
                chain = body
                continue
            patterns = case.pattern.patterns if isinstance(case.pattern, ast.MatchOr) else [case.pattern]
            if not all(isinstance(p, ast.MatchValue) for p in patterns):
                raise ValueError("Unsupported match pattern")
            tests = [ast.Compare(left=subject, ops=[ast.Eq()], comparators=[p.value]) for p in patterns]
            test = tests[0] if len(tests) == 1 else ast.BoolOp(op=ast.Or(), values=tests)
            if case.guard:
                test = ast.BoolOp(op=ast.And(), values=[test, self.visit(case.guard)])
            chain = [ast.If(test=test, body=body, orelse=chain)]
        return [result] + chain

    @staticmethod
    def _statements(node):
        return [] if node is None else node if isinstance(node, list) else [node]


HOT_METHODS = {
    ("sprites", "Sprite", "render"): "return _arc_native.sprite_render(self)",
    ("sprites", "Sprite", "collides_with"): "return _arc_native.sprite_collision(self, other, ignoreMode)",
    ("camera", "Camera", "_raw_render"): "return _arc_native.camera_raw(self, sorted(sprites, key=lambda s: s.layer))",
    ("camera", "Camera", "render"): "output = _arc_native.camera_scale(self, self._raw_render(sprites))\nfor interface in self._interfaces:\n    output = interface.render_interface(output)\nreturn output",
}


def compact_grid(node):
    if isinstance(node, ast.Dict):
        node.values = [compact_grid(value) for value in node.values]
        return node
    data = ast.literal_eval(node)
    if not isinstance(data, list) or not data or not data[0] or not all(isinstance(row, list) and len(row) == len(data[0]) for row in data):
        raise ValueError('Expected a rectangular constant grid')
    if not all(isinstance(value, int) and -128 <= value <= 127 for row in data for value in row):
        raise ValueError('Constant grid is outside int8 range')
    raw = bytes(value & 255 for row in data for value in row)
    return ast.parse(f'_arc_native.matrix({raw!r}, {len(data)}, {len(data[0])})', mode='eval').body


def lower(source, name, lazy=False, compact_grids=(), raster_tails=None, history=None):
    tree = ast.parse(source)
    # Only replace source-pinned vectorized raster tails. Bounds, pivot and
    # game-specific rotation fast paths remain in the original rule bytecode.
    for func in tree.body:
        if isinstance(func, ast.FunctionDef) and func.name in (raster_tails or {}):
            config = raster_tails[func.name]
            start = next(i for i, node in enumerate(func.body) if isinstance(node, ast.Assign)
                         and isinstance(node.targets[0], ast.Name) and node.targets[0].id == config['output'])
            func.body = func.body[:start] + ast.parse(config['output'] + ' = _arc_native.affine(' + ', '.join(config['args']) + ')').body + [func.body[-1]]
    for cls in tree.body:
        if isinstance(cls, ast.ClassDef):
            for func in cls.body:
                key = (name, cls.name, getattr(func, "name", ""))
                if key in HOT_METHODS:
                    func.body = ast.parse(HOT_METHODS[key]).body
                if key == ("sprites", "Sprite", "clone"):
                    # Preserve the constructor's int8 conversion without a nested-list temporary.
                    for call in ast.walk(func):
                        if isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and call.func.id == "Sprite":
                            for kw in call.keywords:
                                if kw.arg == "pixels":
                                    kw.value = ast.Name(id="pixels_copy", ctx=ast.Load())
                if key == ("base_game", "ARCBaseGame", "perform_action"):
                    # E-ink presents the final frame. Every intermediate render
                    # still executes because game interfaces can update rules.
                    for call in ast.walk(func):
                        if isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute) and isinstance(call.func.value, ast.Name) and call.func.value.id == "frame_list" and call.func.attr == "append":
                            call.func = ast.Attribute(value=ast.Name(id="_arc_compat", ctx=ast.Load()), attr="last_frame", ctx=ast.Load())
                            call.args.insert(0, ast.Name(id="frame_list", ctx=ast.Load()))
    tree = Lower().visit(tree)
    if history:
        for node in ast.walk(tree):
            if isinstance(node, ast.Assign) and isinstance(node.value, ast.List) and not node.value.elts:
                if any(isinstance(target, ast.Attribute) and target.attr == history[-1] for target in node.targets):
                    node.value = ast.parse("__import__('arc_history').History()", mode='eval').body
        tree.body.extend(ast.parse('def _arc_compact(game):\n    current = game\n    for name in ' + repr(history) + ':\n        current = getattr(current, name, None)\n        if current is None:\n            return\n    current.compact()').body)
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id in compact_grids:
            # Source-pinned, read-only lookup tables verified by the oracle.
            node.value = compact_grid(node.value)
    if lazy:
        for node in tree.body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                target = node.targets[0].id
                if target == "levels" and isinstance(node.value, ast.List):
                    factories = ', '.join('lambda: (' + ast.unparse(item) + ')' for item in node.value.elts)
                    node.value = ast.parse('_arc_compat.LevelFactories([' + factories + '])', mode='eval').body
                if target == "sprites" and isinstance(node.value, ast.Dict):
                    factories = ', '.join(ast.unparse(k) + ': lambda: (' + ast.unparse(v) + ')' for k, v in zip(node.value.keys, node.value.values))
                    node.value = ast.parse('_arc_compat.SpriteFactories({' + factories + '})', mode='eval').body
        if name == "base_game":
            for node in ast.walk(tree):
                if isinstance(node, ast.Assign) and isinstance(node.value, ast.ListComp):
                    target = node.targets[0]
                    if isinstance(target, ast.Attribute) and target.attr in ('_levels', '_clean_levels'):
                        source = ast.unparse(node.value.generators[0].iter)
                        node.value = ast.parse('_arc_compat.Levels(' + source + ')', mode='eval').body
    tree.body.insert(0, ast.Import(names=[ast.alias(name="_arc_native")]))
    tree.body.insert(0, ast.Import(names=[ast.alias(name="_arc_compat")]))
    tree.body.insert(2, ast.parse("sorted = _arc_compat.sorted").body[0])
    ast.fix_missing_locations(tree)
    return ast.unparse(tree) + "\n"


def compile_sources(source_dir, engine_dir, destination, lazy=False):
    destination.mkdir(parents=True, exist_ok=True)
    sdk = destination / "arcengine"
    sdk.mkdir(exist_ok=True)
    for path in engine_dir.glob("*.py"):
        if path.stem == "enums":
            continue
        (sdk / path.name).write_text(lower(path.read_text(), path.stem, lazy))
    for path in (HERE / "compat").glob("*.py"):
        shutil.copyfile(path, destination / path.name)
    shutil.copyfile(HERE / "compat/arcengine/enums.py", sdk / "enums.py")
    manifest = json.loads((HERE / "sources.json").read_text())
    for entry in manifest["games"]:
        data = (source_dir / (entry["id"] + ".py")).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError("Source digest mismatch: " + entry["id"])
        (destination / (entry["id"].split("-")[0] + ".py")).write_text(lower(data.decode(), entry["id"], lazy, entry.get('compact_grids', ()), entry.get('raster_tails'), entry.get('history')))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lazy", action="store_true")
    args = parser.parse_args()
    compile_sources(args.sources, args.engine, args.output, args.lazy)
