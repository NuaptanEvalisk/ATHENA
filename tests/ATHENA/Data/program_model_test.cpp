/******************************************************************************
* MODULE     : program_model_test.cpp
* DESCRIPTION: Canonical program nodes and KDE syntax definition regressions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QtTest/QtTest>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <functional>

#include "ATHENA/Data/program_model.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "Xml/athena_document_xml.hpp"
#include "drd_std.hpp"
#include "native_editor_actions.hpp"
#include "program_presentation.hpp"

namespace program= athena::program;
namespace node= athena::node;

class TestProgramModel: public QObject {
  Q_OBJECT

private slots:
  void initTestCase () { init_std_drd (); }
  void one_node_type_carries_language_property ();
  void arbitrary_language_is_valid_source ();
  void metadata_survives_document_roundtrip ();
  void native_presentation_uses_language_environment ();
  void kde_catalog_is_available ();
  void editor_action_requires_language ();
};

void
TestProgramModel::one_node_type_carries_language_property () {
  tree python= program::create ("Python");
  tree rust= program::create ("Rust");
  QVERIFY (program::is_canonical (python));
  QVERIFY (program::is_canonical (rust));
  QCOMPARE (L(python), L(rust));
  QCOMPARE (as_string (L(python)), string ("program"));
  QCOMPARE (*program::language (python), std::string ("Python"));
  QCOMPARE (*program::language (rust), std::string ("Rust"));
  QCOMPARE (python[0], tree (DOCUMENT, ""));
}

void
TestProgramModel::arbitrary_language_is_valid_source () {
  tree source= program::create (
    "A language that is not installed in KSyntaxHighlighting",
    tree (DOCUMENT, "print something"));
  QVERIFY (program::is_canonical (source));
  QVERIFY (athena::document_node::validate_node_properties (source).empty ());
  QCOMPARE (*program::language (source),
            std::string ("A language that is not installed in KSyntaxHighlighting"));

  node::metadata malformed= *node::get (source);
  malformed.properties["language"]= node::property (true);
  node::set (source, malformed);
  QVERIFY (!program::is_canonical (source));
}

void
TestProgramModel::metadata_survives_document_roundtrip () {
  using namespace athena::document;
  tree source= program::create ("Zig", tree (DOCUMENT, "const x = 1;"));
  const std::string encoded= write_xml_v2 (source, xml_kind::fragment);
  tree decoded= read_xml_v2 (encoded, xml_kind::fragment);
  QVERIFY (program::is_canonical (decoded));
  QCOMPARE (*program::language (decoded), std::string ("Zig"));
  QCOMPARE (decoded[0], tree (DOCUMENT, "const x = 1;"));
}

void
TestProgramModel::native_presentation_uses_language_environment () {
  tree macro= native_program_macro (program::create ("Python"));
  QVERIFY (is_func (macro, MACRO));

  // The source stays `program`; language is projected into the evaluation
  // environment only by the native presentation macro.
  bool found= false;
  std::function<void(const tree&)> visit= [&] (const tree& t) {
    if (is_func (t, WITH) && N(t) >= 3)
      for (int i= 0; i + 1 < N(t) - 1; i += 2)
        if (is_atomic (t[i]) && t[i] == tree ("prog-language") &&
            t[i + 1] == tree ("Python"))
          found= true;
    if (is_compound (t))
      for (int i= 0; i < N(t); ++i) visit (t[i]);
  };
  visit (macro);
  QVERIFY (found);
}

void
TestProgramModel::kde_catalog_is_available () {
  KSyntaxHighlighting::Repository repository;
  const auto definitions= repository.definitions ();
  QVERIFY2 (definitions.size () > 100,
            "KF6 syntax catalog should expose the KDE/KWrite language set");
  QVERIFY (repository.definitionForName (QStringLiteral ("Python")).isValid ());
  QVERIFY (repository.definitionForName (QStringLiteral ("C++")).isValid ());
  QVERIFY (repository.definitionForName (QStringLiteral ("Rust")).isValid ());
  QVERIFY (repository.definitionForName (QStringLiteral ("Zig")).isValid ());
}

void
TestProgramModel::editor_action_requires_language () {
  QJsonObject valid {
    {QStringLiteral ("op"), QStringLiteral ("make-program")},
    {QStringLiteral ("language"), QStringLiteral ("Python")}
  };
  QString error;
  QVERIFY (native_editor_action_validate (valid, &error));

  QJsonObject empty= valid;
  empty.insert (QStringLiteral ("language"), QStringLiteral ("   "));
  QVERIFY (!native_editor_action_validate (empty, &error));
}

QTEST_MAIN (TestProgramModel)
#include "program_model_test.moc"
