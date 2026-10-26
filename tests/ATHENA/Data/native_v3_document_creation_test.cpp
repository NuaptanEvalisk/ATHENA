/******************************************************************************
* MODULE     : native_v3_document_creation_test.cpp
* DESCRIPTION: Target-version-aware creation without runtime math migration
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>

#include "ATHENA/Data/source_document_creation.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/namespaces.hpp"
#include "ATHENA/Data/interop_filesystem.hpp"
#include "ATHENA/Data/interop_document_codec.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/vault.hpp"
#include "Data/Tree/math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "buffer_actor.hpp"
#include "tm_buffer.hpp"
#include "convert.hpp"
#include "analyze.hpp"
#include "drd_std.hpp"
#include "scheme.hpp"
#include "ATHENA/server.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "file.hpp"
#include "sys_utils.hpp"

#include <cstdlib>
#include <filesystem>

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
namespace fs= std::filesystem;
using namespace athena::document;

url file_url (const fs::path& file) {
  return url_system (string (file.string ().c_str ()));
}

bool write_bytes (const fs::path& file, const std::string& bytes) {
  QFile output (QString::fromStdString (file.string ()));
  return output.open (QIODevice::WriteOnly | QIODevice::Truncate) &&
    output.write (bytes.data (), qint64 (bytes.size ())) == qint64 (bytes.size ()) &&
    output.flush ();
}

std::string read_bytes (const fs::path& file) {
  QFile input (QString::fromStdString (file.string ()));
  if (!input.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read fixture");
  return input.readAll ().toStdString ();
}

void write_vault (const fs::path& root, int model) {
  fs::create_directories (root);
  AthenaVaultfileInfo info;
  info.node_model_version= model;
  std::string error;
  if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
}

tree document_with (const tree& paragraph) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")),
               compound ("body", tree (DOCUMENT, paragraph)));
}
} // namespace

class TestNativeV3DocumentCreation: public QObject {
  Q_OBJECT
private slots:
  void nativeDrdRoles () {
    namespace model= athena::document_node;
    namespace math= athena::math_v3;
    namespace node= athena::node;
    tree accent (MATH_ACCENT, "^", "o", "x");
    node::metadata accent_metadata;
    accent_metadata.properties.emplace ("math:accent-stretch", node::property (true));
    node::set (accent, accent_metadata);
    tree grid (MATH_GRID, tree (ROW, tree (CELL, "x"), tree (CELL, "y")),
                          tree (ROW, tree (CELL, "z"), tree (CELL, "w")));
    node::metadata grid_metadata;
    grid_metadata.properties.emplace ("math:grid-kind", node::property (std::string ("matrix")));
    node::set (grid, grid_metadata);
    tree block (MATH_BLOCK, grid);
    node::metadata block_metadata;
    block_metadata.properties.emplace ("math:block-numbered", node::property (false));
    node::set (block, block_metadata);
    struct sample { tree source; std::vector<bool> content; };
    const std::vector<sample> samples {
      {math::delimited ("(", ")", 'a', "x"), {false, false, false, false, false, true}},
      {tree (MATH_MIDDLE, "|"), {false}},
      {math::big_operator ("+"), {false, false}},
      {math::scripts ("x", "i", "j", "k", "l"), {true, true, true, true, true}},
      {accent, {false, false, true}},
      {tree (MATH_ANNOTATION, "x", "below", "above"), {true, true, true}},
      {tree (MATH_LONG_ARROW, "=", "below", "above"), {false, true, true}},
      {tree (MATH_LONG_ARROW, tree (MATH_SYMBOL, "long-hookrightarrow"), "", ""),
       {false, true, true}},
      {grid, {true, true}},
      {block, {true}},
      {tree (MATH_VARIANT, "bb", "x"), {false, true}},
      {tree (MATH_SYMBOL, "differential-d"), {false}}
    };
    auto drd= standard_drd_for_thread ();
    for (const auto& sample: samples) {
      const tree& source= sample.source;
      QVERIFY2 (math::valid_native_math (source), as_charp (as_string (L(source))));
      QVERIFY (drd->correct_arity (L(source), N(source)));
      const tree body (DOCUMENT, source);
      const auto& info= drd->contracts.resolve (L(source));
      for (int i=0; i<N(source); ++i) {
        const auto& child= info.fields[info.layout.index (i, N(source))];
        const bool content= sample.content[i];
        const int expected_type= content || L(source) == MATH_LONG_ARROW ?
          TYPE_REGULAR : TYPE_STRING;
        QCOMPARE (int (child.type), expected_type);
        QCOMPARE (child.accessible != ACCESSIBLE_NEVER, content);
        const auto role= model::classify_source_path (
          body, {0, i}, drd, model::standard_source_role);
        QVERIFY (role.ok ());
        QCOMPARE (role.content, content);
      }
      const auto assigned= model::assign_detached_source_ids (
        body, drd, model::standard_source_role,
        [] (const model::identity_request&) { return node::new_id (); });
      QVERIFY (assigned.ok ());
      QVERIFY (node::valid_id (node::id (*assigned.body)));
      for (int i=0; i<N(source); ++i)
        if (!sample.content[i]) QVERIFY ((*assigned.body)[0][i] == source[i]);
      QVERIFY (node::content_equal (*assigned.body, body));
    }
    const tree grid_body (DOCUMENT, grid);
    const auto cell= model::classify_source_path (
      grid_body, {0, 1, 1, 0}, drd, model::standard_source_role);
    QVERIFY (cell.ok () && cell.content);
    const tree arrow_body (DOCUMENT, samples[7].source);
    const auto descriptor= model::classify_source_path (
      arrow_body, {0, 0, 0}, drd, model::standard_source_role);
    QVERIFY (descriptor.ok () && !descriptor.content);
  }

  void targetVersionAuthority () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    QCOMPARE (new_source_version_for_model (0), xml_format_version::v2);
    QCOMPARE (new_source_version_for_model (1), xml_format_version::v2);
    QCOMPARE (new_source_version_for_model (2), xml_format_version::v3);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, new_source_version_for_model (3));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, new_source_version_for_model (-1));
    QCOMPARE (new_source_version_for_target (root / "plain.ath"), xml_format_version::v3);
    QCOMPARE (new_source_version_for_target (root / "template.ath",
      document_source_format::xml_v3), xml_format_version::v3);

    write_vault (root / "v3", 2);
    fs::create_directories (root / "v3/subdirectory");
    QCOMPARE (new_source_version_for_target (root / "v3/subdirectory/new.ath"),
              xml_format_version::v3);
    write_vault (root / "v3/nested-v2", 1);
    QCOMPARE (new_source_version_for_target (root / "v3/nested-v2/new.ath"),
              xml_format_version::v2);
    fs::create_directory_symlink (root / "v3/nested-v2", root / "alias");
    QCOMPARE (new_source_version_for_target (root / "alias/new.ath"),
              xml_format_version::v2);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_source_version_for_target (root / "v3/nested-v2/new.ath",
                                     document_source_format::xml_v3));
    QVERIFY (write_bytes (root / "v3/nested-v2/Vaultfile.json", "not JSON"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_source_version_for_target (root / "v3/nested-v2/new.ath"));
    write_vault (root / "future", 3);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_source_version_for_target (root / "future/new.ath"));
  }

  void namespacePlainCreation () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    for (int model: {0, 1, 2}) {
      const auto target_root= root / std::to_string (model);
      write_vault (target_root, model);
      const auto target= target_root / "new.ath";
      string error;
      QVERIFY2 (athena_namespace_create_plain_file (file_url (target), error), as_charp (error));
      const auto bytes= read_bytes (target);
      const auto decoded= decode_native_xml_bytes (bytes);
      QCOMPARE (decoded.version, model == 2 ? xml_format_version::v3 : xml_format_version::v2);
      const tree body= extract (decoded.document, "body");
      QVERIFY (is_func (body, DOCUMENT, 1));
      QVERIFY (is_atomic (body[0]) && body[0]->label == "");
      QVERIFY (athena::node::valid_id (athena::node::id (body)));
      QVERIFY (athena::node::valid_id (athena::node::id (body[0])));
      QVERIFY (!athena_namespace_create_plain_file (file_url (target), error));
      QCOMPARE (read_bytes (target), bytes);
    }
  }

  void namespaceTemplatePreservesV3 () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    write_vault (root / "v3", 2);
    write_vault (root / "v2", 1);
    tree script= athena::math_v3::scripts ("x", "", "", "i", "", 'r');
    auto metadata= *athena::node::get (script);
    metadata.id= athena::node::new_id ();
    athena::node::set (script, metadata);
    const auto template_bytes= write_xml_v3 (document_with (compound ("math", script)));
    QVERIFY (write_bytes (root / "template.ath", template_bytes));
    athena_namespace_definition ns;
    ns.initial_content_path= "template.ath";
    const string base (root.string ().c_str ());
    for (const auto target: {root / "v3/created.ath", root / "outside.ath"}) {
      string error;
      QVERIFY2 (athena_namespace_create_file (ns, file_url (target), base, true, error),
                as_charp (error));
      const auto decoded= decode_native_xml_bytes (read_bytes (target));
      QCOMPARE (decoded.version, xml_format_version::v3);
      const tree body= extract (decoded.document, "body");
      QVERIFY (is_compound (body[0], "math", 1));
      QVERIFY (athena::node::content_equal (body[0][0], script));
      QVERIFY (athena::node::valid_id (athena::node::id (body[0][0])));
      QVERIFY (athena::node::id (body[0][0]) != metadata.id);
    }
    string error;
    QVERIFY (!athena_namespace_create_file (ns, file_url (root / "v2/rejected.ath"),
                                            base, true, error));
    QVERIFY (error != "");
    QVERIFY (!fs::exists (root / "v2/rejected.ath"));
    QCOMPARE (read_bytes (root / "template.ath"), template_bytes);
  }

  void namespaceTemplateDoesNotMigrateLegacyMath () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    write_vault (root / "v3", 2);
    const tree old_math (AROUND, "(", "x", ")");
    const auto bytes= write_xml_v2 (document_with (compound ("math", old_math)));
    QVERIFY (write_bytes (root / "template.ath", bytes));
    athena_namespace_definition ns;
    ns.initial_content_path= "template.ath";
    string error;
    QVERIFY (!athena_namespace_create_file (ns, file_url (root / "v3/rejected.ath"),
      string (root.string ().c_str ()), true, error));
    QVERIFY (error != "");
    QVERIFY (!fs::exists (root / "v3/rejected.ath"));
    QCOMPARE (read_bytes (root / "template.ath"), bytes);

    const auto plain= write_xml_v2 (document_with ("plain v2 template"));
    QVERIFY (write_bytes (root / "plain.ath", plain));
    ns.initial_content_path= "plain.ath";
    error= "";
    QVERIFY2 (athena_namespace_create_file (ns, file_url (root / "v3/plain.ath"),
      string (root.string ().c_str ()), true, error), as_charp (error));
    QCOMPARE (decode_native_xml_bytes (read_bytes (root / "v3/plain.ath")).version,
              xml_format_version::v3);
    QCOMPARE (read_bytes (root / "plain.ath"), plain);
  }

  void newScratchDocumentDefaultsToV3 () {
    const url scratch= make_new_source_buffer ();
    auto cleanup= qScopeGuard ([&] { remove_buffer (scratch); });
    auto buffer= concrete_buffer (scratch);
    QVERIFY (!is_nil (buffer));
    actor_command_record snapshot;
    QVERIFY (buffer->actor->invoke (actor_command_kind::snapshot_document,
      ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &snapshot));
    QCOMPARE (snapshot.argument[0], std::uint64_t (3));
    const auto document= actor_tree_registry::instance ().take (snapshot.payload0);
    const tree body= extract (document, "body");
    QVERIFY (is_func (body, DOCUMENT, 1));
    QVERIFY (athena::node::valid_id (athena::node::id (body)));
    QVERIFY (athena::node::valid_id (athena::node::id (body[0])));
  }

  void bufferCreationAndExistingVersion () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    for (int model: {1, 2}) {
      const auto target_root= root / std::to_string (model);
      write_vault (target_root, model);
      const auto target= target_root / "new.ath";
      const url name= file_url (target);
      auto cleanup= qScopeGuard ([&] { remove_buffer (name); });
      QVERIFY (!buffer_create_source (name));
      auto buffer= concrete_buffer (name);
      QVERIFY (!is_nil (buffer));
      actor_command_record snapshot;
      QVERIFY (buffer->actor->invoke (actor_command_kind::snapshot_document,
        ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &snapshot));
      const auto document= actor_tree_registry::instance ().take (snapshot.payload0);
      QCOMPARE (snapshot.argument[0], std::uint64_t (model == 2 ? 3 : 2));
      QVERIFY (is_func (extract (document, "body"), DOCUMENT, 1));
      QVERIFY (!buffer_save (name));
      QCOMPARE (decode_native_xml_bytes (read_bytes (target)).version,
                model == 2 ? xml_format_version::v3 : xml_format_version::v2);
    }

    const auto target= root / "2/existing-v2.ath";
    const auto existing_bytes= write_xml_v2 (document_with ("existing v2"));
    QVERIFY (write_bytes (target, existing_bytes));
    const url name= file_url (target);
    auto cleanup= qScopeGuard ([&] { remove_buffer (name); });
    QVERIFY (buffer_create_source (name));
    QVERIFY (is_nil (concrete_buffer (name)));
    QCOMPARE (read_bytes (target), existing_bytes);
    QVERIFY (!buffer_import (name, name, "texmacs"));
    QVERIFY (buffer_create_source (name));
    QVERIFY (!buffer_save (name));
    QCOMPARE (decode_native_xml_bytes (read_bytes (target)).version, xml_format_version::v2);
  }

  void transientReplacementCarriesVersionAndRejectsLoss () {
    const url name= url_scratch ("versioned_replace_", ".ath");
    QVERIFY (!replace_buffer_tree (name, document_with ("missing"), xml_format_version::v3));
    QVERIFY (is_nil (concrete_buffer (name)));
    QVERIFY (initialize_buffer_tree (name, document_with ("scratch"), xml_format_version::v2));
    auto cleanup= qScopeGuard ([&] { remove_buffer (name); });
    auto snapshot= [&] {
      actor_command_record response;
      if (!concrete_buffer (name)->actor->invoke (actor_command_kind::snapshot_document,
          ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &response))
        throw std::runtime_error ("Cannot snapshot replacement buffer");
      return std::make_pair (response.argument[0],
        actor_tree_registry::instance ().take (response.payload0));
    };
    const tree source= document_with (compound ("math",
      athena::math_v3::scripts ("x", "", "", "i", "2", 'r')));
    QVERIFY (replace_buffer_tree (name, source, xml_format_version::v3));
    const auto before= snapshot ();
    QCOMPARE (before.first, std::uint64_t (3));
    QVERIFY (athena::node::content_equal (extract (before.second, "body"),
                                         extract (source, "body")));
    const tree legacy= document_with (compound ("math", tree (AROUND, "(", "x", ")")));
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      replace_buffer_tree (name, legacy, xml_format_version::v3));
    const auto after= snapshot ();
    QCOMPARE (after.first, before.first);
    QCOMPARE (after.second, before.second);
  }

  void interopCreationUsesTargetModel () {
    try {
    using namespace athena::interop;
    const tree plain= document_with ("plain source without version hints");
    for (int model: {0, 1, 2}) {
      const auto bytes= new_filesystem_document_bytes (document_node_to_value_v3 (plain), model);
      const auto decoded= decode_native_xml_bytes (bytes);
      QCOMPARE (decoded.version, model == 2 ? xml_format_version::v3 : xml_format_version::v2);
      QVERIFY (athena::node::valid_id (athena::node::id (extract (decoded.document, "body"))));
    }
    const tree native= document_with (compound ("math",
      athena::math_v3::delimited ("(", ")", 'a', "x")));
    const auto decoded= decode_native_xml_bytes (
      new_filesystem_document_bytes (document_node_to_value_v3 (native), 2));
    QCOMPARE (decoded.version, xml_format_version::v3);
    QVERIFY (athena::node::content_equal (extract (decoded.document, "body"),
                                         extract (native, "body")));
    const tree legacy= document_with (compound ("math", tree (AROUND, "(", "x", ")")));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_filesystem_document_bytes (document_node_to_value_v3 (legacy), 2));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_filesystem_document_bytes (document_node_to_value_v3 (plain), 3));
    tree identified= document_with ("existing source identity");
    athena::node::metadata identity;
    identity.id= athena::node::new_id ();
    athena::node::set (identified[1][0], identity);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_filesystem_document_bytes (document_node_to_value_v3 (identified), 2));
  }
    catch (const std::exception& error) { QFAIL (error.what ()); }
  }

  void interopTypedPropertiesAreNotSourceIdentities () {
    try {
    using namespace athena::interop;
    using athena::node::property;
    const tree script= athena::math_v3::scripts ("x", "", "", "i", "", 'r');
    const tree source= document_with (compound ("math", script));
    const auto encoded= document_node_to_value_v3 (source);
    const auto decoded= decode_native_xml_bytes (new_filesystem_document_bytes (encoded, 2));
    QCOMPARE (decoded.version, xml_format_version::v3);
    const tree body= extract (decoded.document, "body");
    QVERIFY (athena::node::content_equal (body[0][0], script));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, new_filesystem_document_bytes (encoded, 1));

    tree rich ("nested identity");
    athena::node::metadata identity;
    identity.id= athena::node::new_id ();
    athena::node::set (rich, identity);
    tree identified= document_with ("new paragraph");
    athena::node::metadata metadata;
    metadata.properties.emplace ("test:rich", property (property::list {
      property (property::dictionary {{"nested", property (athena::node::rich_text {rich})}})}));
    athena::node::set (identified[1][0][0], metadata);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_filesystem_document_bytes (document_node_to_value_v3 (identified), 2));

    metadata.properties.clear ();
    metadata.properties.emplace ("athena:artifact-bindings", property (property::dictionary {
      {"test-role", property (identity.id)}}));
    athena::node::set (identified[1][0][0], metadata);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      new_filesystem_document_bytes (document_node_to_value_v3 (identified), 2));
  }
    catch (const std::exception& error) { QFAIL (error.what ()); }
  }

  void unpublishedCandidateGateStaysClosed () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    write_vault (root, 2);
    fs::create_directories (root / ".athena");
    const auto marker= root / ".athena/xml-v3-candidate-manifest.json";
    QVERIFY (write_bytes (marker, "{}"));
    const auto before= read_bytes (root / "Vaultfile.json");
    const auto active= vault_capture_context ();
    QVERIFY (vault_load (file_url (root), "v3 fixture", "map.sqlite", "ns.sqlite") != "");
    QVERIFY (vault_capture_context () == active);
    QCOMPARE (read_bytes (root / "Vaultfile.json"), before);
    QVERIFY (!fs::exists (root / "map.sqlite"));
    QVERIFY (!fs::exists (root / "ns.sqlite"));
    fs::remove (marker);
    fs::create_symlink (root / "missing-receipt", marker);
    QVERIFY (vault_load (file_url (root), "v3 fixture", "map.sqlite", "ns.sqlite") != "");
    QVERIFY (vault_capture_context () == active);
    QVERIFY (!fs::exists (root / "ns.sqlite"));
  }

  void localV3VaultOpensButFutureModelsDoNot () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    auto cleanup= qScopeGuard ([] { vault_close (); });
    write_vault (root, 3);
    QVERIFY (vault_load (file_url (root), "future fixture", "map.sqlite", "ns.sqlite") != "");
    QVERIFY (!fs::exists (root / "ns.sqlite"));
    write_vault (root, 2);
    QCOMPARE (vault_load (file_url (root), "v3 fixture", "map.sqlite", "ns.sqlite"), string (""));
    QVERIFY (vault_capture_context ());
    QCOMPARE (vault_capture_context ()->node_model_version, 2);
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server runtime;
    TestNativeV3DocumentCreation test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  qputenv ("GUILE_AUTO_COMPILE", "0");
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication application (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid () || !QDir (profile.path ()).mkpath ("fonts")) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}
#include "native_v3_document_creation_test.moc"
