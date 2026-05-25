#!/usr/bin/env python3
"""
Replace Obsidian-generated course SSG pages with ATHENA course homepages.

Run this script at the vault root, whose layout is expected to contain:

  Notes Root/Sources/Lecture Notes/
  Notes Root/Support Files/Templates/Course Homepage.ath

By default this is a dry run. Pass --apply to overwrite the matching
"Course - *.ath" files.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


LECTURE_NOTES_REL = Path("Notes Root") / "Sources" / "Lecture Notes"
TEMPLATE_REL = Path("Notes Root") / "Support Files" / "Templates" / "Course Homepage.ath"


@dataclass(frozen=True)
class CourseInfo:
    title: bytes
    name: bytes
    location: bytes
    duration: bytes
    lecturer: bytes
    number: bytes


def normalize_field(value: bytes) -> bytes:
    value = re.sub(rb"\s+", b" ", value)
    return value.strip()


def scan_macro_argument(data: bytes, start: int, path: Path, what: str) -> bytes:
    depth = 0
    i = start
    while i < len(data):
        c = data[i]
        if c == ord("<"):
            depth += 1
        elif c == ord(">"):
            if depth == 0:
                return data[start:i]
            depth -= 1
        i += 1
    raise ValueError(f"{path}: unterminated {what}")


def find_prefixed_argument(data: bytes, prefix: bytes, path: Path, what: str) -> bytes:
    start = data.find(prefix)
    if start < 0:
        raise ValueError(f"{path}: missing {what}")
    return scan_macro_argument(data, start + len(prefix), path, what)


def extract_doc_title(data: bytes, path: Path) -> bytes:
    title = find_prefixed_argument(
        data, b"<doc-data|<doc-title|", path, "<doc-data|<doc-title|...>>"
    )
    return normalize_field(title)


def extract_strong_item(data: bytes, label: bytes, path: Path) -> bytes:
    pattern = (
        rb"<item>"
        + re.escape(label)
        + rb"\.\s*<strong\|"
    )
    match = re.search(pattern, data, re.S)
    if not match:
        label_text = display_bytes(label)
        raise ValueError(f"{path}: missing item '{label_text}. <strong|...>'")
    return normalize_field(
        scan_macro_argument(data, match.end(), path, f"{display_bytes(label)} field")
    )


def extract_course_info(path: Path) -> CourseInfo:
    data = path.read_bytes()
    title = extract_doc_title(data, path)
    if title == b"Course - Name":
        title = path.stem.encode("utf-8")
    return CourseInfo(
        title=title,
        name=extract_strong_item(data, b"Course name", path),
        location=extract_strong_item(data, b"Course location", path),
        duration=extract_strong_item(data, b"Course duration", path),
        lecturer=extract_strong_item(data, b"Lecturer", path),
        number=extract_strong_item(data, b"Course number", path),
    )


def replace_once(data: bytes, old: bytes, new: bytes, path: Path) -> bytes:
    count = data.count(old)
    if count != 1:
        old_text = display_bytes(old)
        raise ValueError(
            f"{path}: expected exactly one occurrence of {old_text!r}, found {count}"
        )
    return data.replace(old, new, 1)


def render_homepage(template_data: bytes, info: CourseInfo, template_path: Path) -> bytes:
    data = template_data
    data = replace_once(
        data,
        b"<doc-data|<doc-title|Course - Name>>",
        b"<doc-data|<doc-title|" + info.title + b">>",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Course name. <with|font-series|bold|Name>",
        b"<item>Course name. <with|font-series|bold|" + info.name + b">",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Course location. <with|font-series|bold|Location>",
        b"<item>Course location. <with|font-series|bold|" + info.location + b">",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Course duration. <with|font-series|bold|Duration>",
        b"<item>Course duration. <with|font-series|bold|" + info.duration + b">",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Lecturer. <with|font-series|bold|Who>",
        b"<item>Lecturer. <with|font-series|bold|" + info.lecturer + b">",
        template_path,
    )
    data = replace_once(
        data,
        b"<item>Course number. <with|font-series|bold|Which>",
        b"<item>Course number. <with|font-series|bold|" + info.number + b">",
        template_path,
    )
    return data


def display_bytes(value: bytes) -> str:
    return value.decode("utf-8", errors="backslashreplace")


def course_files(lecture_notes: Path) -> list[Path]:
    return sorted(lecture_notes.glob("*/Course -*.ath"))


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
            "Replace Obsidian-generated 'Course - *.ath' files with ATHENA "
            "course homepage template files, preserving course metadata."
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
        help="course homepage template; defaults to Notes Root/Support Files/Templates/Course Homepage.ath",
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
    lecture_notes = vault_root / LECTURE_NOTES_REL
    template_path = (
        args.template.resolve()
        if args.template is not None
        else vault_root / TEMPLATE_REL
    )

    if not lecture_notes.is_dir():
        print(f"error: lecture-notes directory not found: {lecture_notes}", file=sys.stderr)
        return 2
    if not template_path.is_file():
        print(f"error: template file not found: {template_path}", file=sys.stderr)
        return 2

    template_data = template_path.read_bytes()
    files = course_files(lecture_notes)
    if not files:
        print(f"No Course -*.ath files found under {lecture_notes}")
        return 0

    failures = 0
    changed = 0
    for path in files:
        try:
            info = extract_course_info(path)
            rendered = render_homepage(template_data, info, template_path)
            changed += 1
            mode = "overwrite" if args.apply else "would overwrite"
            print(
                f"{mode}: {path}\n"
                f"  title:    {display_bytes(info.title)}\n"
                f"  name:     {display_bytes(info.name)}\n"
                f"  location: {display_bytes(info.location)}\n"
                f"  duration: {display_bytes(info.duration)}\n"
                f"  lecturer: {display_bytes(info.lecturer)}\n"
                f"  number:   {display_bytes(info.number)}"
            )
            if args.apply:
                atomic_write(path, rendered)
        except Exception as e:
            failures += 1
            print(f"error: {e}", file=sys.stderr)

    if not args.apply:
        print("\nDry run only. Re-run with --apply to overwrite the files.")
    print(f"Processed: {changed}; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
