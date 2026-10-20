/******************************************************************************
* MODULE     : native_v3_cork_migration_test.cpp
* DESCRIPTION: Lossless offline conversion of audited Cork byte identities
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "Xml/athena_document_xml.hpp"
#include "Xml/legacy_cork.hpp"
#include "Xml/math_v2_to_v3.hpp"
#include "drd_std.hpp"
#include "node_metadata.hpp"

using namespace athena::document;
using node_property= athena::node::property;

class TestNativeV3CorkMigration: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void shippedEncodingAndImporterEvidence () {
    const auto directory= qEnvironmentVariable ("ATHENA_PATH") + "/langs/encoding";
    QFile file (directory + "/cork-escaped-to-ascii.json");
    QVERIFY (file.open (QIODevice::ReadOnly));
    const auto mappings= QJsonDocument::fromJson (file.readAll ()).object ()
                          .value ("mappings").toArray ();
    for (const auto& key: {QString ("#3C"), QString ("#3E")}) {
      bool found= false;
      for (const auto& mapping: mappings) {
        const auto pair= mapping.toArray ();
        if (pair.size () != 2 || pair[0].toString () != key) continue;
        QCOMPARE (pair[1].toString (), key == "#3C" ? QString ("&lt;") : QString ("&gt;"));
        found= true;
      }
      QVERIFY (found);
    }
    const legacy_cork_table table (directory.toStdString ());
    const auto pieces= table.decode (">", legacy_text_role::content);
    QCOMPARE (pieces.size (), std::size_t (1));
    QVERIFY (pieces[0].kind == legacy_piece_kind::named_symbol);
    QCOMPARE (pieces[0].value, std::string ("cork:3E"));
  }

  void exactReportTextRemainsText () {
    // Exact failing CONCAT, including the two separate symbol nodes.
    const tree source (DOCUMENT, tree (CONCAT,
      tree ("mode local, documents 0 -"), tree (NAMED_SYMBOL, "cork:3E"),
      tree (" 0, chunks 0 -"), tree (NAMED_SYMBOL, "cork:3E"),
      tree (" 0, result: ")));
    const auto original= write_xml_v2 (source);
    const auto result= migrate_math_v2_tree (read_xml_v2 (original), true);
    const tree expected (DOCUMENT, tree (CONCAT,
      tree ("mode local, documents 0 -"), tree (">"),
      tree (" 0, chunks 0 -"), tree (">"), tree (" 0, result: ")));
    QVERIFY (result.document == expected);
    QVERIFY (result.warnings.empty ());
    QVERIFY (result.removed_identities.empty ());
    QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == expected);
    QCOMPARE (write_xml_v2 (source), original);
  }

  void preserveBothMetadataOwnersAndReferences () {
    for (const char* identity: {"cork:3C", "cork:3E"}) {
      tree symbol (NAMED_SYMBOL, tree (identity));
      athena::node::metadata leaf;
      leaf.id= athena::node::new_id ();
      leaf.properties.emplace ("fixture:description", node_property (std::string ("byte identity")));
      athena::node::set (symbol[0], leaf);
      athena::node::metadata wrapper;
      wrapper.id= athena::node::new_id ();
      wrapper.properties.emplace ("fixture:description", node_property (std::string ("symbol")));
      wrapper.properties.emplace ("fixture:target", node_property (athena::node::reference {leaf.id}));
      athena::node::set (symbol, wrapper);
      tree source (DOCUMENT, symbol);
      athena::node::metadata root;
      root.properties.emplace ("fixture:target", node_property (athena::node::reference {wrapper.id}));
      athena::node::set (source, root);
      const auto original= write_xml_v2 (source);
      const auto result= migrate_math_v2_tree (read_xml_v2 (original), true);
      const tree& converted= result.document[0];
      QVERIFY (is_func (converted, CONCAT, 1));
      QVERIFY (is_atomic (converted[0]));
      QCOMPARE (converted[0]->label, string (identity == std::string ("cork:3C") ? "<" : ">"));
      QVERIFY (athena::node::equal_metadata (converted, symbol));
      QVERIFY (athena::node::equal_metadata (converted[0], symbol[0]));
      QVERIFY (athena::node::equal_metadata (result.document, source));
      QVERIFY (result.removed_identities.empty ());
      QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);
      QCOMPARE (write_xml_v2 (source), original);
    }
  }

  void wrapperMetadataSurvivesScalarConversion () {
    tree symbol (NAMED_SYMBOL, "cork:3E");
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties.emplace ("fixture:reviewed", node_property (true));
    athena::node::set (symbol, meta);
    const auto result= migrate_math_v2_tree (tree (DOCUMENT, symbol), true);
    QVERIFY (is_atomic (result.document[0]));
    QCOMPARE (result.document[0]->label, string (">"));
    QVERIFY (athena::node::equal_metadata (result.document[0], symbol));
    QVERIFY (result.removed_identities.empty ());
    QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);
  }

  void mathAndRichTextUseSameAuditedMapping () {
    tree source (DOCUMENT, compound ("math", tree (CONCAT,
      tree ("x"), tree (NAMED_SYMBOL, "cork:3E"), tree ("y"))));
    athena::node::metadata meta;
    meta.properties.emplace ("fixture:caption", node_property (athena::node::rich_text {
      tree (CONCAT, tree (NAMED_SYMBOL, "cork:3C"), tree ("tag>"))}));
    athena::node::set (source, meta);
    const auto result= migrate_math_v2_tree (source, true);
    QVERIFY (result.document[0][0][1] == tree (">"));
    const auto& rich= std::get<athena::node::rich_text> (
      athena::node::get (result.document)->properties.at ("fixture:caption").data);
    QVERIFY (tree (rich.content) == tree (CONCAT, tree ("<"), tree ("tag>")));
    QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);
  }

  void unsupportedAndNoncanonicalIdentitiesStillFail () {
    for (const char* identity: {"cork:18", "cork:1A", "cork:DF", "cork:FF",
                               "cork:3e", "cork:03E", "cork:3Eextra"})
      for (bool strict: {false, true})
        QVERIFY_THROWS_EXCEPTION (std::runtime_error, migrate_math_v2_tree (
          tree (DOCUMENT, tree (NAMED_SYMBOL, tree (identity))), strict));
  }
};

QTEST_GUILESS_MAIN (TestNativeV3CorkMigration)
#include "native_v3_cork_migration_test.moc"
