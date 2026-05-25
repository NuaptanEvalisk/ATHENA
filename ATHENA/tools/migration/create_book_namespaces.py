#!/usr/bin/env python3
"""
Create book-level semi-concrete namespaces for an ATHENA vault.

Run this script at the vault root.  By default this is a dry run; pass --apply
to write to ns.sqlite.
"""

from __future__ import annotations

import argparse
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path


READING_REL = Path("Notes Root") / "Sources" / "Reading"
DEFAULT_DB_NAME = "ns.sqlite"
DEFAULT_SORTER_REL = Path("dependencies") / "reading-sorter.c"


@dataclass(frozen=True)
class BookNamespace:
    directory: Path
    abbreviation: str
    name: str
    template: str
    sorter_path: Path
    homepage_path: Path


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


def parse_book_folder_name(folder_name: str, path: Path) -> tuple[str, str]:
    if folder_name == "Internet":
        return "Internet", "Everyone"
    match = re.fullmatch(r"(.+?) \(([^()]+)\)", folder_name)
    if not match:
        raise ValueError(f"{path}: folder name is not of form 'Title (Author)'")
    return match.group(1).strip(), match.group(2).strip()


def book_homepage_file(book_dir: Path) -> Path | None:
    files = sorted(book_dir.glob("Book - *.ath"))
    if not files:
        return None
    return files[0]


def infer_abbreviation(book_dir: Path) -> str | None:
    counts: dict[str, int] = {}
    for path in book_dir.glob("*.ath"):
        if path.name.startswith("Book -"):
            continue
        match = re.match(r"^([A-Z][A-Z0-9]{1,12})\s+\d+\b", path.stem)
        if match:
            abbr = match.group(1)
            counts[abbr] = counts.get(abbr, 0) + 1
    if not counts:
        return None
    best = max(counts.values())
    return sorted(k for k, v in counts.items() if v == best)[0]


def book_namespaces(
    vault_root: Path, sorter_path: Path
) -> tuple[list[BookNamespace], list[str]]:
    reading_dir = vault_root / READING_REL
    out: list[BookNamespace] = []
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
        abbreviation = infer_abbreviation(book_dir)
        if abbreviation is None:
            warnings.append(f"skip {book_dir}: could not infer reading abbreviation")
            continue
        out.append(
            BookNamespace(
                directory=book_dir,
                abbreviation=abbreviation,
                name=f"Book - {book_dir.name}",
                template=f"{abbreviation} %N %s",
                sorter_path=sorter_path,
                homepage_path=homepage.resolve(),
            )
        )
    return out, warnings


def upsert_book_namespace(db: sqlite3.Connection, ns: BookNamespace) -> None:
    db.execute(
        """
        INSERT INTO namespaces
          (name, kind, template, sorter_trivial, sorter_path, style_path,
           initial_content_path, homepage_path)
        VALUES (?, 'semi-concrete', ?, 0, ?, '', '', ?)
        ON CONFLICT(name) DO UPDATE SET
          kind=excluded.kind,
          template=excluded.template,
          sorter_trivial=excluded.sorter_trivial,
          sorter_path=excluded.sorter_path,
          style_path=excluded.style_path,
          initial_content_path=excluded.initial_content_path,
          homepage_path=excluded.homepage_path;
        """,
        (ns.name, ns.template, str(ns.sorter_path), str(ns.homepage_path)),
    )
    db.execute(
        "DELETE FROM namespace_parents WHERE child=? AND source='declared';",
        (ns.name,),
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create one semi-concrete namespace for each book directory."
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
        "--sorter",
        type=Path,
        default=None,
        help="sorter .c path; defaults to <vault-root>/dependencies/reading-sorter.c",
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
    reading_dir = vault_root / READING_REL
    db_path = args.db.resolve() if args.db is not None else vault_root / DEFAULT_DB_NAME
    sorter_path = (
        args.sorter.resolve()
        if args.sorter is not None
        else (vault_root / DEFAULT_SORTER_REL).resolve()
    )

    if not reading_dir.is_dir():
        print(f"error: reading directory not found: {reading_dir}", file=sys.stderr)
        return 2
    if not sorter_path.is_file():
        print(f"error: sorter not found: {sorter_path}", file=sys.stderr)
        return 2

    namespaces, warnings = book_namespaces(vault_root, sorter_path)
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)

    if not namespaces:
        print("No book namespaces found.")
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
        action = "upsert" if args.apply else "would upsert"
        for ns in namespaces:
            existing = namespace_exists(db, ns.name)
            print(
                f"{action} ({'update' if existing else 'create'}): {ns.name}\n"
                f"  directory: {ns.directory}\n"
                f"  abbreviation: {ns.abbreviation}\n"
                f"  template:  {ns.template}\n"
                f"  sorter:    {ns.sorter_path}\n"
                f"  homepage:  {ns.homepage_path}\n"
                f"  parents:   <none>"
            )
            if args.apply:
                upsert_book_namespace(db, ns)

        if args.apply:
            db.commit()
        else:
            db.rollback()
            print("\nDry run only. Re-run with --apply to write ns.sqlite.")

    print(f"Prepared: {len(namespaces)}; warnings: {len(warnings)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
