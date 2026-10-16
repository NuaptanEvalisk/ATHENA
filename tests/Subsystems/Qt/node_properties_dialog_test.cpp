/******************************************************************************
* MODULE     : node_properties_dialog_test.cpp
* DESCRIPTION: Native metadata form preservation and rejected-edit feedback
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTreeWidget>
#include <memory>
#include "Subsystems/Qt/QTMNodePropertiesDialog.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Tree/math_tree_v3.hpp"
#include "drd_std.hpp"

namespace node= athena::node;
namespace model= athena::document_node;
namespace xml= athena::document;

namespace {
tree source () {
  tree result (athena::enunciation::label (), tree (DOCUMENT, "Body"));
  node::metadata metadata;
  metadata.id= "11111111-1111-4111-8111-111111111111";
  metadata.properties["kind"]= node::property (std::string ("theorem"));
  metadata.properties["variant"]= node::property (std::string ("unknown-extension-variant"));
  metadata.properties["numbered"]= node::property (true);
  metadata.properties["name"]= node::property (node::rich_text {
    tree (CONCAT, "Named ", tree (EXTERN, "must-not-run"))});
  metadata.properties["attribution"]= node::property (node::property::list {
    node::property (node::rich_text {tree ("Author")})});
  metadata.properties["test:integer"]= node::property (std::int64_t (9223372036854775807LL));
  metadata.properties["athena:artifact-bindings"]= node::property (node::property::dictionary {
    {"statement", node::property (std::string ("22222222-2222-4222-8222-222222222222"))}});
  node::set (result, metadata);
  return result;
}
std::string snapshot (const tree& t) {
  return encode_node_property_snapshot (t, xml::xml_format_version::v2);
}
void accept (QDialog* dialog) {
  dialog->findChild<QDialogButtonBox*> ()->button (QDialogButtonBox::Ok)->click ();
}
}

class NodePropertiesDialogTest: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }
  void unchangedDraftIsLossless () {
    tree original= source ();
    int calls= 0;
    std::unique_ptr<QDialog> dialog (make_node_properties_dialog (snapshot (original), true, true,
      [&] (std::string bytes, node_property_completion complete) {
        ++calls;
        auto decoded= decode_node_property_snapshot (bytes);
        QCOMPARE (decoded.source_version, xml::xml_format_version::v2);
        auto desired= decoded.header;
        QCOMPARE (desired, model::property_header (original));
        auto prepared= model::prepare_property_replacement (original, {},
          model::property_header (original), desired);
        QVERIFY (prepared.ok () && !prepared.change);
        complete ("");
      }));
    auto* uuid= dialog->findChild<QLineEdit*> ("node-uuid");
    QVERIFY (uuid && uuid->isReadOnly ());
    QCOMPARE (uuid->text (), QString::fromStdString (node::id (original)));
    QCOMPARE (dialog->findChild<QComboBox*> ("enunciation-variant")->currentText (),
              QString ("unknown-extension-variant"));
    if (!qEnvironmentVariableIsEmpty ("ATHENA_TEST_SCREENSHOT")) {
      dialog->show ();
      QTest::qWait (50);
      QVERIFY (dialog->grab ().save (qEnvironmentVariable ("ATHENA_TEST_SCREENSHOT")));
    }
    accept (dialog.get ());
    QCOMPARE (calls, 1);
    QCOMPARE (dialog->result (), int (QDialog::Accepted));
  }
  void editPreservesReservedAndStructuredProperties () {
    tree original= source (), output;
    std::unique_ptr<QDialog> dialog (make_node_properties_dialog (snapshot (original), true, true,
      [&] (std::string bytes, node_property_completion complete) {
        auto prepared= model::prepare_property_replacement (original, {},
          model::property_header (original), decode_node_property_snapshot (bytes).header);
        QVERIFY (prepared.ok () && prepared.change);
        output= clean_apply (original, *prepared.change);
        complete ("");
      }));
    dialog->findChild<QLineEdit*> ("enunciation-year")->setText ("19XX");
    dialog->findChild<QLineEdit*> ("enunciation-target")->setText ("33333333-3333-4333-8333-333333333333");
    accept (dialog.get ());
    QCOMPARE (output[0], original[0]);
    QCOMPARE (node::id (output), node::id (original));
    for (const char* key: {"name", "variant", "attribution", "test:integer", "athena:artifact-bindings"})
      QVERIFY (node::equal (node::get (original)->properties.at (key), node::get (output)->properties.at (key)));
    QCOMPARE (std::get<std::string> (node::get (output)->properties.at ("year").data), std::string ("19XX"));
  }
  void rejectedDraftRemainsEditable () {
    std::unique_ptr<QDialog> dialog (make_node_properties_dialog (snapshot (source ()), true, true,
      [] (std::string, node_property_completion complete) { complete ("Node no longer exists"); }));
    accept (dialog.get ());
    QCOMPARE (dialog->result (), int (QDialog::Rejected));
    QCOMPARE (dialog->findChild<QLabel*> ("node-properties-error")->text (), QString ("Node no longer exists"));
    QVERIFY (dialog->findChild<QDialogButtonBox*> ()->isEnabled ());
  }
  void genericAndReadOnlyInspection () {
    for (bool canonical: {false, true}) {
      std::unique_ptr<QDialog> dialog (make_node_properties_dialog (snapshot (source ()), canonical, false,
        [] (std::string, node_property_completion) { QFAIL ("Inspector must not mutate source"); }));
      auto* buttons= dialog->findChild<QDialogButtonBox*> ();
      QVERIFY (!buttons->button (QDialogButtonBox::Ok));
      QVERIFY (buttons->button (QDialogButtonBox::Close));
      QVERIFY (dialog->findChild<QTreeWidget*> ("node-property-inspector")->topLevelItemCount () >= 7);
      if (canonical) QVERIFY (!dialog->findChild<QComboBox*> ("enunciation-kind")->isEnabled ());
    }
  }
  void nativeHeaderAndRichFields () {
    tree original= source ();
    const tree scripts= athena::math_v3::scripts ("x", "", "", "i", "", 'r');
    node::edit (original)->properties["name"]= node::property (node::rich_text {scripts});
    node::edit (original)->properties["attribution"]= node::property (node::property::list {
      node::property (node::rich_text {tree (MATH_VARIANT, "bb", "R")})});
    for (auto version: {xml::xml_format_version::v1, xml::xml_format_version::v2,
                        xml::xml_format_version::v3}) {
      const auto bytes= encode_node_property_snapshot (original, version);
      const auto decoded= decode_node_property_snapshot (bytes);
      QCOMPARE (decoded.source_version, version);
      QCOMPARE (decoded.header, model::property_header (original));
      QCOMPARE (xml::inspect_xml_version (bytes), version == xml::xml_format_version::v3 ?
        xml::xml_format_version::v3 : xml::xml_format_version::v2);
      int calls= 0;
      std::unique_ptr<QDialog> dialog (make_node_properties_dialog (bytes, true, true,
        [&] (std::string desired, node_property_completion complete) {
          ++calls;
          const auto result= decode_node_property_snapshot (desired);
          QCOMPARE (result.source_version, version);
          QCOMPARE (result.header, decoded.header);
          complete ("");
        }));
      accept (dialog.get ());
      QCOMPARE (calls, 1);
    }
    tree delimiter= athena::math_v3::delimited ("(", ")", 'a', scripts);
    node::metadata delimiter_metadata;
    delimiter_metadata.id= node::id (original);
    node::set (delimiter, delimiter_metadata);
    const auto bytes= encode_node_property_snapshot (delimiter, xml::xml_format_version::v3);
    const auto decoded= decode_node_property_snapshot (bytes);
    QCOMPARE (decoded.header, model::property_header (delimiter));
    QVERIFY (is_func (decoded.header, DELIMITED, 0));
    const auto script_header= decode_node_property_snapshot (
      encode_node_property_snapshot (scripts, xml::xml_format_version::v3));
    QCOMPARE (script_header.header, model::property_header (scripts));
    QCOMPARE (std::get<std::string> (node::get (script_header.header)->properties.at (
      athena::math_v3::scripts_policy_key).data), std::string ("r"));
    std::unique_ptr<QDialog> dialog (make_node_properties_dialog (bytes, false, false, {}));
    QCOMPARE (dialog->findChild<QLineEdit*> ("node-uuid")->text (),
              QString::fromStdString (node::id (delimiter)));
  }
  void rejectsWrongEnvelopeAndMalformedCarrier () {
    const auto bytes= encode_node_property_snapshot (source (), xml::xml_format_version::v3);
    auto carrier= xml::decode_native_xml_bytes (bytes, xml::xml_kind::fragment).document;
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, decode_node_property_snapshot (
      xml::write_xml_v2 (carrier, xml::xml_kind::fragment)));
    carrier[1]= "2";
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, decode_node_property_snapshot (
      xml::write_xml_v3 (carrier, xml::xml_kind::fragment)));
    carrier[1]= "9";
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, decode_node_property_snapshot (
      xml::write_xml_v3 (carrier, xml::xml_kind::fragment)));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, decode_node_property_snapshot (
      xml::write_xml_v3 (tree (TUPLE, "unrelated"), xml::xml_kind::fragment)));
    tree legacy= source ();
    node::edit (legacy)->properties["name"]= node::property (node::rich_text {
      tree (AROUND, "(", "x", ")")});
    QVERIFY_THROWS_EXCEPTION (std::exception,
      encode_node_property_snapshot (legacy, xml::xml_format_version::v3));
  }
  void rejectsUnknownHeaderLabelWithoutRegistration () {
    const string unknown ("test-unregistered-node-property-carrier-label");
    QVERIFY (!existing_tree_label (unknown));
    for (auto version: {xml::xml_format_version::v2, xml::xml_format_version::v3}) {
      tree carrier (TUPLE, "node-property-header",
                    version == xml::xml_format_version::v3 ? "3" : "2", tree (unknown));
      const auto bytes= xml::encode_native_xml_bytes (carrier, version, xml::xml_kind::fragment);
      QVERIFY_THROWS_EXCEPTION (std::invalid_argument, decode_node_property_snapshot (bytes));
      QVERIFY (!existing_tree_label (unknown));
    }
  }
};

QTEST_MAIN (NodePropertiesDialogTest)
#include "node_properties_dialog_test.moc"
