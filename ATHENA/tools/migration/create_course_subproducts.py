#!/usr/bin/env python3
"""
Create course/type namespace sub-products for an ATHENA vault.

This mirrors the Namespace Manager's "Generate sub-products" path for the
regular course migration case:

  Course - X  x  Lecture Notes
  Course - X  x  Assignments
  Course - X  x  Course Extras

By default this is a dry run.  Pass --apply to write generated sorter .c files
under .athena/ns-sorters/ and upsert rows in ns.sqlite.
"""

from __future__ import annotations

import argparse
import os
import re
import sqlite3
import sys
import time
from dataclasses import dataclass
from pathlib import Path


LECTURE_NOTES_REL = Path("Notes Root") / "Sources" / "Lecture Notes"
DEFAULT_DB_NAME = "ns.sqlite"

PRODUCT_TYPES = (
    ("Lecture Notes", "Lecture Notes", "%R", "Lecture Notes ", ("roman",)),
    ("Assignments", "Assignments", "%s %R", "Assignments ", ("string", "roman")),
    ("Course Extras", "Extras", "%s %R", "Extras ", ("string", "roman")),
)

FIELD_TYPE_TO_C = {
    "string": "ATHENA_NS_STRING",
    "word": "ATHENA_NS_WORD",
    "char": "ATHENA_NS_CHAR",
    "int": "ATHENA_NS_INT",
    "positive-int": "ATHENA_NS_POS_INT",
    "roman": "ATHENA_NS_ROMAN",
}


@dataclass(frozen=True)
class NamespaceRow:
    name: str
    kind: str
    template: str
    sorter_trivial: bool
    sorter_path: str


@dataclass(frozen=True)
class ProductPlan:
    course: NamespaceRow
    type_ns: NamespaceRow
    course_dir: Path
    abbreviation: str
    type_label: str
    product_name: str
    product_template: str
    product_field_types: tuple[str, ...]
    matching_files: tuple[Path, ...]


@dataclass(frozen=True)
class Part:
    child_index: int | None = None
    literal: str = ""


@dataclass(frozen=True)
class FieldExpr:
    field_type: str
    parts: tuple[Part, ...]


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


def namespace_by_name(db: sqlite3.Connection, name: str) -> NamespaceRow | None:
    row = db.execute(
        """
        SELECT name, kind, template, sorter_trivial, sorter_path
        FROM namespaces WHERE name=?;
        """,
        (name,),
    ).fetchone()
    if row is None:
        return None
    return NamespaceRow(
        name=row[0],
        kind=row[1],
        template=row[2],
        sorter_trivial=bool(row[3]),
        sorter_path=row[4],
    )


def course_abbreviation(course: NamespaceRow) -> str | None:
    match = re.match(r"^([A-Z][A-Z0-9]*)\s+%s\s+%R$", course.template)
    if match:
        return match.group(1)
    return None


def files_for_product(course_dir: Path, abbreviation: str, type_label: str) -> tuple[Path, ...]:
    prefix = f"{abbreviation} {type_label} "
    return tuple(
        sorted(
            p
            for p in course_dir.glob("*.ath")
            if p.stem.startswith(prefix)
        )
    )


