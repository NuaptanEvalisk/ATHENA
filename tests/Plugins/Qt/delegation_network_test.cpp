/******************************************************************************
* MODULE     : delegation_network_test.cpp
* DESCRIPTION: Opt-in live tests for ATHENA Delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include <QtTest/QtTest>

#include "QTMDelegationClient.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <sqlite3.h>

class TestDelegationNetwork: public QObject {
  Q_OBJECT

private slots:
  void exercisesLiveArtifactDelegation ();
  void exercisesLiveIncrementalRagDelegation ();
};

static QTMDelegationServer
live_server () {
  QString url= QString::fromUtf8 (qgetenv ("ATHENA_DELEGATION_TEST_URL"));
  QTMDelegationServer server;
  QString error;
  if (!qtm_delegation_fetch_identity (url, server, &error))
    qFatal ("Could not fetch live delegation identity: %s",
            qPrintable (error));
  return server;
}

static void
write_ath (const QString& path, const QString& text) {
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate))
    qFatal ("Could not write test ATHENA document");
  file.write (("<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\n" +
              text + "\n</body>\n").toUtf8 ());
}

static int
sqlite_count (const QString& path, const char* table) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (path.toUtf8 ().constData (), &db,
                       SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    qFatal ("Could not open delegated RAG database");
  QString sql= QString ("SELECT COUNT(*) FROM %1").arg (table);
  sqlite3_stmt* statement= nullptr;
  if (sqlite3_prepare_v2 (db, sql.toUtf8 ().constData (), -1,
                          &statement, nullptr) != SQLITE_OK)
    qFatal ("Could not query delegated RAG database");
  int count= sqlite3_step (statement) == SQLITE_ROW ?
             sqlite3_column_int (statement, 0): -1;
  sqlite3_finalize (statement);
  sqlite3_close (db);
  return count;
}

void
TestDelegationNetwork::exercisesLiveArtifactDelegation () {
  QString url= QString::fromUtf8 (qgetenv ("ATHENA_DELEGATION_TEST_URL"));
  if (url.isEmpty ())
    QSKIP ("Set ATHENA_DELEGATION_TEST_URL to run the live network test");

  QTMDelegationServer server;
  QString error;
  QVERIFY2 (qtm_delegation_fetch_identity (url, server, &error),
            qPrintable (error));
  QVERIFY (server.capabilities.contains ("athena-delegation-v1"));
  QVERIFY (server.capabilities.contains ("artifact-definition-span-v2"));
  QVERIFY2 (qtm_delegation_save_servers ({server}, &error),
            qPrintable (error));

  if (qEnvironmentVariableIntValue ("ATHENA_DELEGATION_TEST_ENROLL") != 0) {
    QString status;
    QVERIFY2 (qtm_delegation_enroll (server, &status, &error),
              qPrintable (error));
    qInfo ().noquote () << "enrollment status:" << status;
    qInfo ().noquote () << "delegation config:" << qtm_delegation_config_dir ();
    QSKIP ("Enrollment submitted; accept the emitted client key and rerun");
  }

  QString status;
  QVERIFY2 (qtm_delegation_check_auth (server, &status, &error),
            qPrintable (error));
  QCOMPARE (status, QString ("accepted"));

  bool countOk= false;
  int requestCount= qEnvironmentVariableIntValue (
    "ATHENA_DELEGATION_TEST_ARTIFACT_REQUESTS", &countOk);
  if (!countOk || requestCount < 1) requestCount= 16;
  std::vector<AthenaArtifactRangeRequest> requests;
  for (int i=0; i<requestCount; i++) {
    AthenaArtifactRangeRequest request;
    request.keyword_latex= "compactness";
    request.paragraphs.push_back ({0,
      "The term \\textbf{compactness} denotes the property that every open "
      "cover has a finite subcover."});
    request.paragraphs.push_back ({1,
      "This condition is preserved by continuous maps into Hausdorff spaces."});
    requests.push_back (std::move (request));
  }

  std::vector<std::vector<int>> results;
  size_t observedCompleted= 0;
  QVERIFY2 (qtm_delegation_select_artifact_ranges (
    server, requests, results,
    [&] (size_t completed, size_t total, size_t queued, size_t running) {
      observedCompleted= std::max (observedCompleted, completed);
      qInfo () << "artifact delegation progress" << completed << "/" << total
               << "queued" << queued << "running" << running;
      return true;
    }, &error), qPrintable (error));
  QCOMPARE (results.size (), requests.size ());
  QCOMPARE (observedCompleted, requests.size ());
  for (const std::vector<int>& result: results) {
    QVERIFY (!result.empty ());
    QCOMPARE (result.front (), 0);
  }
}

void
TestDelegationNetwork::exercisesLiveIncrementalRagDelegation () {
  QString url= QString::fromUtf8 (qgetenv ("ATHENA_DELEGATION_TEST_URL"));
  if (url.isEmpty ())
    QSKIP ("Set ATHENA_DELEGATION_TEST_URL to run the live network test");
  if (qEnvironmentVariableIntValue ("ATHENA_DELEGATION_TEST_ENROLL") != 0)
    QSKIP ("Enrollment run does not execute workloads");
  QString model= QString::fromUtf8 (
    qgetenv ("ATHENA_DELEGATION_TEST_EMBEDDING_MODEL"));
  if (model.isEmpty ())
    QSKIP ("Set ATHENA_DELEGATION_TEST_EMBEDDING_MODEL for the live RAG test");

  QTemporaryDir vault;
  QVERIFY (vault.isValid ());
  QString first= vault.filePath ("Alpha.ath");
  QString second= vault.filePath ("Beta.ath");
  QString database= vault.filePath ("rag.sqlite");
  write_ath (first, "Alpha topology studies open and closed subsets.");
  write_ath (second, "Beta algebra studies groups, rings, and fields.");

  QTMDelegationServer server= live_server ();
  QString summary;
  QString error;
  QVERIFY2 (qtm_delegation_run_embedding (
    server, vault.path (), database, model, "auto", &summary, &error),
    qPrintable (error));
  qInfo ().noquote () << "initial delegated RAG:" << summary;
  QCOMPARE (sqlite_count (database, "documents"), 2);
  QVERIFY (sqlite_count (database, "chunks") >= 2);

  write_ath (first,
    "Alpha topology studies compact spaces and continuous functions.");
  QVERIFY2 (qtm_delegation_run_embedding (
    server, vault.path (), database, model, "auto", &summary, &error),
    qPrintable (error));
  qInfo ().noquote () << "incremental delegated RAG:" << summary;
  QVERIFY (summary.contains ("1 changed .ath files"));
  QCOMPARE (sqlite_count (database, "documents"), 2);

  QVERIFY (QFile::remove (second));
  QVERIFY2 (qtm_delegation_run_embedding (
    server, vault.path (), database, model, "auto", &summary, &error),
    qPrintable (error));
  qInfo ().noquote () << "delete delegated RAG:" << summary;
  QVERIFY (summary.contains ("1 deletions"));
  QCOMPARE (sqlite_count (database, "documents"), 1);
}

QTEST_GUILESS_MAIN (TestDelegationNetwork)
#include "delegation_network_test.moc"
