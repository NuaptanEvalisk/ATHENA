#!/usr/bin/env python3
"""
Create course-level concrete namespaces for an ATHENA vault.

Run this script at the vault root.  By default this is a dry run; pass --apply
to write to ns.sqlite.
"""

from __future__ import annotations

import argparse
import re
import sqlite3
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


LECTURE_NOTES_REL = Path("Notes Root") / "Sources" / "Lecture Notes"
DEFAULT_DB_NAME = "ns.sqlite"
DEFAULT_PARENT = "Courses"
DEFAULT_SORTER_REL = Path("dependencies") / "roman-sorter.c"
DEFAULT_STYLE_REL = Path("dependencies") / "unnumbered-sections-generic.ts"


@dataclass(frozen=True)
class CourseNamespace:
    directory: Path
    abbreviation: str
    name: str
    template: str
    sorter_path: Path
    homepage_path: Path | None
    style_path: Path


def ensure_schema(db: sqlite3.Connection) -> None:
    db.executescript(
        """
        CREATE TABLE IF NOT EXISTS meta (
          key TEXT PRIMARY KEY,
          value TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS namespaces (
          name TEXT PRIMARY KEY,
          kind TEXT NOT NULL CHECK(kind IN
            ('abstract','semi-concrete','concrete')),
          template TEXT NOT NULL DEFAULT '',
          sorter_trivial INTEGER NOT NULL DEFAULT 0,
          sorter_path TEXT NOT NULL DEFAULT '',
          style_path TEXT NOT NULL DEFAULT '',
          initial_content_path TEXT NOT NULL DEFAULT '',
          homepage_path TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS namespace_parents (
          child TEXT NOT NULL,
          parent TEXT NOT NULL,
          source TEXT NOT NULL CHECK(source IN ('declared','derived')),
          ord INTEGER NOT NULL DEFAULT 0,
          PRIMARY KEY(child, parent, source)
        );
        CREATE INDEX IF NOT EXISTS namespace_parents_child_idx
          ON namespace_parents(child, source, ord);
        CREATE INDEX IF NOT EXISTS namespace_parents_parent_idx
          ON namespace_parents(parent);
        CREATE TABLE IF NOT EXISTS relation_decisions (
          parent TEXT NOT NULL,
          child TEXT NOT NULL,
          decision TEXT NOT NULL CHECK(decision IN ('allow','deny')),
          source TEXT NOT NULL DEFAULT 'user',
          PRIMARY KEY(parent, child)
        );
        INSERT INTO meta(key, value) VALUES('schema-version', '1')
          ON CONFLICT(key) DO NOTHING;
        """
    )
    for column, definition in (
        ("sorter_trivial", "INTEGER NOT NULL DEFAULT 0"),
        ("initial_content_path", "TEXT NOT NULL DEFAULT ''"),
        ("homepage_path", "TEXT NOT NULL DEFAULT ''"),
    ):
        ensure_column(db, "namespaces", column, definition)


def ensure_column(
    db: sqlite3.Connection, table: str, column: str, definition: str
) -> None:
    existing = {row[1] for row in db.execute(f"PRAGMA table_info({table})")}
    if column not in existing:
        db.execute(f"ALTER TABLE {table} ADD COLUMN {column} {definition}")


def namespace_exists(db: sqlite3.Connection, name: str) -> bool:
    row = db.execute("SELECT 1 FROM namespaces WHERE name=?;", (name,)).fetchone()
    return row is not None


def infer_abbreviation(course_dir: Path) -> str | None:
    counts: Counter[str] = Counter()
    for path in course_dir.glob("*.ath"):
        if path.name.startswith("Course -"):
            continue
        match = re.match(r"^([A-Z][A-Z0-9]{1,12})\b", path.stem)
        if match:
            counts[match.group(1)] += 1
    if not counts:
        return None
    best_count = max(counts.values())
    candidates = sorted(k for k, v in counts.items() if v == best_count)
    return candidates[0]


def choose_homepage(course_dir: Path) -> Path | None:
    files = sorted(course_dir.glob("Course - *.ath"))
    if not files:
        return None
    return files[0]


def course_namespaces(
    vault_root: Path, sorter_path: Path, style_path: Path
) -> tuple[list[CourseNamespace], list[str]]:
    lecture_notes = vault_root / LECTURE_NOTES_REL
    out: list[CourseNamespace] = []
    warnings: list[str] = []
    for course_dir in sorted(p for p in lecture_notes.iterdir() if p.is_dir()):
        if not any(course_dir.glob("*.ath")):
            continue
        abbreviation = infer_abbreviation(course_dir)
        if abbreviation is None:
            warnings.append(f"skip {course_dir}: could not infer abbreviation")
            continue
        out.append(
            CourseNamespace(
                directory=course_dir,
                abbreviation=abbreviation,
                name=f"Course - {course_dir.name}",
                template=f"{abbreviation} %s %R",
                sorter_path=sorter_path,
                homepage_path=choose_homepage(course_dir),
                style_path=style_path,
            )
        )
    return out, warnings


