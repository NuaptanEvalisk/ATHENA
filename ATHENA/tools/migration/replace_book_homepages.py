#!/usr/bin/env python3
"""
Replace Obsidian-generated book SSG pages with ATHENA book homepages.

Run this script at the vault root, whose layout is expected to contain:

  Notes Root/Sources/Reading/
  Notes Root/Support Files/Templates/Book Homepage.ath

By default this is a dry run. Pass --apply to overwrite the matching
"Book - *.ath" files.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


READING_REL = Path("Notes Root") / "Sources" / "Reading"
TEMPLATE_REL = Path("Notes Root") / "Support Files" / "Templates" / "Book Homepage.ath"


@dataclass(frozen=True)
class BookInfo:
    path: Path
    title: bytes
    author: bytes


def display_bytes(value: bytes) -> str:
    return value.decode("utf-8", errors="backslashreplace")


def replace_once(data: bytes, old: bytes, new: bytes, path: Path) -> bytes:
    count = data.count(old)
    if count != 1:
        old_text = display_bytes(old)
        raise ValueError(
            f"{path}: expected exactly one occurrence of {old_text!r}, found {count}"
        )
    return data.replace(old, new, 1)


def parse_book_folder_name(folder_name: str, path: Path) -> tuple[str, str]:
    if folder_name == "Internet":
        return "Internet", "Everyone"
    match = re.fullmatch(r"(.+?) \(([^()]+)\)", folder_name)
    if not match:
        raise ValueError(f"{path}: folder name is not of form 'Title (Author)'")
    title = match.group(1).strip()
    author = match.group(2).strip()
    if not title or not author:
        raise ValueError(f"{path}: empty title or author in folder name")
    return title, author


def book_homepage_file(book_dir: Path) -> Path | None:
    files = sorted(book_dir.glob("Book - *.ath"))
    if not files:
        return None
    return files[0]


def collect_books(reading_dir: Path) -> tuple[list[BookInfo], list[str]]:
    books: list[BookInfo] = []
    warnings: list[str] = []
    for book_dir in sorted(p for p in reading_dir.iterdir() if p.is_dir()):
        homepage = book_homepage_file(book_dir)
        if homepage is None:
            warnings.append(f"skip {book_dir}: no Book - *.ath file")
            continue
        try:
            title, author = parse_book_folder_name(book_dir.name, book_dir)
        except ValueError as e:
            warnings.append(str(e))
            continue
        books.append(
            BookInfo(
                path=homepage,
                title=title.encode("utf-8"),
                author=author.encode("utf-8"),
            )
        )
    return books, warnings


def render_homepage(template_data: bytes, info: BookInfo, template_path: Path) -> bytes:
    data = template_data
    data = replace_once(
        data,
        b"<doc-data|<doc-title|Name>>",
        b"<doc-data|<doc-title|" + info.title + b">>",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Book name. <with|font-series|bold|Name>",
        b"<item>Book name. <with|font-series|bold|" + info.title + b">",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Author. <with|font-series|bold|Who>",
        b"<item>Author. <with|font-series|bold|" + info.author + b">",
        template_path,
    )
    return data


def atomic_write(path: Path, data: bytes) -> None:
    fd, tmp_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(fd, "wb") as tmp:
            tmp.write(data)
            tmp.flush()
            os.fsync(tmp.fileno())
        os.replace(tmp_name, path)
    except Exception:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Replace Obsidian-generated 'Book - *.ath' files with ATHENA "
            "book homepage template files, deriving title/author from folder names."
        )
    )
    parser.add_argument(
        "--vault-root",
        type=Path,
        default=Path.cwd(),
        help="vault root; defaults to the current working directory",
    )
    parser.add_argument(
        "--template",
        type=Path,
        default=None,
        help="book homepage template; defaults to Notes Root/Support Files/Templates/Book Homepage.ath",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="actually overwrite files; without this flag the script only reports actions",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    vault_root = args.vault_root.resolve()
    reading_dir = vault_root / READING_REL
    template_path = (
        args.template.resolve()
        if args.template is not None
        else vault_root / TEMPLATE_REL
    )

    if not reading_dir.is_dir():
        print(f"error: reading directory not found: {reading_dir}", file=sys.stderr)
        return 2
    if not template_path.is_file():
        print(f"error: template file not found: {template_path}", file=sys.stderr)
        return 2

    template_data = template_path.read_bytes()
    books, warnings = collect_books(reading_dir)
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)

    if not books:
        print("No Book - *.ath files found.")
        return 1 if warnings else 0

    changed = 0
    failures = 0
    for info in books:
        try:
            rendered = render_homepage(template_data, info, template_path)
            changed += 1
            mode = "overwrite" if args.apply else "would overwrite"
            print(
                f"{mode}: {info.path}\n"
                f"  title:  {display_bytes(info.title)}\n"
                f"  author: {display_bytes(info.author)}"
            )
            if args.apply:
                atomic_write(info.path, rendered)
        except Exception as e:
            failures += 1
            print(f"error: {e}", file=sys.stderr)

    if not args.apply:
        print("\nDry run only. Re-run with --apply to overwrite the files.")
    print(f"Processed: {changed}; warnings: {len(warnings)}; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
