/******************************************************************************
* MODULE     : native_v3_maintenance_test.cpp
* DESCRIPTION: Format-preserving maintenance transforms and disk publication
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/evaluation_bars.hpp"
#include "ATHENA/Data/person_names.hpp"
#include "ATHENA/Data/redundant_wikilinks.hpp"
#include "ATHENA/Data/vault_file_references.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;
namespace node= athena::node;
namespace math= athena::math_v3;
using namespace athena::document;

namespace {
tree identified (tree value) {
  node::metadata metadata;
  if (const auto* old= node::get (value)) metadata= *old;
  metadata.id= node::new_id ();
  node::set (value, metadata);
  return value;
}

void property (tree& value, const char* key, node::property item) {
  node::metadata metadata;
  if (const auto* old= node::get (value)) metadata= *old;
  metadata.properties[key]= std::move (item);
  node::set (value, metadata);
}

tree math_fixture (xml_format_version version) {
  if (version != xml_format_version::v3)
    return compound ("math", tree (CONCAT,
      tree (WIDE, "u", tree (CONCAT, tree (NAMED_SYMBOL, "texmacs:bar"))),
      tree (RSUB, "i"), tree (AROUND, "(", "x", ")"),
      tree (TFORMAT, tree (CWITH, "1", "1", "1", "1", "cell-halign", "l"),
        tree (TABLE, tree (ROW, tree (CELL, "y"))))));
  tree scripts= identified (math::scripts (
    identified (math::big_operator ("+")), "a", "b", "c", "d", 'o'));
  tree accent= identified (tree (MATH_ACCENT, "^", "o", scripts));
  property (accent, "math:accent-stretch", node::property (true));
  tree delimited= identified (math::delimited (
    "(", ")", 'm', tree (MATH_VARIANT, "bf", accent), "2", "4"));
  tree cell= identified (tree (CELL, delimited));
  property (cell, "math:cell-format", node::property (node::property::dictionary {
    {"align", node::property (std::string ("r"))},
    {"border-bottom", node::property (std::string ("1ln"))}}));
  tree row= identified (tree (ROW, cell));
  math::set_row_numbering (row, math::row_numbering::unnumbered);
  math::set_row_tag (row, identified (tree (CONCAT, "T", compound ("math", "i"))));
  tree grid= identified (tree (MATH_GRID, row));
  property (grid, "math:grid-kind", node::property (std::string ("align")));
  tree block= identified (tree (MATH_BLOCK, grid));
  property (block, "math:block-numbered", node::property (true));
  return block;
}

tree source_document (tree body) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")), compound ("body", body));
}

tree source_fixture (xml_format_version version) {
  tree image= identified (tree (IMAGE, identified (tree ("plot.png")), "1cm", "1cm", "", ""));
  tree prose= identified (tree ("Zyxwvu Exampleperson and Zyxwvu Exampleperson"));
  property (prose, "fixture:rich", node::property (node::rich_text {
    identified (compound ("strong", "untouched title"))}));
  tree body= identified (tree (DOCUMENT, image, prose, math_fixture (version)));
  property (body, "fixture:typed", node::property (node::property::dictionary {
    {"flag", node::property (true)}, {"integer", node::property (std::int64_t (17))},
    {"real", node::property (1.25)},
    {"reference", node::property (node::reference {node::id (prose)})},
    {"list", node::property (node::property::list {
      node::property (std::string ("opaque plot.png")),
      node::property (node::rich_text {identified (tree (IMAGE, "plot.png", "", "", "", ""))})})}}));
  return identified (source_document (body));
}

void save (const fs::path& path, const std::string& bytes) {
  if (!write_file_bytes (path, bytes)) throw std::runtime_error ("Cannot write maintenance fixture");
}

std::string load (const fs::path& path) {
  std::string result;
  if (!read_file_bytes (path, result)) throw std::runtime_error ("Cannot read maintenance fixture");
  return result;
}

void vault (const fs::path& root, int model= 1) {
  AthenaVaultfileInfo info;
  info.node_model_version= model;
  std::string error;
  if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
}

bool contains (const tree& value, const string& text) {
  if (is_atomic (value)) return value->label == text;
  for (int i=0; i<N(value); ++i) if (contains (value[i], text)) return true;
  return false;
}
}

class NativeV3MaintenanceTest: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void transformsPreserveMetadataAndMath () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    save (root / "plot.png", "image");
    for (auto version: {xml_format_version::v2, xml_format_version::v3}) {
      const tree source= source_fixture (version);
      const tree before= copy (source);
      size_t changed= 0;
      const tree rewritten= athena_vault_rewrite_file_references (
        source, root / "sample.ath", root / "sample.ath",
        root / "plot.png", root / "renamed.png", changed);
      QCOMPARE (changed, size_t (2));
      const tree body= extract (rewritten, "body");
      const tree old= extract (source, "body");
      QVERIFY (body[2] == old[2]);
      QVERIFY (body[1] == old[1]);
      QVERIFY (node::equal_metadata (body[0], old[0]));
      QVERIFY (node::equal_metadata (body[0][0], old[0][0]));
      QVERIFY (body[0][0]->label == "renamed.png");
      const auto& dictionary= std::get<node::property::dictionary> (
        node::get (body)->properties.at ("fixture:typed").data);
      const auto& list= std::get<node::property::list> (dictionary.at ("list").data);
      QCOMPARE (std::get<std::string> (list[0].data), std::string ("opaque plot.png"));
      const tree rich= std::get<node::rich_text> (list[1].data).content;
      QVERIFY (rich[0] == "renamed.png");
      QVERIFY (source == before);
      QVERIFY (decode_native_xml_bytes (encode_native_xml_bytes (rewritten, version)).document == rewritten);

      int wrapped= 0;
      const tree normalized= athena_normalize_person_names (
        old, wrapped, std::vector<string> {"Zyxwvu Exampleperson"});
      QCOMPARE (wrapped, 2);
      QVERIFY (normalized[2] == old[2]);
      QVERIFY (node::equal_metadata (normalized, old));
      QVERIFY (node::equal_metadata (normalized[1], old[1]));
      QVERIFY (is_func (normalized[1], CONCAT, 3));
      QVERIFY (normalized[1][0] == compound ("person", "Zyxwvu Exampleperson"));
      QVERIFY (normalized[1][2] == compound ("person", "Zyxwvu Exampleperson"));
      QVERIFY (source == before);
    }
  }

  void retainsExplicitFormatsAndNoopBytes () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path file= fs::path (temporary.path ().toStdString ()) / "source.ath";
    const tree plain= source_document (tree (DOCUMENT, "before"));
    for (int format= 0; format<5; ++format) {
      std::string bytes;
      if (format < 3)
        bytes= encode_native_xml_bytes (plain, format == 0 ? xml_format_version::v1 :
          format == 1 ? xml_format_version::v2 : xml_format_version::v3) + "\n\n";
      else bytes= tm_to_std (format == 3 ? tree_to_texmacs (plain) : tree_to_scheme_document (plain));
      save (file, bytes);
      MaintenanceDocument source;
      std::string error, encoded;
      QVERIFY2 (read_maintenance_document (file, source, error), error.c_str ());
      const auto original_format= source.format;
      QVERIFY2 (serialize_maintenance_document (source, source.document, encoded, error), error.c_str ());
      QCOMPARE (encoded, bytes);
      const auto revision= *source.revision;
      QVERIFY2 (commit_maintenance_document (source, encoded, error), error.c_str ());
      MaintenanceDocument noop;
      QVERIFY2 (read_maintenance_document (file, noop, error), error.c_str ());
      QVERIFY (athena::filesystem::same_revision (*noop.revision, revision));
      tree changed= copy (source.document);
      for (int i=0; i<N(changed); ++i)
        if (is_compound (changed[i], "body", 1)) changed[i][0][0]= "after";
      QVERIFY2 (serialize_maintenance_document (source, changed, encoded, error), error.c_str ());
      QVERIFY2 (commit_maintenance_document (source, encoded, error), error.c_str ());
      MaintenanceDocument after;
      QVERIFY2 (read_maintenance_document (file, after, error), error.c_str ());
      QCOMPARE (after.format, original_format);
      QVERIFY (after.document == changed);
    }
  }

  void wikilinkRewriteRetainsIdentifiedWrappersAndMath () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path file= fs::path (temporary.path ().toStdString ()) / "Source.ath";
    for (auto version: {xml_format_version::v2, xml_format_version::v3}) {
      const tree link= compound ("hlink", "Vector spaces", "tmfs://wikilink/block-target/hint/anchor");
      const tree retained= identified (copy (link));
      const tree source= source_document (identified (tree (DOCUMENT, link, retained, math_fixture (version))));
      const tree target= source_document (tree (DOCUMENT,
        compound ("label", "definition:Vector space {"),
        compound ("definition", tree (CONCAT, compound ("strong", "Vector space"), " is defined.")),
        compound ("label", "definition:Vector space }")));
      AthenaArtifactRecord artifact;
      artifact.uuid= "artifact-one";
      artifact.type= "definition";
      artifact.origin= "enunciation";
      artifact.relative_path= "Target.ath";
      artifact.anchor_stem= "definition:Vector space";
      artifact.display_text= "Vector space is defined.";
      artifact.semantic_names= {"Vector space"};
      artifact.keyword_tree= "definition";
      std::vector<AthenaRedundantWikilinkDocument> documents {
        {"Source.ath", source, 0}, {"Target.ath", target, 0}};
      AthenaRedundantWikilinkStats stats;
      athena_remove_redundant_block_wikilinks (documents,
        {{"block-target", "Target.ath", "", "definition:Vector space {"}}, {artifact}, stats);
      QCOMPARE (stats.links_removed, size_t (1));
      tree expected= copy (source);
      expected[1][0][0]= "Vector spaces";
      QVERIFY (documents[0].document == expected);
      save (file, encode_native_xml_bytes (source, version));
      MaintenanceDocument snapshot;
      std::string error, encoded;
      QVERIFY2 (read_maintenance_document (file, snapshot, error), error.c_str ());
      QVERIFY2 (serialize_maintenance_document (snapshot, documents[0].document, encoded, error), error.c_str ());
      QVERIFY2 (commit_maintenance_document (snapshot, encoded, error), error.c_str ());
      const auto decoded= decode_native_xml_bytes (load (file));
      QCOMPARE (decoded.version, version);
      QVERIFY (decoded.document == expected);
    }
  }

  void assetPassPreservesMixedSources () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    vault (root);
    save (root / "plot.png", "image");
    const tree old= source_fixture (xml_format_version::v2);
    const tree native= source_fixture (xml_format_version::v3);
    save (root / "old.ath", write_xml_v2 (old));
    save (root / "native.ath", write_xml_v3 (native));
    const auto noop= write_xml_v3 (source_document (tree (DOCUMENT, "unchanged"))) + "\n";
    save (root / "noop.ath", noop);
    VaultMaintenanceContext context;
    context.root= root;
    const auto result= vault_maintenance_pass_normalize_assets (context);
    QVERIFY2 (result.ok, result.message.c_str ());
    QCOMPARE (context.summary.asset_reference_updates, size_t (4));
    QVERIFY (!fs::exists (root / "plot.png"));
    QCOMPARE (load (root / "noop.ath"), noop);
    for (const auto& item: std::vector<std::pair<fs::path, tree>> {
        {root / "old.ath", old}, {root / "native.ath", native}}) {
      const auto after= decode_native_xml_bytes (load (item.first));
      const auto expected_version= item.first.filename () == "old.ath" ? xml_format_version::v2 : xml_format_version::v3;
      QCOMPARE (after.version, expected_version);
      const tree body= extract (after.document, "body");
      const fs::path asset= root / tm_to_std (body[0][0]->label);
      QVERIFY (fs::exists (asset));
      size_t changed= 0;
      const tree expected= athena_vault_rewrite_file_references (
        item.second, item.first, item.first, root / "plot.png", asset, changed);
      QVERIFY (after.document == expected);
      QCOMPARE (changed, size_t (2));
    }
    const auto before= load (root / "native.ath");
    QVERIFY (vault_maintenance_pass_normalize_assets (context).ok);
    QCOMPARE (load (root / "native.ath"), before);
  }

  void personPassLearnsNativeNamesAndPreservesMath () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    const auto names= write_xml_v3 (source_document (
      tree (DOCUMENT, compound ("person", "Zyxwvu Exampleperson"))));
    save (root / "names.ath", names);
    for (auto version: {xml_format_version::v2, xml_format_version::v3}) {
      const tree source= source_fixture (version);
      const auto file= root / (version == xml_format_version::v2 ? "old.ath" : "native.ath");
      save (file, encode_native_xml_bytes (source, version));
    }
    const auto old= decode_native_xml_bytes (load (root / "old.ath"));
    const auto native= decode_native_xml_bytes (load (root / "native.ath"));
    VaultMaintenanceContext context;
    context.root= root;
    const auto result= vault_maintenance_pass_normalize_person_names (context);
    QVERIFY2 (result.ok, result.message.c_str ());
    QCOMPARE (context.summary.person_files_changed, size_t (2));
    QCOMPARE (context.summary.person_names_wrapped, size_t (4));
    QCOMPARE (load (root / "names.ath"), names);
    for (const auto& item: std::vector<std::pair<fs::path, native_xml_read_result>> {
        {root / "old.ath", old}, {root / "native.ath", native}}) {
      const auto after= decode_native_xml_bytes (load (item.first));
      QCOMPARE (after.version, item.second.version);
      const tree before_body= extract (item.second.document, "body");
      const tree after_body= extract (after.document, "body");
      QVERIFY (after_body[2] == before_body[2]);
      QVERIFY (node::equal_metadata (after_body, before_body));
      QVERIFY (node::equal_metadata (after_body[1], before_body[1]));
    }
    const auto before= load (root / "native.ath");
    QVERIFY (vault_maintenance_pass_normalize_person_names (context).ok);
    QCOMPARE (load (root / "native.ath"), before);
  }

  void enunciationDiskRewritePreservesVersionAndMath () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path file= fs::path (temporary.path ().toStdString ()) / "source.ath";
    for (auto version: {xml_format_version::v2, xml_format_version::v3}) {
      const tree math= math_fixture (version);
      const tree old= identified (compound ("theorem", identified (tree (DOCUMENT, math))));
      const tree document= source_document (identified (tree (DOCUMENT, old)));
      save (file, encode_native_xml_bytes (document, version));
      MaintenanceDocument source;
      std::string error;
      QVERIFY2 (read_maintenance_document (file, source, error), error.c_str ());
      const auto output= maintenance_enunciation_bytes (source.bytes, false);
      QVERIFY2 (commit_maintenance_document (source, output, error), error.c_str ());
      const auto decoded= decode_native_xml_bytes (load (file));
      QCOMPARE (decoded.version, version);
      const tree enunciation= extract (decoded.document, "body")[0];
      QVERIFY (athena::enunciation::is_canonical (enunciation));
      QCOMPARE (node::id (enunciation), node::id (old));
      QVERIFY (enunciation[0] == old[0]);
      QCOMPARE (maintenance_enunciation_bytes (output, false), output);
    }
    const auto plain= write_xml (source_document (tree (DOCUMENT, "plain"))) + "\n";
    QCOMPARE (maintenance_enunciation_bytes (plain, false), plain);
    const auto unsupported= write_xml (source_document (tree (DOCUMENT, compound ("theorem", "body"))));
    QVERIFY_EXCEPTION_THROWN (maintenance_enunciation_bytes (unsupported, false), codec_exception);
  }

  void evaluationBarsUseNativeNodesAndRetainMetadata () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    for (auto version: {xml_format_version::v2, xml_format_version::v3}) {
      const tree atom= identified (tree ("x|"));
      tree expression= identified (compound ("math", atom));
      const tree body= identified (tree (DOCUMENT, expression, math_fixture (version)));
      const tree document= source_document (body);
      const auto file= root / (version == xml_format_version::v2 ? "old.ath" : "native.ath");
      save (file, encode_native_xml_bytes (document, version));
      int count= 0;
      const tree converted= athena_promote_evaluation_bars (atom, count, "math", version);
      QCOMPARE (count, 1);
      QVERIFY (node::equal_metadata (atom, converted));
      QVERIFY (version == xml_format_version::v3 ? is_func (converted, DELIMITED, 6) :
                                                  is_func (converted, VAR_AROUND, 3));
      VaultMaintenanceContext context;
      context.root= root;
      const auto result= vault_maintenance_pass_promote_evaluation_bars (context);
      QVERIFY2 (result.ok, result.message.c_str ());
      const auto after= decode_native_xml_bytes (load (file));
      QCOMPARE (after.version, version);
      tree expected= copy (document);
      expected[1][0][0][0]= converted;
      QVERIFY (after.document == expected);
      const auto bytes= load (file);
      QVERIFY (vault_maintenance_pass_promote_evaluation_bars (context).ok);
      QCOMPARE (load (file), bytes);
    }
  }

  void malformedAndUnsupportedInputsPreventWrites () {
    for (auto pass: {vault_maintenance_pass_normalize_assets,
                    vault_maintenance_pass_normalize_person_names,
                    vault_maintenance_pass_promote_evaluation_bars}) {
      QTemporaryDir temporary;
      QVERIFY (temporary.isValid ());
      const fs::path root (temporary.path ().toStdString ());
      vault (root);
      save (root / "plot.png", "image");
      tree body (DOCUMENT, tree (IMAGE, "plot.png", "", "", "", ""),
                 "Emmy Noether", compound ("math", "x|"));
      const auto bytes= write_xml_v3 (source_document (body));
      save (root / "a-valid.ath", bytes);
      save (root / "z-malformed.ath", "<a v=\"3\" k=\"d\"><n");
      VaultMaintenanceContext context;
      context.root= root;
      const auto result= pass (context);
      QVERIFY (!result.ok);
      QVERIFY (!result.message.empty ());
      QCOMPARE (load (root / "a-valid.ath"), bytes);
      QCOMPARE (load (root / "plot.png"), std::string ("image"));
    }
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    const auto good= write_xml_v3 (source_document (tree (DOCUMENT, compound ("math", "x|"))));
    const auto bad= write_xml_v3 (source_document (tree (DOCUMENT, compound ("math",
      tree (CONCAT, "x", math::scripts ("|", "", "", "0", "", 'r'))))));
    save (root / "a-good.ath", good);
    save (root / "z-scripted.ath", bad);
    VaultMaintenanceContext context;
    context.root= root;
    const auto result= vault_maintenance_pass_promote_evaluation_bars (context);
    QVERIFY (!result.ok);
    QVERIFY (result.message.find ("scripted native bar") != std::string::npos);
    QCOMPARE (load (root / "a-good.ath"), good);
    QCOMPARE (load (root / "z-scripted.ath"), bad);
  }

  void attributedEvaluationBarsKeepOneOwnerPerUuid () {
    const tree atom= identified (tree ("|"));
    const tree expression= identified (tree (FRAC, "x", "y"));
    const tree bar= identified (tree ("|"));
    tree sequence= identified (tree (CONCAT, expression, bar));
    ::property (sequence, "fixture:rich", node::property (node::rich_text {
      identified (compound ("strong", "span metadata"))}));
    for (const tree& source: {atom, sequence}) {
      const tree before= copy (source);
      int count= 0;
      const tree converted= athena_promote_evaluation_bars (
        source, count, "math", xml_format_version::v3);
      QCOMPARE (count, 1);
      QVERIFY (source == before);
      tree expected;
      if (is_atomic (source)) {
        expected= math::delimited ("", "|", 'a', "");
        node::copy_metadata (source, expected);
        QVERIFY (is_func (converted, DELIMITED, 6));
        QCOMPARE (node::id (converted), node::id (source));
      }
      else {
        tree delimiter= math::delimited ("", "|", 'a', expression);
        node::copy_metadata (bar, delimiter);
        expected= tree (CONCAT, delimiter);
        node::copy_metadata (source, expected);
        QVERIFY (is_func (converted, CONCAT, 1));
        QVERIFY (is_func (converted[0], DELIMITED, 6));
        QCOMPARE (node::id (converted), node::id (source));
        QCOMPARE (node::id (converted[0]), node::id (bar));
        QVERIFY (node::id (converted) != node::id (converted[0]));
        QVERIFY (converted[0][5] == expression);
      }
      QVERIFY (converted == expected);
      // Both strict codecs reject duplicate UUIDs, including rich properties.
      const auto bytes= write_xml_v3 (converted, xml_kind::fragment);
      QVERIFY (read_xml_v3 (bytes, xml_kind::fragment) == converted);
      count= 0;
      QVERIFY (athena_promote_evaluation_bars (
        converted, count, "math", xml_format_version::v3) == converted);
      QCOMPARE (count, 0);
    }

    const tree unsupported= identified (tree (CONCAT, "x",
      identified (math::scripts (identified (tree ("|")), "", "", "0", "", 'r'))));
    const tree before= copy (unsupported);
    int count= 0;
    QVERIFY_EXCEPTION_THROWN (athena_promote_evaluation_bars (
      unsupported, count, "math", xml_format_version::v3), std::runtime_error);
    QVERIFY (unsupported == before);
  }

  void staleSnapshotsCannotPublish () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path file= fs::path (temporary.path ().toStdString ()) / "source.ath";
    const auto original= write_xml_v3 (source_document (tree (DOCUMENT, "original")));
    const auto concurrent= write_xml_v3 (source_document (tree (DOCUMENT, "concurrent")));
    for (bool replace_inode: {false, true}) {
      save (file, original);
      MaintenanceDocument source;
      std::string error;
      QVERIFY2 (read_maintenance_document (file, source, error), error.c_str ());
      if (replace_inode) { fs::remove (file); save (file, original); }
      else save (file, concurrent);
      const auto current= load (file);
      QVERIFY (!commit_maintenance_document (source, concurrent, error));
      QVERIFY (!error.empty ());
      QCOMPARE (load (file), current);
    }
  }

  void directoryReplacementAndSymlinksCannotPublish () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    const auto original= write_xml_v3 (source_document (tree (DOCUMENT, "original")));
    const auto replacement= write_xml_v3 (source_document (tree (DOCUMENT, "replacement")));
    for (int scenario= 0; scenario<3; ++scenario) {
      const auto parent= root / ("parent-" + std::to_string (scenario));
      const auto moved= root / ("moved-" + std::to_string (scenario));
      fs::create_directory (parent);
      const auto file= parent / "source.ath";
      save (file, original);
      MaintenanceDocument snapshot;
      std::string error;
      QVERIFY2 (read_maintenance_document (file, snapshot, error), error.c_str ());
      fs::path protected_file;
      if (scenario < 2) {
        fs::rename (parent, moved);
        protected_file= moved / "source.ath";
        if (scenario == 0) {
          fs::create_directory (parent);
          // Keep the captured file inode, but replace its containing directory.
          fs::rename (protected_file, file);
          protected_file= file;
        }
        else fs::create_directory_symlink (moved, parent);
      }
      else {
        protected_file= root / "outside.ath";
        save (protected_file, original);
        fs::remove (file);
        fs::create_symlink (protected_file, file);
      }
      QVERIFY (!maintenance_document_current (snapshot, error));
      QVERIFY (!commit_maintenance_document (snapshot, replacement, error));
      QVERIFY (!error.empty ());
      QCOMPARE (load (protected_file), original);
    }
  }

  void snapshotsUseBoundedDescriptors () {
#if defined(__linux__) || defined(__APPLE__)
    if (qgetenv ("ATHENA_TEST_MAINTENANCE_FD_LIMIT") != "1") {
      QProcess child;
      auto environment= QProcessEnvironment::systemEnvironment ();
      environment.insert ("ATHENA_TEST_MAINTENANCE_FD_LIMIT", "1");
      child.setProcessEnvironment (environment);
      child.start (QCoreApplication::applicationFilePath (), {"snapshotsUseBoundedDescriptors"});
      QVERIFY2 (child.waitForStarted (), qPrintable (child.errorString ()));
      QVERIFY2 (child.waitForFinished (120000), qPrintable (child.errorString ()));
      const QByteArray output= child.readAllStandardOutput () + child.readAllStandardError ();
      QVERIFY2 (child.exitStatus () == QProcess::NormalExit && child.exitCode () == 0,
                output.constData ());
      return;
    }
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    const auto original= write_xml_v3 (source_document (tree (DOCUMENT, "original")));
    const auto replacement= write_xml_v3 (source_document (tree (DOCUMENT, "replacement")));
    struct rlimit previous;
    QVERIFY (::getrlimit (RLIMIT_NOFILE, &previous) == 0);
    struct rlimit limited= previous;
    if (limited.rlim_cur > 64) limited.rlim_cur= 64;
    QVERIFY (::setrlimit (RLIMIT_NOFILE, &limited) == 0);
    auto restore= qScopeGuard ([&] { (void) ::setrlimit (RLIMIT_NOFILE, &previous); });
    std::vector<MaintenanceDocument> snapshots (832);
    std::string error;
    for (size_t i=0; i<snapshots.size (); ++i) {
      const auto file= root / (std::to_string (i) + ".ath");
      save (file, original);
      QVERIFY2 (read_maintenance_document (file, snapshots[i], error), error.c_str ());
    }
    for (const auto& snapshot: snapshots)
      QVERIFY2 (maintenance_document_current (snapshot, error), error.c_str ());
    for (size_t index: {size_t (0), size_t (416), size_t (831)}) {
      QVERIFY2 (commit_maintenance_document (snapshots[index], replacement, error), error.c_str ());
      QCOMPARE (load (snapshots[index].path), replacement);
      QVERIFY (!maintenance_document_current (snapshots[index], error));
    }
#else
    QSKIP ("RLIMIT_NOFILE is unavailable on this platform");
#endif
  }

  void summariesChooseTargetVersion () {
    for (int model: {0, 1, 2}) {
      QTemporaryDir temporary;
      QVERIFY (temporary.isValid ());
      const fs::path root (temporary.path ().toStdString ());
      vault (root, model);
      VaultMaintenanceContext context;
      context.root= root;
      context.vault_name= "Literal <tag>| & \\ UTF-8 \xce\xb1";
      context.summary.generate_summary_page= true;
      context.summary.summary_dir= "maintenance";
      context.warnings.push_back ("Warning <&>| \xce\xb2");
      QVERIFY (vault_maintenance_write_summary_page (context, true));
      const auto summary= decode_native_xml_bytes (load (context.summary.summary_file));
      QCOMPARE (summary.version, model == 2 ? xml_format_version::v3 : xml_format_version::v2);
      QVERIFY (contains (summary.document, std_to_tm (context.vault_name)));
      QVERIFY (contains (summary.document, std_to_tm (context.warnings[0])));
      QVERIFY (contains (summary.document, "Vault Maintenance Summary"));
    }
  }
};

int main (int argc, char** argv) {
  QTemporaryDir profile;
  if (!profile.isValid () || !QDir (profile.path ()).mkpath ("fonts")) return 1;
  qputenv ("HOME", profile.path ().toUtf8 ());
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  qputenv ("XDG_CONFIG_HOME", profile.filePath ("config").toUtf8 ());
  qputenv ("XDG_CACHE_HOME", profile.filePath ("cache").toUtf8 ());
  qputenv ("XDG_DATA_HOME", profile.filePath ("data").toUtf8 ());
  QApplication app (argc, argv);
  NativeV3MaintenanceTest test;
  return QTest::qExec (&test, argc, argv);
}
#include "native_v3_maintenance_test.moc"
