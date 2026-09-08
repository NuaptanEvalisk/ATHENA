/******************************************************************************
* MODULE     : structured_radioactive_test.cpp
* DESCRIPTION: Native mixed mathematical names retain clickable source boxes
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <fstream>
#include "boot.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "typesetter.hpp"
#include "convert.hpp"
#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/artifact_document.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static int link_count (box b, const string& destination) {
  int result= 0;
  tree description= (tree) b;
  if (is_tuple (description) && N(description) > 0 && description[0] == "direct-link") {
    rectangles regions;
    tree action= b->message ("select", (b->x1+b->x2)/2, (b->y1+b->y2)/2, regions);
    if (action == tree (TUPLE, "direct-link", destination)) ++result;
  }
  for (int i=0; i<N(b); ++i) result += link_count (b[i], destination);
  return result;
}

class StructuredRadioactiveTest: public QObject {
  Q_OBJECT
private slots:
  void resolvesFullAndIncludedNames () {
    auto record= [] (const char* id, std::vector<std::string> names) {
      AthenaArtifactRecord r;
      r.uuid= id;
      r.type= "definition";
      r.relative_path= "test.ath";
      r.semantic_names= std::move (names);
      return r;
    };
    auto group= record ("1", {"group", "finite groups"});
    auto subgroup= record ("2", {"subgroups", "normal subgroup"});
    auto theorem= record ("3", {"Euler theorem", "Eulerian theorem"});
    auto general= record ("4", {"generalized Euler's theorems"});
    auto excluded= record ("5", {"groups"});
    excluded.type= "completion";
    auto math= record ("6", {"sigma-algebra"});
    tree sigma= compound ("math", "<sigma>");
    tree name (CONCAT, sigma, "-algebra");
    string serialized= tree_to_texmacs (name);
    math.semantic_name_trees= {std::string (as_charp (serialized), N(serialized))};
    AthenaArtifactRadioactiveMatcher matcher ({group, subgroup, theorem, general, excluded, math});
    auto result= matcher.resolve (tree ("GROUPS"));
    QCOMPARE (result.exact.size (), size_t (1));
    QCOMPARE (result.exact[0].uuid, std::string ("1"));
    QCOMPARE (result.partial.size (), size_t (1));
    QCOMPARE (result.partial[0].uuid, std::string ("2"));
    AthenaArtifactRadioactiveMatcher filtered ({group, subgroup},
      athena_artifact_title_filter_from_entries ({"subgroups", "normal subgroup"}));
    QVERIFY (filtered.resolve (tree ("group")).partial.empty ());
    result= matcher.resolve (tree ("Eulerian theorem"));
    QCOMPARE (result.exact.size (), size_t (1));
    QCOMPARE (result.partial.size (), size_t (1));
    QCOMPARE (result.partial[0].uuid, std::string ("4"));
    result= matcher.resolve (tree ("theorem"));
    QVERIFY (result.exact.empty ());
    QCOMPARE (result.partial.size (), size_t (2));
    QVERIFY (matcher.resolve (tree ("unrelated")).partial.empty ());
    QVERIFY (matcher.resolve (tree ("")).partial.empty ());
    QCOMPARE (matcher.resolve (name).exact.size (), size_t (1));
    QCOMPARE (matcher.resolve (sigma).partial.size (), size_t (1));
    QVERIFY (matcher.resolve (tree ("sigma")).partial.empty ());
    QVERIFY (matcher.resolve (compound ("math", "<tau>")).partial.empty ());
    auto matches= matcher.matches ("group");
    QCOMPARE (matches.size (), size_t (1));
    QCOMPARE (athena_artifact_radioactive_destination (matches[0]),
              "tmfs://artifact-disambiguation/" + matches[0].disambiguation_key);
    QVERIFY (athena_artifact_radioactive_destination (AthenaArtifactRadioactiveMatch ()).empty ());
    string page= tree_to_texmacs (athena_artifact_disambiguation_document (result, "Pagella"));
    std::string document (as_charp (page), N(page));
    QVERIFY (document.find ("No full matches.") != std::string::npos);
    QVERIFY (document.find ("Partial matches") != std::string::npos);
    QVERIFY (document.find ("tmfs://artifact/3") != std::string::npos);
    QVERIFY (document.find ("tmfs://artifact/4") != std::string::npos);
    string query_url= athena_artifact_name_query_url (name);
    QByteArray payload (as_charp (query_url), N(query_url));
    payload= QByteArray::fromHex (payload.mid (QByteArray ("tmfs://artifact-name-query/").size ()));
    tree roundtrip= texmacs_to_tree (string (payload.constData (), payload.size ()));
    QCOMPARE (matcher.resolve (roundtrip).exact.size (), size_t (1));
    std::string oversized (8193, 'x');
    QCOMPARE (athena_artifact_name_query_url (tree (string (oversized.c_str ()))), string (""));
  }

  void typesetsMixedNameWithoutChangingGeometry () {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    std::filesystem::path root (directory.path ().toStdString ());
    AthenaVaultfileInfo info;
    std::string error;
    QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
    tree sigma= compound ("math", "<sigma>");
    tree name (CONCAT, sigma, "-algebra");
    tree body (DOCUMENT, compound ("definition",
      tree (CONCAT, compound ("strong", name), " is a family of subsets.")));
    tree document (DOCUMENT);
    document << compound ("TeXmacs", "2.1.4") << compound ("style", "generic")
             << compound ("body", body);
    string bytes= tree_to_texmacs (document);
    std::ofstream source (root / "name.ath");
    source.write (as_charp (bytes), N(bytes));
    source.close ();
    AthenaArtifactsBuildResult built;
    QVERIFY2 (athena_artifacts_build (root, {}, true, {}, built, error), error.c_str ());
    std::vector<AthenaArtifactRecord> records;
    QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
    QCOMPARE (records.size (), size_t (1));
    string destination= "tmfs://artifact-disambiguation/" *
      string (athena_artifact_radioactive_key (records[0]).c_str ());
    QCOMPARE (vault_load (url_system (root.string ().c_str ()), "test", "vault.sqlite"), string (""));
    struct CloseVault { ~CloseVault () { vault_close (); } } close;
    AthenaArtifactNameResolution resolved;
    QVERIFY (athena_artifact_resolve_name_key (athena_artifact_radioactive_key (records[0]), resolved));
    QCOMPARE (resolved.exact.size (), size_t (1));
    string page= tree_to_texmacs (athena_artifact_disambiguation_page (
      string (athena_artifact_radioactive_key (records[0]).c_str ())));
    QVERIFY (std::string (as_charp (page), N(page)).find ("tmfs://artifact/" + records[0].uuid) != std::string::npos);
    string query_url= athena_artifact_name_query_url (name);
    string prefix= "tmfs://artifact-name-query/";
    page= tree_to_texmacs (athena_artifact_name_query_page (query_url (N(prefix), N(query_url))));
    QVERIFY (std::string (as_charp (page), N(page)).find ("tmfs://artifact/" + records[0].uuid) != std::string::npos);
    page= tree_to_texmacs (athena_artifact_name_query_page ("zz"));
    QVERIFY (std::string (as_charp (page), N(page)).find ("query is invalid") != std::string::npos);

    drd_info drd ("structured-radioactive", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write ("athena-radioactive-links-in-transclusion", "true");
    env->write (RADIOACTIVE_LINK_COLOR, "preserve");
    env->write (MODE, "text");
    env->update ();
    tree old_locus= env->read ("athena-inside-locus");
    tree paragraph (CONCAT, "A ", sigma, "-algebra is given.");
    box linked= typeset_as_concat (env, paragraph, path ());
    QVERIFY (link_count (linked, destination) >= 2);
    QCOMPARE (env->read ("athena-inside-locus"), old_locus);
    env->write ("athena-radioactive-links-suppressed", "true");
    box plain= typeset_as_concat (env, paragraph, path ());
    QCOMPARE (link_count (plain, destination), 0);
    QCOMPARE (linked->x2-linked->x1, plain->x2-plain->x1);
    QCOMPARE (linked->y1, plain->y1);
    QCOMPARE (linked->y2, plain->y2);
    env->write ("athena-radioactive-links-suppressed", "false");
    env->write (PAGE_PRINTED, "true");
    QCOMPARE (link_count (typeset_as_concat (env, paragraph, path ()), destination), 0);
  }
};

static void run_tests (int argc, char** argv) {
  init_plugins ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    StructuredRadioactiveTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "structured_radioactive_test.moc"