def upsert_course_namespace(
    db: sqlite3.Connection, ns: CourseNamespace, parent: str
) -> None:
    db.execute(
        """
        INSERT INTO namespaces
          (name, kind, template, sorter_trivial, sorter_path, style_path,
           initial_content_path, homepage_path)
        VALUES (?, 'concrete', ?, 0, ?, ?, '', ?)
        ON CONFLICT(name) DO UPDATE SET
          kind=excluded.kind,
          template=excluded.template,
          sorter_trivial=excluded.sorter_trivial,
          sorter_path=excluded.sorter_path,
          style_path=excluded.style_path,
          initial_content_path=excluded.initial_content_path,
          homepage_path=excluded.homepage_path;
        """,
        (
            ns.name,
            ns.template,
            str(ns.sorter_path),
            str(ns.style_path),
            "" if ns.homepage_path is None else str(ns.homepage_path),
        ),
    )
    db.execute(
        "DELETE FROM namespace_parents WHERE child=? AND source='declared';",
        (ns.name,),
    )
    db.execute(
        """
        INSERT OR REPLACE INTO namespace_parents(child, parent, source, ord)
        VALUES (?, ?, 'declared', 0);
        """,
        (ns.name, parent),
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create one concrete namespace for each course directory."
    )
    parser.add_argument(
        "--vault-root",
        type=Path,
        default=Path.cwd(),
        help="vault root; defaults to the current working directory",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=None,
        help="namespace SQLite database; defaults to <vault-root>/ns.sqlite",
    )
    parser.add_argument(
        "--parent",
        default=DEFAULT_PARENT,
        help="explicit parent namespace to assign; defaults to Courses",
    )
    parser.add_argument(
        "--sorter",
        type=Path,
        default=None,
        help="sorter .c path; defaults to <vault-root>/dependencies/roman-sorter.c",
    )
    parser.add_argument(
        "--style",
        type=Path,
        default=None,
        help="style path; defaults to <vault-root>/dependencies/unnumbered-sections-generic.ts",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="actually write ns.sqlite; without this flag the script only reports actions",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    vault_root = args.vault_root.resolve()
    lecture_notes = vault_root / LECTURE_NOTES_REL
    db_path = args.db.resolve() if args.db is not None else vault_root / DEFAULT_DB_NAME
    sorter_path = (
        args.sorter.resolve()
        if args.sorter is not None
        else (vault_root / DEFAULT_SORTER_REL).resolve()
    )
    style_path = (
        args.style.resolve()
        if args.style is not None
        else (vault_root / DEFAULT_STYLE_REL).resolve()
    )

    if not lecture_notes.is_dir():
        print(f"error: lecture-notes directory not found: {lecture_notes}", file=sys.stderr)
        return 2
    if not sorter_path.is_file():
        print(f"error: sorter not found: {sorter_path}", file=sys.stderr)
        return 2
    if not style_path.is_file():
        print(f"error: style not found: {style_path}", file=sys.stderr)
        return 2

    namespaces, warnings = course_namespaces(vault_root, sorter_path, style_path)
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)

    if not namespaces:
        print("No course namespaces found.")
        return 1 if warnings else 0

    if args.apply:
        db = sqlite3.connect(db_path)
    else:
        if not db_path.is_file():
            print(f"error: namespace database not found: {db_path}", file=sys.stderr)
            return 2
        db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)

    with db:
        db.execute("PRAGMA foreign_keys=ON;")
        if args.apply:
            ensure_schema(db)
        if not namespace_exists(db, args.parent):
            print(
                f"error: parent namespace does not exist: {args.parent}",
                file=sys.stderr,
            )
            return 2

        action = "upsert" if args.apply else "would upsert"
        for ns in namespaces:
            existing = namespace_exists(db, ns.name)
            state = "update" if existing else "create"
            print(
                f"{action} ({state}): {ns.name}\n"
                f"  directory: {ns.directory}\n"
                f"  template:  {ns.template}\n"
                f"  sorter:    {ns.sorter_path}\n"
                f"  homepage:  {ns.homepage_path if ns.homepage_path is not None else '<none>'}\n"
                f"  style:     {ns.style_path}\n"
                f"  parent:    {args.parent}"
            )
            if args.apply:
                upsert_course_namespace(db, ns, args.parent)

        if args.apply:
            db.commit()
        else:
            db.rollback()
            print("\nDry run only. Re-run with --apply to write ns.sqlite.")

    print(f"Prepared: {len(namespaces)}; warnings: {len(warnings)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
