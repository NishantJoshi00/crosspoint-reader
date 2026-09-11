# ARC player dependencies

The ARC player uses the following MIT-licensed projects. It is an independent
player for this firmware fork, not an ARC Prize product.

| Component | Source | Revision | Changes |
| --- | --- | --- | --- |
| ARCEngine 0.9.3 | https://github.com/arcprize/ARCEngine | b495c6acaf253c9681cd7b75c4299d352e9ce6f8 | SDK lowering, native raster/collision/camera paths, lightweight data classes, lazy levels |
| Public ARC-AGI-3 games | URLs and SHA-256 in `tools/arc/sources.json` | 25 pinned versions | Portable bytecode; compact constant grids and undo storage; native affine raster tails in BP35/LF52 |
| MicroPython 1.27.0 | https://github.com/micropython/micropython | 78ff170de9e32c79db6e64d3e33d2bd60002bdcd | Embedded port, ordered dictionaries, bounded ROM reader; patches in `tools/arc/patches` |
| ulab | https://github.com/v923z/micropython-ulab | 01ad8a5fff89d7aeb68ef1827d1ba8f83f6462a7 | Boolean masks, integer indexing, membership and empty-slice fixes |

ARCEngine and every bundled game source carry Copyright (c) 2026 ARC Prize
Foundation. Full license texts are in `tools/arc/licenses/`. MicroPython and
ulab also retain their licenses beside the vendored source. Include these
notices and all three license files when distributing the firmware or packs.

`arc_random.py` implements MT19937 (Matsumoto and Nishimura) with the integer
seeding and sampling conventions used by CPython and NumPy. The Python SDK is
the reference for the automated action comparisons.
