/******************************************************************************
* MODULE     : artifact_delegation_queue_test.cpp
* DESCRIPTION: Tests for asynchronous artifact delegation jobs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include <QtTest/QtTest>

#include "artifact_delegation_queue.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

using athena::mcp::ArtifactDelegationQueue;
using athena::mcp::ArtifactDelegationQueueOptions;

class TestArtifactDelegationQueue: public QObject {
  Q_OBJECT

private slots:
  void validatesAndIsolatesJobs ();
  void submitIsIdempotentAndAcknowledged ();
};

static QJsonObject
request_params (const QString& submission) {
  QJsonArray catalog;
  catalog.append ("A definition paragraph.");
  QJsonArray candidates;
  QJsonObject candidate;
  candidate["offset"]= 0;
  candidate["catalog"]= 0;
  candidates.append (candidate);
  QJsonObject request;
  request["id"]= "r0";
  request["keyword_latex"]= "definition";
  request["candidates"]= candidates;
  QJsonArray requests;
  requests.append (request);
  QJsonObject params;
  params["submission_id"]= submission;
  params["catalog"]= catalog;
  params["requests"]= requests;
  return params;
}

void
TestArtifactDelegationQueue::validatesAndIsolatesJobs () {
  ArtifactDelegationQueueOptions options;
  options.model_path= "/definitely/missing/artifact-range-model.gguf";
  options.max_requests_per_job= 4;
  ArtifactDelegationQueue queue (options);
  QVERIFY (!queue.available ());

  QJsonObject invalid= request_params ("invalid");
  QJsonArray badRequests= invalid.value ("requests").toArray ();
  QJsonObject badRequest= badRequests[0].toObject ();
  badRequest["candidates"]= QJsonArray ();
  badRequests[0]= badRequest;
  invalid["requests"]= badRequests;
  QVERIFY (!queue.submit ("alice", invalid).value ("ok").toBool ());

  QJsonObject submitted= queue.submit ("alice", request_params ("one"));
  QVERIFY (submitted.value ("ok").toBool ());
  QJsonObject wait;
  wait["job_id"]= submitted.value ("job_id").toString ();
  QVERIFY (!queue.wait ("bob", wait).value ("ok").toBool ());
}

void
TestArtifactDelegationQueue::submitIsIdempotentAndAcknowledged () {
  ArtifactDelegationQueueOptions options;
  options.model_path= "/definitely/missing/artifact-range-model.gguf";
  ArtifactDelegationQueue queue (options);
  QJsonObject first= queue.submit ("alice", request_params ("same"));
  QJsonObject repeated= queue.submit ("alice", request_params ("same"));
  QVERIFY (first.value ("ok").toBool ());
  QCOMPARE (repeated.value ("job_id"), first.value ("job_id"));

  QJsonObject wait;
  wait["job_id"]= first.value ("job_id").toString ();
  wait["cursor"]= 0;
  QJsonObject status;
  QTRY_VERIFY_WITH_TIMEOUT (
    (status= queue.wait ("alice", wait)).value ("state").toString () ==
      "failed",
    2000);
  QCOMPARE (status.value ("results").toArray ().size (), 0);
  QJsonObject ack= queue.acknowledge ("alice", wait);
  QVERIFY (ack.value ("ok").toBool ());
  QVERIFY (!queue.wait ("alice", wait).value ("ok").toBool ());
}

QTEST_MAIN (TestArtifactDelegationQueue)
#include "artifact_delegation_queue_test.moc"
