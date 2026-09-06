/******************************************************************************
* MODULE     : evaluation_bar_conversion_test.cpp
* DESCRIPTION: Structural evaluation-bar maintenance conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "ATHENA/Data/evaluation_bars.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_maintenance.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "boot.hpp"
#include "convert.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static tree bar (tree body) {
  return tree (VAR_AROUND, "<nobracket>", body, "|");
}

class TestEvaluationBarConversion: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }
  void fractionAndLimit () {
    tree fraction (FRAC, "d", "dt");
    tree source (CONCAT, fraction, "|", tree (RSUB, "t=0"));
    int count= 0;
    tree result= athena_promote_evaluation_bars (source, count, "math");
    QVERIFY (result == tree (CONCAT, bar (fraction), tree (RSUB, "t=0")));
    QCOMPARE (count, 1);
    count= 0;
    QVERIFY (athena_promote_evaluation_bars (result, count, "math") == result);
    QCOMPARE (count, 0);
    QVERIFY (source[1] == "|");
  }
  void atomsAndBoundaries () {
    int count= 0;
    QVERIFY (athena_promote_evaluation_bars ("x|+y<vert>", count, "math") ==
             bar (tree (CONCAT, bar ("x"), "+y")));
    QCOMPARE (count, 2);
    tree fraction (FRAC, "x|", "y|");
    count= 0;
    QVERIFY (athena_promote_evaluation_bars (fraction, count, "math") ==
             tree (FRAC, bar ("x"), bar ("y")));
    QCOMPARE (count, 2);
    tree cells (TABLE, tree (ROW, tree (CELL, "x|"), tree (CELL, "y|")));
    count= 0;
    QVERIFY (athena_promote_evaluation_bars (cells, count, "math") ==
             tree (TABLE, tree (ROW, tree (CELL, bar ("x")),
                                    tree (CELL, bar ("y")))));
    QCOMPARE (count, 2);
  }
  void preserveRelationsAndDelimiters () {
    tree source (CONCAT);
    source << tree ("a<mid>b<divides>c<shortmid>d<nmid>e<||>f")
           << tree (VAR_AROUND, "|", "x", "|")
           << tree (AROUND, "|", "y", "|")
           << tree (LEFT, "|") << tree (MID, "|") << tree (RIGHT, "|");
    int count= 0;
    QVERIFY (athena_promote_evaluation_bars (source, count, "math") == source);
    QCOMPARE (count, 0);
  }
  void modesAndAttributes () {
    tree source (DOCUMENT, "prose | <vert>",
      tree (WITH, "mode", "math",
        tree (CONCAT, "x|", tree (WITH, "mode", "text", "text|"))));
    int count= 0;
    tree result= athena_promote_evaluation_bars (source, count);
    QCOMPARE (count, 1);
    QVERIFY (result[0] == source[0]);
    QVERIFY (result[1][2] == tree (CONCAT, bar ("x"),
                                      tree (WITH, "mode", "text", "text|")));
    tree attr (WITH, "font", "foo|bar", "x|");
    count= 0;
    QVERIFY (athena_promote_evaluation_bars (attr, count, "math") ==
             tree (WITH, "font", "foo|bar", bar ("x")));
    QCOMPARE (count, 1);
  }
  void temporaryVault () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    std::filesystem::path root (temporary.path ().toStdString ());
    tree body (DOCUMENT, compound ("math", tree (CONCAT,
      tree (FRAC, "d", "dt"), "|", tree (RSUB, "t=0"))),
      compound ("equation*", "x|"));
    tree document (DOCUMENT);
    document << compound ("TeXmacs", "2.1.4")
             << compound ("style", tuple ("generic"))
             << compound ("body", body);
    auto source= tm_to_std (tree_to_texmacs (document));
    QVERIFY (write_file_bytes (root / "sample.ath", source));
    QVERIFY (write_file_bytes (root / "untouched.tm", source));
    std::filesystem::create_directory (root / ".backup");
    QVERIFY (write_file_bytes (root / ".backup/untouched.ath", source));
    VaultMaintenanceContext ctx;
    ctx.root= root;
    auto result= vault_maintenance_pass_promote_evaluation_bars (ctx);
    QVERIFY2 (result.ok, result.message.c_str ());
    QVERIFY2 (result.message == "promoted 2 bar(s) in 1 of 1 ATHENA document(s)",
              result.message.c_str ());
    std::string rewritten, untouched;
    QVERIFY (read_file_bytes (root / "sample.ath", rewritten));
    tree expected (DOCUMENT, compound ("math", tree (CONCAT,
      bar (tree (FRAC, "d", "dt")), tree (RSUB, "t=0"))),
      compound ("equation*", bar ("x")));
    QVERIFY (extract (texmacs_document_to_tree (std_to_tm (rewritten)), "body") ==
             expected);
    QVERIFY (read_file_bytes (root / "untouched.tm", untouched));
    QCOMPARE (untouched, source);
    QVERIFY (read_file_bytes (root / ".backup/untouched.ath", untouched));
    QCOMPARE (untouched, source);
    result= vault_maintenance_pass_promote_evaluation_bars (ctx);
    QVERIFY2 (result.ok, result.message.c_str ());
    QCOMPARE (result.message, std::string (
      "promoted 0 bar(s) in 0 of 1 ATHENA document(s)"));
    QVERIFY (read_file_bytes (root / "sample.ath", untouched));
    QCOMPARE (untouched, rewritten);

    // A bad input prevents any rewrite, even after a valid candidate was read.
    QVERIFY (write_file_bytes (root / "sample.ath", source));
    QVERIFY (write_file_bytes (root / "z-invalid.ath", "not a document"));
    result= vault_maintenance_pass_promote_evaluation_bars (ctx);
    QVERIFY (!result.ok);
    QVERIFY (read_file_bytes (root / "sample.ath", untouched));
    QCOMPARE (untouched, source);
  }
  void optionalPassDefaults () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    AthenaVaultfileInfo info;
    std::string error;
    QVERIFY2 (athena_vaultfile_write (
      std::filesystem::path (temporary.path ().toStdString ()), info, error),
      error.c_str ());
    std::vector<VaultMaintenancePlanEntry> entries;
    QVERIFY2 (vault_maintenance_plan (
      string (temporary.path ().toUtf8 ().constData ()), entries, error),
      error.c_str ());
    bool found= false;
    std::string skipped;
    for (const auto& entry: entries)
      if (entry.id == "promote-evaluation-bars") {
        found= true;
        QVERIFY (!entry.selected_by_default);
      }
      else {
        if (!skipped.empty ()) skipped += ",";
        skipped += entry.id;
      }
    QVERIFY (found);
    auto path= std::filesystem::path (temporary.path ().toStdString ()) /
               "optional.ath";
    tree document (DOCUMENT);
    document << compound ("TeXmacs", "2.1.4")
             << compound ("style", tuple ("generic"))
             << compound ("body", tree (DOCUMENT, compound ("math", "x|")));
    std::string source= tm_to_std (tree_to_texmacs (document)), after;
    QVERIFY (write_file_bytes (path, source));
    qputenv ("ATHENA_VAULT_MAINTENANCE_SKIP_PASSES", skipped.c_str ());
    qunsetenv ("ATHENA_VAULT_MAINTENANCE_ENABLE_PASSES");
    QVERIFY (vault_maintenance_run (std_to_tm (temporary.path ().toStdString ())));
    QVERIFY (read_file_bytes (path, after));
    QCOMPARE (after, source);
    qputenv ("ATHENA_VAULT_MAINTENANCE_ENABLE_PASSES", "promote-evaluation-bars");
    QVERIFY (vault_maintenance_run (std_to_tm (temporary.path ().toStdString ())));
    QVERIFY (read_file_bytes (path, after));
    QVERIFY (after != source);
    qunsetenv ("ATHENA_VAULT_MAINTENANCE_ENABLE_PASSES");
    qunsetenv ("ATHENA_VAULT_MAINTENANCE_SKIP_PASSES");
  }
};

static void run_tests (int argc, char** argv) {
  init_plugins ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    TestEvaluationBarConversion test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("HOME", profile.path ().toUtf8 ());
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  qputenv ("XDG_CONFIG_HOME", profile.filePath ("config").toUtf8 ());
  qputenv ("XDG_CACHE_HOME", profile.filePath ("cache").toUtf8 ());
  qputenv ("XDG_DATA_HOME", profile.filePath ("data").toUtf8 ());
  QApplication app (argc, argv);
  cache_initialize ();
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}
#include "evaluation_bar_conversion_test.moc"