def product_plans(vault_root: Path, db: sqlite3.Connection) -> list[ProductPlan]:
    lecture_notes = vault_root / LECTURE_NOTES_REL
    plans: list[ProductPlan] = []
    type_rows: dict[str, NamespaceRow] = {}
    for type_name, *_ in PRODUCT_TYPES:
        ns = namespace_by_name(db, type_name)
        if ns is None:
            raise ValueError(f"missing namespace: {type_name}")
        if ns.kind not in ("concrete", "semi-concrete"):
            raise ValueError(f"namespace is not concrete/semi-concrete: {type_name}")
        if not ns.sorter_trivial and ns.sorter_path == "":
            raise ValueError(f"namespace has no sorter: {type_name}")
        type_rows[type_name] = ns

    for course_dir in sorted(p for p in lecture_notes.iterdir() if p.is_dir()):
        course_name = f"Course - {course_dir.name}"
        course = namespace_by_name(db, course_name)
        if course is None:
            continue
        if course.kind not in ("concrete", "semi-concrete"):
            continue
        abbr = course_abbreviation(course)
        if abbr is None:
            raise ValueError(
                f"course namespace has unexpected template: {course.name}: {course.template}"
            )
        if not course.sorter_trivial and course.sorter_path == "":
            raise ValueError(f"course namespace has no sorter: {course.name}")

        for type_name, type_label, suffix_template, literal_prefix, field_types in PRODUCT_TYPES:
            matches = files_for_product(course_dir, abbr, type_label)
            if not matches:
                continue
            product_template = f"{abbr} {type_label} {suffix_template}"
            plans.append(
                ProductPlan(
                    course=course,
                    type_ns=type_rows[type_name],
                    course_dir=course_dir,
                    abbreviation=abbr,
                    type_label=type_label,
                    product_name=f"{course.name} - {type_name}",
                    product_template=product_template,
                    product_field_types=field_types,
                    matching_files=matches,
                )
            )
    return plans


def c_string_escape(value: str) -> str:
    out = []
    for ch in value.encode("utf-8"):
        if ch == ord("\\"):
            out.append("\\\\")
        elif ch == ord('"'):
            out.append('\\"')
        elif ch == ord("\n"):
            out.append("\\n")
        elif ch == ord("\r"):
            out.append("\\r")
        elif ch == ord("\t"):
            out.append("\\t")
        elif ch < 32 or ch >= 127:
            out.append(f"\\x{ch:02x}")
        else:
            out.append(chr(ch))
    return "".join(out)


def safe_file_component(value: str) -> str:
    out = []
    for ch in value:
        if ch.isalnum() or ch in "-_":
            out.append(ch.lower())
        elif ch == " ":
            out.append("-")
    return "".join(out) or "namespace"


def resolve_sorter_path(vault_root: Path, sorter_path: str) -> Path:
    path = Path(sorter_path)
    if path.is_absolute():
        return path
    return vault_root / path


def replace_identifier(source: str, old: str, new: str) -> str:
    return re.sub(
        rf"(?<![A-Za-z0-9_]){re.escape(old)}(?![A-Za-z0-9_])",
        new,
        source,
    )


def sorter_source_for_function(vault_root: Path, ns: NamespaceRow, function_name: str) -> str:
    if ns.sorter_trivial or ns.sorter_path == "":
        return (
            f"static int\n{function_name} "
            "(int n, const AthenaNsField* a, const AthenaNsField* b) {\n"
            "  (void) n; (void) a; (void) b;\n"
            "  return 0;\n"
            "}\n"
        )
    path = resolve_sorter_path(vault_root, ns.sorter_path)
    source = path.read_text(encoding="utf-8")
    return replace_identifier(source, "athena_ns_compare", function_name)


def append_field_build_code(lines: list[str], prefix: str, source_name: str, fields: tuple[FieldExpr, ...]) -> None:
    count = len(fields)
    lines.append(f"  AthenaNsField {prefix}[{count if count else 1}];")
    for i, field in enumerate(fields):
        buf = f"{prefix}_text_{i}"
        lines.append(f"  char {buf}[512];")
        lines.append(f"  {buf}[0] = 0;")
        for part in field.parts:
            if part.child_index is not None:
                lines.append(
                    f"  if ({part.child_index} < n) "
                    f"athena_ns_product_append ({buf}, 512, "
                    f"{source_name}[{part.child_index}].text);"
                )
            else:
                lines.append(
                    f"  athena_ns_product_append ({buf}, 512, "
                    f"\"{c_string_escape(part.literal)}\");"
                )
        lines.append(f"  {prefix}[{i}].text = {buf};")
        lines.append(f"  {prefix}[{i}].type = {FIELD_TYPE_TO_C[field.field_type]};")
        lines.append(f"  {prefix}[{i}].integer = athena_ns_product_parse_int ({buf});")
        lines.append(f"  {prefix}[{i}].roman = athena_ns_roman_value ({buf});")


