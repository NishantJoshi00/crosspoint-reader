"""Import marker for ARCEngine's optional host-only debug image exporter.

The player runs with debug=False. Filesystem access is provided by the C++ pack
loader; game bytecode does not receive access to the SD filesystem.
"""


def getenv(name, default=None):
    return default
