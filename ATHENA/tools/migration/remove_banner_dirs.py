#!/usr/bin/env python3
"""
Remove Obsidian SSG banner cache directories from an ATHENA vault.

Run this script at the vault root.  By default this is a dry run.  Pass
--apply to remove directories named exactly "banner"; even with --apply the
script asks for confirmation before deleting anything.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


def banner_dirs(vault_root: Path) -> list[Path]:
    out: list[Path] = []
    for path in vault_root.rglob("banner"):
        if path.is_dir():
            out.append(path)
    return sorted(out)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Recursively remove directories named exactly 'banner'."
    )
    parser.add_argument(
        "--vault-root",
        type=Path,
        default=Path.cwd(),
        help="vault root; defaults to the current working directory",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="actually delete banner directories; still asks for Y/N confirmation",
    )
    return parser.parse_args(argv)


def confirm(paths: list[Path]) -> bool:
    print()
    print(f"About to delete {len(paths)} banner director{'y' if len(paths) == 1 else 'ies'}.")
    answer = input("Proceed? Type Y to delete, anything else to cancel: ")
    return answer == "Y"


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    vault_root = args.vault_root.resolve()
    if not vault_root.is_dir():
        print(f"error: vault root is not a directory: {vault_root}", file=sys.stderr)
        return 2

    paths = banner_dirs(vault_root)
    if not paths:
        print(f"No banner directories found under {vault_root}")
        return 0

    mode = "delete" if args.apply else "would delete"
    for path in paths:
        print(f"{mode}: {path}")

    if not args.apply:
        print("\nDry run only. Re-run with --apply to delete these directories.")
        return 0

    if not confirm(paths):
        print("Cancelled. Nothing deleted.")
        return 1

    failures = 0
    for path in paths:
        try:
            shutil.rmtree(path)
            print(f"deleted: {path}")
        except Exception as e:
            failures += 1
            print(f"error: failed to delete {path}: {e}", file=sys.stderr)

    print(f"Deleted: {len(paths) - failures}; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