def field_mappings(plan: ProductPlan) -> tuple[tuple[FieldExpr, ...], tuple[FieldExpr, ...]]:
    if plan.type_label == "Lecture Notes":
        course_fields = (
            FieldExpr("string", (Part(literal="Lecture Notes"),)),
            FieldExpr("roman", (Part(child_index=0),)),
        )
        type_fields = (
            FieldExpr("word", (Part(literal=plan.abbreviation),)),
            FieldExpr("roman", (Part(child_index=0),)),
        )
    elif plan.type_label in ("Assignments", "Extras"):
        course_fields = (
            FieldExpr(
                "string",
                (Part(literal=f"{plan.type_label} "), Part(child_index=0)),
            ),
            FieldExpr("roman", (Part(child_index=1),)),
        )
        type_fields = (
            FieldExpr("word", (Part(literal=plan.abbreviation),)),
            FieldExpr("string", (Part(child_index=0),)),
            FieldExpr("roman", (Part(child_index=1),)),
        )
    else:
        raise ValueError(f"unsupported product type: {plan.type_label}")
    return course_fields, type_fields


def product_sorter_source(vault_root: Path, plan: ProductPlan) -> str:
    left_source = sorter_source_for_function(vault_root, plan.course, "athena_ns_compare_left")
    right_source = sorter_source_for_function(vault_root, plan.type_ns, "athena_ns_compare_right")
    course_fields, type_fields = field_mappings(plan)

    lines = [
        "/*",
        " * Generated ATHENA namespace product sorter.",
        f" * Parent 1: {c_string_escape(plan.course.name)}",
        f" * Parent 2: {c_string_escape(plan.type_ns.name)}",
        f" * Product template: {c_string_escape(plan.product_template)}",
        " */",
        "",
        left_source.rstrip(),
        "",
        right_source.rstrip(),
        "",
        "static void",
        "athena_ns_product_append (char* out, int cap, const char* s) {",
        "  int i = 0;",
        "  if (cap <= 0) return;",
        "  while (i + 1 < cap && out[i] != 0) i++;",
        "  if (s == 0) return;",
        "  while (i + 1 < cap && *s != 0) out[i++] = *s++;",
        "  out[i] = 0;",
        "}",
        "",
        "static long long",
        "athena_ns_product_parse_int (const char* s) {",
        "  long long sign = 1, value = 0;",
        "  if (s == 0) return 0;",
        "  if (*s == '-') { sign = -1; s++; }",
        "  while (*s >= '0' && *s <= '9') {",
        "    value = value * 10 + (*s - '0');",
        "    s++;",
        "  }",
        "  return sign * value;",
        "}",
        "",
        "int",
        "athena_ns_compare (int n, const AthenaNsField* a, const AthenaNsField* b) {",
    ]
    append_field_build_code(lines, "left_a", "a", course_fields)
    append_field_build_code(lines, "left_b", "b", course_fields)
    append_field_build_code(lines, "right_a", "a", type_fields)
    append_field_build_code(lines, "right_b", "b", type_fields)
    lines.extend(
        [
            f"  int c1 = athena_ns_compare_left ({len(course_fields)}, left_a, left_b);",
            f"  int c2 = athena_ns_compare_right ({len(type_fields)}, right_a, right_b);",
            "  if (c1 < 0 || c2 < 0) return -1;",
            "  if (c1 > 0 || c2 > 0) return 1;",
            "  return 0;",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def generated_sorter_path(vault_root: Path, plan: ProductPlan, timestamp: int) -> Path:
    directory = vault_root / ".athena" / "ns-sorters"
    stem = (
        "product-"
        + safe_file_component(plan.course.name)
        + "-"
        + safe_file_component(plan.type_ns.name)
        + "-"
        + str(timestamp)
    )
    for i in range(1000):
        suffix = "" if i == 0 else f"-{i}"
        path = directory / f"{stem}{suffix}.c"
        if not path.exists():
            return path
    raise RuntimeError(f"could not choose sorter path for {plan.product_name}")


def upsert_product_namespace(
    db: sqlite3.Connection,
    plan: ProductPlan,
    sorter_rel_path: str,
) -> None:
    db.execute(
        """
        INSERT INTO namespaces
          (name, kind, template, sorter_trivial, sorter_path, style_path,
           initial_content_path, homepage_path)
        VALUES (?, 'semi-concrete', ?, 0, ?, '', '', '')
        ON CONFLICT(name) DO UPDATE SET
          kind=excluded.kind,
          template=excluded.template,
          sorter_trivial=excluded.sorter_trivial,
          sorter_path=excluded.sorter_path,
          style_path=excluded.style_path,
          initial_content_path=excluded.initial_content_path,
          homepage_path=excluded.homepage_path;
        """,
        (plan.product_name, plan.product_template, sorter_rel_path),
    )
    db.execute(
        "DELETE FROM namespace_parents WHERE child=? AND source='declared';",
        (plan.product_name,),
    )
    for order, parent in enumerate((plan.course.name, plan.type_ns.name)):
        db.execute(
            """
            INSERT OR REPLACE INTO namespace_parents(child, parent, source, ord)
            VALUES (?, ?, 'declared', ?);
            """,
            (plan.product_name, parent, order),
        )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create course/type sub-product namespaces."
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
        "--apply",
        action="store_true",
        help="actually write sorter files and ns.sqlite; otherwise dry-run",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    vault_root = args.vault_root.resolve()
    db_path = args.db.resolve() if args.db is not None else vault_root / DEFAULT_DB_NAME
    lecture_notes = vault_root / LECTURE_NOTES_REL

    if not lecture_notes.is_dir():
        print(f"error: lecture-notes directory not found: {lecture_notes}", file=sys.stderr)
        return 2
    if not db_path.is_file():
        print(f"error: namespace database not found: {db_path}", file=sys.stderr)
        return 2

    if args.apply:
        db = sqlite3.connect(db_path)
    else:
        db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)

    timestamp = int(time.time())
    written_sorters: list[Path] = []
    try:
        with db:
            db.execute("PRAGMA foreign_keys=ON;")
            if args.apply:
                ensure_schema(db)
            plans = product_plans(vault_root, db)
            if not plans:
                print("No course/type sub-products to create.")
                return 0

            action = "upsert" if args.apply else "would upsert"
            for plan in plans:
                existing = namespace_by_name(db, plan.product_name) is not None
                sorter_path = generated_sorter_path(vault_root, plan, timestamp)
                sorter_rel = sorter_path.relative_to(vault_root).as_posix()
                print(
                    f"{action} ({'update' if existing else 'create'}): {plan.product_name}\n"
                    f"  parents:   {plan.course.name}; {plan.type_ns.name}\n"
                    f"  template:  {plan.product_template}\n"
                    f"  sorter:    {sorter_rel}\n"
                    f"  matches:   {len(plan.matching_files)}"
                )
                if args.apply:
                    sorter_path.parent.mkdir(parents=True, exist_ok=True)
                    sorter_path.write_text(
                        product_sorter_source(vault_root, plan),
                        encoding="utf-8",
                    )
                    written_sorters.append(sorter_path)
                    upsert_product_namespace(db, plan, sorter_rel)

            if args.apply:
                db.commit()
            else:
                db.rollback()
                print("\nDry run only. Re-run with --apply to write sorters and ns.sqlite.")

        print(f"Prepared: {len(plans)}")
        return 0
    except Exception:
        for path in written_sorters:
            try:
                path.unlink()
            except OSError:
                pass
        raise


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
