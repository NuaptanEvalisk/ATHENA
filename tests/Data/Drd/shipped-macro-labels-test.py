#!/usr/bin/env python3
"""Regression tests for declaration-only shipped macro label generation."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "shipped_labels", ROOT / "tools/generate-shipped-macro-labels.py")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def document(body):
    return ('<athena-document version="1" text-model="utf-8">'
            '<node tag="document"><node tag="body"><node tag="document">'
            + body + '</node></node></node></athena-document>')


def assignment(name, value):
    return '<node tag="assign">' + name + value + '</node>'


MACRO = '<node tag="macro"><text>body</text><text>result</text></node>'
XMACRO = '<node tag="xmacro"><text>args</text><text>result</text></node>'


class ShippedMacroLabelsTest(unittest.TestCase):
    def parse(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.ats"
            path.write_text(source, encoding="utf-8")
            return GENERATOR.declarations(path)

    def test_literal_macro_and_xmacro_declarations_only(self):
        source = document(
            assignment('<text>ordinary</text>', MACRO)
            + assignment('<text>variadic</text>', XMACRO)
            + assignment('<text>ordinary</text>', MACRO)
            + assignment('<text>variable</text>', '<text>value</text>')
            + assignment('<text>alias</text>', '<node tag="value"><text>ordinary</text></node>')
            + '<node tag="undeclared-use"><text>body</text></node>')
        names, skipped = self.parse(source)
        self.assertEqual(names, {"ordinary", "variadic"})
        self.assertEqual(skipped, 0)

    def test_nested_literal_declarations_but_no_computed_name_evaluation(self):
        computed = '<node tag="merge"><text>prefix-</text><node tag="arg"><text>name</text></node></node>'
        nested = assignment('<text>nested</text>', MACRO)
        source = document(assignment('<text>outer</text>',
            '<node tag="macro"><text>name</text>' + nested
            + assignment(computed, MACRO) + '</node>'))
        names, skipped = self.parse(source)
        self.assertEqual(names, {"outer", "nested"})
        self.assertEqual(skipped, 1)

    def test_cpp_strings_preserve_utf8_quotes_and_backslashes(self):
        names, _ = self.parse(document(assignment('<text>a&quot;\\&#233;7</text>', MACRO)))
        self.assertEqual(names, {'a"\\\u00e97'})
        self.assertEqual(GENERATOR.cpp_string('a"\\\u00e97'), '"a\\"\\\\\\303\\2517"')
        self.assertEqual(GENERATOR.cpp_string("line\n"), '"line\\012"')
        self.assertTrue(GENERATOR.render(names).isascii())

    def test_unsupported_and_malformed_sources_fail(self):
        bad_sources = (
            '<athena-document>',
            document('').replace('version="1"', 'version="3"'),
            document(assignment('<text/>', MACRO)),
            document(assignment('<text encoding="base64">YQ==</text>', MACRO)),
            '<!DOCTYPE athena-document [<!ENTITY name "injected">]>'
            + document(assignment('<text>&name;</text>', MACRO)),
        )
        for source in bad_sources:
            with self.subTest(source=source), self.assertRaises(GENERATOR.DeclarationError):
                self.parse(source)

    def test_catalog_is_sorted_deduplicated_and_excludes_user_examples(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("packages", "styles", "examples"):
                (root / name).mkdir()
            (root / "packages/z.ats").write_text(document(
                assignment('<text>z</text>', MACRO) + assignment('<text>a</text>', XMACRO)), encoding="utf-8")
            (root / "styles/a.ats").write_text(document(assignment('<text>a</text>', MACRO)), encoding="utf-8")
            (root / "examples/user.ats").write_text(document(assignment('<text>user-only</text>', MACRO)), encoding="utf-8")
            names, skipped, count = GENERATOR.catalog(root)
            self.assertEqual(names, ["a", "z"])
            self.assertEqual(skipped, {})
            self.assertEqual(count, 2)
            self.assertEqual(GENERATOR.render(names), GENERATOR.render(reversed(names)))
            (root / "packages/z.ats").unlink()
            self.assertEqual(GENERATOR.catalog(root)[0], ["a"])

    def test_shipped_source_of_truth_contains_cold_text_macros(self):
        names, skipped, count = GENERATOR.catalog(ROOT / "ATHENA")
        self.assertIn("strong", names)
        self.assertIn("em", names)
        self.assertGreater(count, 0)
        self.assertGreater(sum(skipped.values()), 0)
        self.assertEqual(names, sorted(set(names)))


if __name__ == "__main__":
    unittest.main()
