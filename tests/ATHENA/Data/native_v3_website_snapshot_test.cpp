/******************************************************************************
* MODULE     : native_v3_website_snapshot_test.cpp
* DESCRIPTION: Website snapshots preserve source format and rich node metadata
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "ATHENA/Data/websites_internal.hpp"
#include "Data/Tree/math_tree_v3.hpp"
#include "node_metadata_transform.hpp"
#include "Data/NodeContracts/execution_state.hpp"

namespace {
namespace node= athena::node;
namespace math= athena::math_v3;
namespace doc= athena::document;
using namespace athena_websites;

void identify (tree& value) {
  node::metadata metadata;
  if (const auto* original= node::get (value)) metadata= *original;
  metadata.id= node::new_id ();
  node::set (value, metadata);
}

tree fixture_document (const tree& body) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")),
               compound ("body", tree (DOCUMENT, body)));
}
}

class NativeV3WebsiteSnapshotTest: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { athena::contracts::initialize_builtins (); }

  void legacySymbolConversionRetainsMetadata () {
    tree script= math::scripts ("x", "", "", "i", "", 'r');
    identify (script);
    tree symbol= compound ("copyright");
    identify (symbol);
    node::metadata metadata;
    metadata.id= node::new_id ();
    metadata.properties.emplace ("test:rich", node::property (
      node::property::dictionary {{"nested", node::property (node::property::list {
        node::property (node::rich_text {symbol})})}}));
    metadata.properties.emplace ("test:reference", node::property (
      node::reference {node::id (script)}));
    tree source (DOCUMENT, script);
    node::set (source, metadata);
    const tree original= copy (source);
    const tree converted= convert_OTS1_symbols_to_universal_encoding (source);
    QCOMPARE (node::id (converted), node::id (source));
    QVERIFY (converted[0] == script);
    const auto& properties= node::get (converted)->properties;
    const auto& nested= std::get<node::property::dictionary> (
      properties.at ("test:rich").data);
    const tree rich= std::get<node::rich_text> (
      std::get<node::property::list> (nested.at ("nested").data)[0].data).content;
    QVERIFY (is_atomic (rich));
    QCOMPARE (rich->label, string ("<copyright>"));
    QCOMPARE (node::id (rich), node::id (symbol));
    QVERIFY (node::equal (properties.at ("test:reference"),
                          metadata.properties.at ("test:reference")));
    QVERIFY (source == original);
  }

  void rowTagLinksAssetsAndNestedRichValues () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    GenerationContext context;
    context.root= fs::path (temporary.path ().toStdString ()) / "vault";
    context.destination= fs::path (temporary.path ().toStdString ()) / "site";
    fs::create_directories (context.root / "docs");
    fs::create_directories (context.root / "images");
    QVERIFY (write_file_bytes (context.root / "images/tag.png", "fixture image bytes"));
    context.selected_files.insert ("other.ath");
    tree link (HLINK, "Tag link", "../other.ath");
    identify (link); identify (link[1]);
    tree image (IMAGE, "../images/tag.png", "1cm", "1cm", "", "");
    identify (image); identify (image[0]);
    tree row (ROW, tree (CELL, "x"));
    identify (row);
    math::set_row_numbering (row, math::row_numbering::unnumbered);
    math::set_row_tag (row, tree (CONCAT, link, image));
    auto metadata= *node::get (row);
    tree extra (HLINK, "Nested", "../other.ath");
    identify (extra);
    metadata.properties.emplace ("test:nested", node::property (
      node::property::list {node::property (node::property::dictionary {
        {"rich", node::property (node::rich_text {extra})}})}));
    node::set (row, metadata);
    const tree original= copy (row);
    const tree rewritten= rewrite_static_links (row, "docs/source.ath",
                                                "docs/source.ath", context);
    QCOMPARE (node::id (rewritten), node::id (row));
    QCOMPARE (math::get_row_numbering (rewritten), math::row_numbering::unnumbered);
    const auto tag= math::get_row_tag (rewritten);
    QVERIFY (tag.has_value ());
    QCOMPARE (node::id ((*tag)[0]), node::id (link));
    QCOMPARE (node::id ((*tag)[0][1]), node::id (link[1]));
    QVERIFY ((*tag)[0][1] != link[1]);
    QCOMPARE (node::id ((*tag)[1]), node::id (image));
    QCOMPARE (node::id ((*tag)[1][0]), node::id (image[0]));
    QVERIFY (fs::exists (context.destination / "images/tag.png"));
    const auto& list= std::get<node::property::list> (
      node::get (rewritten)->properties.at ("test:nested").data);
    const auto& dictionary= std::get<node::property::dictionary> (list[0].data);
    const tree rich= std::get<node::rich_text> (dictionary.at ("rich").data).content;
    QCOMPARE (node::id (rich), node::id (extra));
    QVERIFY (rich[1] != extra[1]);
    QVERIFY (row == original);
    math::set_row_tag (row, "");
    const auto empty= math::get_row_tag (
      rewrite_static_links (row, "docs/source.ath", "docs/source.ath", context));
    QVERIFY (empty.has_value ());
    QCOMPARE (*empty, tree (""));
  }

  void nativeEnunciationIsNotSynthesizedAsLegacy () {
    tree source= compound ("enunciation", tree (DOCUMENT, "Body"));
    node::metadata metadata;
    metadata.id= node::new_id ();
    metadata.properties.emplace ("kind", node::property (std::string ("definition")));
    metadata.properties.emplace ("numbered", node::property (true));
    metadata.properties.emplace ("name", node::property (node::rich_text {tree ("Name")}));
    node::set (source, metadata);
    GenerationContext context;
    QVERIFY (rewrite_static_links (source, "source.ath", "source.ath", context) == source);
  }

  void snapshotsKeepActualVersionEvenWithoutMetadata () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    for (const auto version: {doc::xml_format_version::v2, doc::xml_format_version::v3}) {
      const fs::path path= fs::path (temporary.path ().toStdString ()) / "source.ath";
      const tree source= fixture_document ("Plain source");
      const auto bytes= doc::encode_native_xml_bytes (source, version);
      QVERIFY (write_file_bytes (path, bytes));
      MaintenanceDocument snapshot;
      std::string error, output;
      QVERIFY2 (read_maintenance_document (path, snapshot, error), error.c_str ());
      QVERIFY2 (serialize_maintenance_document (snapshot, fixture_document ("Changed snapshot"),
                                                output, error), error.c_str ());
      const auto decoded= doc::decode_native_xml_bytes (output);
      QCOMPARE (decoded.version, version);
      QVERIFY (decoded.document == fixture_document ("Changed snapshot"));
      std::string unchanged;
      QVERIFY (read_file_bytes (path, unchanged));
      QCOMPARE (unchanged, bytes);
    }
  }

  void invalidSourcesNeverProducePages () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path path= fs::path (temporary.path ().toStdString ()) / "invalid.ath";
    QVERIFY (write_file_bytes (path, "<a v=\"3\" k=\"d\"><n"));
    MaintenanceDocument snapshot;
    std::string error;
    QVERIFY (!read_maintenance_document (path, snapshot, error));
    QVERIFY (!error.empty ());
    const auto target= path.parent_path () / "out.html";
    QVERIFY (!export_document_html (snapshot, tree (_ERROR, "bad import"), path,
                                     target, "out.html", "bad", "", error));
    QVERIFY (!fs::exists (target));
  }
};

QTEST_GUILESS_MAIN (NativeV3WebsiteSnapshotTest)
#include "native_v3_website_snapshot_test.moc"
