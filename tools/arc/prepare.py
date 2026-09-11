#!/usr/bin/env python3
"""Download the public ARC games, verifying the exact sources in sources.json.

The credentials used here are anonymous and are never saved in the repository.
Game code is only executed by the exporter/oracle after its digest is verified.
"""

import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parent
BASE = "https://three.arcprize.org"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fetch(path, key=None):
    headers = {"Accept": "application/json"}
    if key:
        headers["X-Api-Key"] = key
    with urllib.request.urlopen(urllib.request.Request(BASE + path, headers=headers), timeout=30) as response:
        return response.read()


def prepare(destination):
    manifest = json.loads((HERE / "sources.json").read_text())
    destination.mkdir(parents=True, exist_ok=True)
    key = None
    for game in manifest["games"]:
        target = destination / (game["id"] + ".py")
        if target.exists() and digest(target.read_bytes()) == game["sha256"]:
            continue
        if key is None:
            key = json.loads(fetch("/api/games/anonkey"))["api_key"]
        data = fetch("/api/games/" + game["id"] + "/source", key)
        if digest(data) != game["sha256"]:
            raise RuntimeError("Source digest mismatch: " + game["id"])
        temporary = target.with_suffix(".tmp")
        temporary.write_bytes(data)
        temporary.replace(target)
        print("Downloaded", game["id"])
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=HERE.parent.parent / ".cache/arc/sources")
    args = parser.parse_args()
    prepare(args.output)
