/******************************************************************************
* MODULE     : native_v3_cold_labels_test.cpp
* DESCRIPTION: Strict cold XML decoding uses shipped declarations, never input
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "tree_label.hpp"

namespace xml= athena::document;

class NativeV3ColdLabelsTest: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () {
    // No Guile, application boot, DRD heuristics, style loading or v1/v2 read.
    init_standard_tree_labels ();
  }
  void shippedMacroDecodesCold () {
    for (const char* name: {"strong", "em", "section", "subsection*",
                           "description", "todo-list", "cloud-todo-list",
                           "enumerate-numeric", "tabular"}) {
      const tree_label label= as_tree_label (name);
      QVERIFY (label != UNKNOWN);
      const std::string bytes= std::string ("<a v=\"3\" k=\"t\"><n t=\"") + name +
        "\"><t><v>cold source</v></t></n></a>";
      const auto decoded= xml::decode_native_xml_bytes (bytes, xml::xml_kind::fragment);
      QCOMPARE (decoded.version, xml::xml_format_version::v3);
      QCOMPARE (decoded.document, tree (label, "cold source"));
      init_standard_tree_labels ();
      QCOMPARE (as_tree_label (name), label);
    }
  }
  void nativeNamespacePlaceholdersDecodeCold () {
    for (const char* name: {"ns-name", "ns-type", "ns-sorting-algo", "ns-matches",
                           "ns-children", "ns-parents", "ns-filename-template",
                           "ns-summary-link"}) {
      QVERIFY (existing_tree_label (name));
      const std::string bytes= std::string ("<a v=\"3\" k=\"t\"><n t=\"") + name + "\"/></a>";
      QCOMPARE (xml::decode_native_xml_bytes (bytes, xml::xml_kind::fragment).document,
                tree (as_tree_label (name)));
    }
  }
  void unknownInputCannotDeclareItself () {
    const string name ("test-cold-undeclared-custom-macro");
    QVERIFY (!existing_tree_label (name));
    const std::string bytes=
      "<a v=\"3\" k=\"t\"><n t=\"test-cold-undeclared-custom-macro\">"
      "<t><v>body</v></t></n></a>";
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::decode_native_xml_bytes (bytes, xml::xml_kind::fragment));
    QVERIFY (!existing_tree_label (name));
    // A custom extension's declaration owner must explicitly register its name.
    const tree_label declared= make_tree_label (name);
    QCOMPARE (xml::decode_native_xml_bytes (bytes, xml::xml_kind::fragment).document,
              tree (declared, "body"));
  }
  void catalogDoesNotEnableLegacyMath () {
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::read_xml_v3 (
      "<a v=\"3\" k=\"t\"><n t=\"around\"><t><v>(</v></t>"
      "<t><v>x</v></t><t><v>)</v></t></n></a>", xml::xml_kind::fragment));
  }
};

QTEST_APPLESS_MAIN (NativeV3ColdLabelsTest)
#include "native_v3_cold_labels_test.moc"
