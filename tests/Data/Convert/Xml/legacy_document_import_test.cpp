/******************************************************************************
* MODULE     : legacy_document_import_test.cpp
* DESCRIPTION: Lossless legacy text roles, structured symbols and relocation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include "Xml/legacy_document_import.hpp"
#include "drd_std.hpp"
#include "convert.hpp"
#include <future>

using namespace athena::document;
bool headless_mode= true;
bool is_headless () { return true; }

namespace {
legacy_cork_table table () {
  return legacy_cork_table ((qEnvironmentVariable ("ATHENA_PATH") + "/langs/encoding").toStdString ());
}
std::string text_of (const std::vector<legacy_text_piece>& pieces) {
  std::string out;
  for (const auto& p: pieces) {
    if (p.kind != legacy_piece_kind::text) throw std::runtime_error ("Expected text");
    out += p.value;
  }
  return out;
}
tree doc (tree body) {
  return tree (DOCUMENT, compound ("TeXmacs", "2.1.4"), compound ("body", body));
}
}

class TestLegacyImport: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }
  void charactersAndRoles () {
    auto t= table ();
    QCOMPARE (text_of (t.decode ("<#4E2D><#1F469><#200D><#1F4BB>", legacy_text_role::content)),
              std::string (u8"\u4e2d\U0001f469\u200d\U0001f4bb"));
    QCOMPARE (text_of (t.decode ("<less>alpha<gtr>", legacy_text_role::content)), std::string ("<alpha>"));
    QCOMPARE (text_of (t.decode ("<alpha>", legacy_text_role::identifier)), std::string ("<alpha>"));
    QCOMPARE (text_of (t.decode ("`...<less>x<gtr>\n", legacy_text_role::code)), std::string ("`...<x>\n"));
    // Even valid-looking UTF-8 bytes are explicitly old Cork, never guessed.
    QVERIFY (text_of (t.decode ("\xc3\xa9", legacy_text_role::content)) != std::string ("\xc3\xa9"));
    QCOMPARE (text_of (t.decode ("\xe9", legacy_text_role::content)), std::string (u8"\u00e9"));
    QCOMPARE (text_of (t.decode ("...", legacy_text_role::content)), std::string ("..."));
    QCOMPARE (text_of (t.decode ("%\x18\x18", legacy_text_role::content)), std::string (u8"\u2031"));
  }
  void noApproximation () {
    auto t= table ();
    auto a= t.decode ("<mathD><mathcatalan><unknown-glyph>\xdf", legacy_text_role::content);
    QCOMPARE (a.size (), std::size_t (4));
    for (const auto& p: a) QVERIFY (p.kind == legacy_piece_kind::named_symbol);
    QCOMPARE (a[0].value, std::string ("texmacs:mathD"));
    QCOMPARE (a[1].value, std::string ("texmacs:mathcatalan"));
    QCOMPARE (a[3].value, std::string ("cork:DF"));
    QVERIFY_THROWS_EXCEPTION (legacy_text_error, t.decode ("<mathD>", legacy_text_role::code));
    for (auto bad: {"<#>", "<#D800>", "<#110000>", "<#ZZ>", "<unclosed", "<<bad>"})
      QVERIFY_THROWS_EXCEPTION (legacy_text_error, t.decode (bad, legacy_text_role::content));
    QVERIFY_THROWS_EXCEPTION (legacy_text_error, t.decode ("a<#4E2D>", legacy_text_role::content, 3));
    QVERIFY_THROWS_EXCEPTION (legacy_text_error, t.decode ("\xe9\xe9\xe9", legacy_text_role::content, 100, 2));
  }
  void structureAndMapping () {
    auto t= table ();
    tree body (DOCUMENT, "x<mathD>\xe9", tree (RAW_DATA, tree (string ("\0\xff", 2))),
               tree (LABEL, "<alpha>"), compound ("TeXmacs", "nested logo"));
    tree source= doc (body);
    auto result= import_legacy_document (source, t);
    auto converted= result.document[0][0];
    QCOMPARE (N (source), 2);
    QCOMPARE (N (result.document), 1);
    QVERIFY (converted[0] == tree (CONCAT, "x", compound ("named-symbol", "texmacs:mathD"), tree (u8"\u00e9")));
    QVERIFY (converted[1] == body[1]);
    QVERIFY (converted[2] == body[2]);
    QVERIFY (converted[3] == body[3]);
    QVERIFY (read_xml (write_xml (result.document)) == result.document);
    auto before= result.relocate ({1, 0, 0}, 1, boundary_affinity::preceding);
    auto after= result.relocate ({1, 0, 0}, 1, boundary_affinity::following);
    QVERIFY (before && after);
    QVERIFY (before->node == document_path ({0, 0, 0, 0}));
    QCOMPARE (before->offset, std::size_t (1));
    QVERIFY (after->node == document_path ({0, 0, 0, 1}));
    QCOMPARE (after->offset, std::size_t (0));
    QVERIFY (!result.relocate ({1, 0, 0}, 3, boundary_affinity::following));
    auto end= result.relocate ({1, 0, 0}, 9, boundary_affinity::following);
    QVERIFY (end && end->node == document_path ({0, 0, 0, 2}));
    QCOMPARE (end->offset, std::size_t (2));
    QVERIFY (!result.relocate ({0, 0}, 0, boundary_affinity::following));
    QVERIFY (!result.relocate_node ({0}));
    QVERIFY (result.relocate_node ({1, 0, 1, 0}) == std::optional<document_path> (document_path {0, 0, 1, 0}));
    auto linear= import_legacy_document (doc ("ordinary text"), t);
    auto interior= linear.relocate ({1, 0}, 4, boundary_affinity::following);
    QVERIFY (interior && interior->node == document_path ({0, 0}) && interior->offset == 4);
  }
  void macrosAndBudgets () {
    auto t= table ();
    tree source= doc (tree (MACRO, "<arg>", tree (CONCAT, tree (ARG, "<arg>"), "<mathD>")));
    auto out= import_legacy_document (source, t).document[0][0];
    QVERIFY (out[0] == "<arg>");
    QVERIFY (out[1][0][0] == "<arg>");
    QVERIFY (is_func (out[1][1], CONCAT));
    auto custom= import_legacy_document (doc (compound ("custom", "`<less>\n")), t, {},
      [] (const tree& parent, int, legacy_text_role inherited) {
        return is_compound (parent, "custom") ? legacy_text_role::code : inherited;
      });
    QVERIFY (custom.document[0][0][0] == "`<\n");
    legacy_import_limits limit;
    limit.positions= 1;
    QVERIFY_THROWS_EXCEPTION (legacy_text_error, import_legacy_document (doc ("\xe9\xe9"), t, limit));
    limit= {}; limit.codec.depth= 1;
    QVERIFY_THROWS_EXCEPTION (codec_exception, import_legacy_document (source, t, limit));
  }
  void independentWorkers () {
    auto t= table ();
    auto task= [&t] {
      auto r= import_legacy_document (doc ("<mathD><#4E2D>"), t);
      return write_xml (r.document);
    };
    auto first= std::async (std::launch::async, task);
    auto second= std::async (std::launch::async, task);
    QCOMPARE (first.get (), second.get ());
  }
  void oldFormatsAndXml () {
    auto t= table ();
    tree original= doc (tree (DOCUMENT, "<less>x<gtr><#4E2D><mathD>",
                              tree (RAW_DATA, tree (string ("\0\xff\x80", 3)))));
    QVERIFY (texmacs_document_to_tree (tree_to_texmacs (original)) == original);
    QVERIFY (scheme_document_to_tree (tree_to_scheme (original)) == original);
    for (const auto& serialized: {tree_to_texmacs (original), tree_to_scheme (original)}) {
      auto imported= import_legacy_document_bytes (
        std::string_view (as_charp (serialized), N (serialized)), t);
      const auto expected= import_legacy_document (original, t).document;
      const auto diagnostic= write_xml (imported.document) + "\nEXPECTED: " + write_xml (expected);
      QVERIFY2 (imported.document == expected, diagnostic.c_str ());
      QVERIFY (read_xml (write_xml (imported.document)) == imported.document);
    }
    for (const std::string header: {"(apply \"TeXmacs\" \"2.1.4\")", "(expand \"TeXmacs\" \"2.1.4\")"}) {
      auto imported= import_legacy_document_bytes ("(document " + header + " (body \"x\"))", t);
      QVERIFY (imported.document == tree (DOCUMENT, compound ("body", "x")));
    }
    QVERIFY (import_legacy_document_bytes (
      "(document (TeXmacs \"2.1.4\") (body \"x\" ; trailing comment\n))", t).document ==
      tree (DOCUMENT, compound ("body", "x")));
    for (const std::string invalid: {
         "<TeXmacs|2.1.4>>", "<TeXmacs|2.1.4><\\body>bad</wrong>",
         "<TeXmacs|2.1.4><\\body><#F></body>",
         "(document (TeXmacs \"2.1.4\") (body \"unterminated))",
         "(document (TeXmacs \"2.1.4\") (body \"x\")) trailing",
         "(document (TeXmacs \"2.1.4\") ())",
         "(document (TeXmacs \"2.1.4\" \"extra\") (body \"x\"))",
         "(document (TeXmacs \"2.1.4\") (body unquoted))"})
      QVERIFY_THROWS_EXCEPTION (codec_exception, import_legacy_document_bytes (invalid, t));
    legacy_import_limits budget;
    budget.codec.depth= 3;
    QVERIFY_THROWS_EXCEPTION (codec_exception, import_legacy_document_bytes (
      "(document (TeXmacs \"2.1.4\") (body (a (b (c (d \"x\"))))))", t, budget));
    QVERIFY_THROWS_EXCEPTION (codec_exception, import_legacy_document_bytes (
      "<TeXmacs|2.1.4><body|<a|<b|<c|<d|x>>>>>", t, budget));
  }
};
QTEST_GUILESS_MAIN (TestLegacyImport)
#include "legacy_document_import_test.moc"
