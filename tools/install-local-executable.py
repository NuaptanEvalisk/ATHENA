#!/usr/bin/env python3
"""Atomically deploy a Linux executable without archiving previous builds."""

import argparse
import filecmp
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def build_id(path):
    notes = subprocess.check_output(["readelf", "-n", str(path)], text=True)
    match = re.search(r"Build ID: ([0-9a-f]+)", notes)
    if not match:
        raise RuntimeError(f"No ELF build ID: {path}")
    return match.group(1)


def install(source, destination):
    source = source.resolve()
    destination = destination.absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    new_id = build_id(source)
    if destination.exists() and filecmp.cmp(source, destination, shallow=False):
        print(f"unchanged {destination} (build ID {new_id})")
        return

    # The rename replaces the directory entry, not an executable's existing
    # mapping. Running processes keep their old inode without any archive.
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=destination.parent)
    os.close(descriptor)
    try:
        shutil.copyfile(source, temporary)
        os.chmod(temporary, 0o755)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f"deployed  {destination} (build ID {new_id})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    install(arguments.source, arguments.destination)
