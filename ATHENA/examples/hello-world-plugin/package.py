#!/usr/bin/env python3
"""Package this example with the unchanged, standalone Python AUDMAP SDK.

Copyright (C) 2026 Nuaptan Felix Evalisk.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Destination ZIP (must not exist)")
    args = parser.parse_args()
    example = Path(__file__).resolve().parent
    repository = example.parents[2]
    sdk = repository / "clients/python/src/athena_audmap"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(args.output, "x", compression=ZIP_DEFLATED) as archive:
        for name in ("manifest.json", "plugin_exec", "README.md"):
            archive.write(example / name, name)
        for source in sorted(sdk.glob("*.py")):
            archive.write(source, "sdk/athena_audmap/" + source.name)
        archive.write(repository / "LICENSE", "LICENSE")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
