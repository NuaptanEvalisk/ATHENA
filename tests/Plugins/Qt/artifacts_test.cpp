/******************************************************************************
* MODULE     : artifacts_test.cpp
* DESCRIPTION: Tests for ATHENA semantic artifact extraction and indexing
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/artifact_range_llm.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;

class TestArtifacts: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void selectsDefinitionRangeWithConfiguredModel ();
  void extractsEnunciationsBoldTextAndProofLink ();
  void boundsBoldDefinitionCandidatesAtEnunciations ();
  void doesNotLinkNonAdjacentProof ();
  void buildsIncrementallyAndPurgesDeletedDocuments ();
  void reportsBuildPhasesInOrder ();
};

class MissingRangeModel {
public:
  MissingRangeModel (): previous (qgetenv ("ATHENA_ARTIFACT_RANGE_MODEL")),
                        hadPrevious (!previous.isNull ()) {
    qputenv ("ATHENA_ARTIFACT_RANGE_MODEL",
             "/definitely/missing/athena-artifact-range-model.gguf");
  }
  ~MissingRangeModel () {
    if (hadPrevious) qputenv ("ATHENA_ARTIFACT_RANGE_MODEL", previous);
    else qunsetenv ("ATHENA_ARTIFACT_RANGE_MODEL");
  }

private:
  QByteArray previous;
  bool hadPrevious;
};

void
TestArtifacts::initTestCase () {
  qputenv ("ATHENA_ARTIFACT_WORKER_EXECUTABLE",
           (QCoreApplication::applicationDirPath () + "/../src/ATHENA.bin")
             .toUtf8 ());
  QByteArray libraryPath=
    (QCoreApplication::applicationDirPath () + "/../x64/lib").toUtf8 ();
  QByteArray inherited= qgetenv ("LD_LIBRARY_PATH");
  if (!inherited.isEmpty ()) libraryPath += ':' + inherited;
  qputenv ("LD_LIBRARY_PATH", libraryPath);
}

void
TestArtifacts::selectsDefinitionRangeWithConfiguredModel () {
  if (qEnvironmentVariableIsEmpty ("ATHENA_ARTIFACT_RANGE_MODEL"))
    QSKIP ("No real artifact range model configured");
  std::vector<std::pair<int,std::string>> paragraphs= {
    {-1, "We now introduce a useful class."},
    {0, "A map is called a covering map if it satisfies the following local "
        "condition."},
    {1, "Every point has an open neighborhood whose inverse image is a "
        "disjoint union of sets mapped homeomorphically onto it."},
    {2, "The identity map is an example."}
  };
  std::vector<int> selected= athena_artifact_select_definition_range (
    "covering map", paragraphs);
  QVERIFY (std::find (selected.begin (), selected.end (), 0) != selected.end ());
  QVERIFY (std::find (selected.begin (), selected.end (), 1) != selected.end ());
  for (int offset: selected) QVERIFY (offset >= -1 && offset <= 2);
}

static tree
artifact_test_document (const char* keyword) {
  tree body (DOCUMENT);
  body << compound ("label", "Compactness theorem {")
       << compound ("theorem", "Every finite rank operator is compact.")
       << compound ("label", "Compactness theorem }")
       << compound ("label", "Proof of compactness {")
       << compound ("proof", "The image of the unit ball is bounded in a "
                              "finite dimensional space.")
       << compound ("label", "Proof of compactness }");
  tree paragraph (CONCAT);
  paragraph << "An operator is called " << compound ("strong", keyword)
            << " when it maps bounded sets to relatively compact sets. "
            << compound ("strong", "not");
  body << paragraph;
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  return document;
}

static void
write_document (const fs::path& path, const tree& document) {
  string serialized= tree_to_texmacs (document);
  std::ofstream output (path, std::ios::binary | std::ios::trunc);
  output.write (as_charp (serialized), N(serialized));
}

void
TestArtifacts::extractsEnunciationsBoldTextAndProofLink () {
  MissingRangeModel noModel;
  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (
              artifact_test_document ("compact operator"), "A.ath", records,
              error), error.c_str ());
  QCOMPARE (records.size (), (size_t) 3);
  QCOMPARE (records[0].type, std::string ("provable"));
  QCOMPARE (records[0].anchor_stem,
            std::string ("Compactness theorem"));
  QVERIFY (records[0].proof_uuid.rfind ("@order:", 0) == 0);
  QCOMPARE (records[1].type, std::string ("completion"));
  QCOMPARE (records[2].origin, std::string ("bold-text"));
  QCOMPARE (records[2].display_text, std::string ("compact operator"));
  QVERIFY (records[2].keyword_tree.find ("<strong|compact operator>") !=
           std::string::npos);
  QCOMPARE (records[2].paragraph_offsets, std::vector<int> ({0}));
}

void
TestArtifacts::boundsBoldDefinitionCandidatesAtEnunciations () {
  MissingRangeModel noModel;
  tree definition (CONCAT);
  definition << "A map is a " << compound ("strong", "covering map")
             << " when it satisfies the local condition.";
  tree body (DOCUMENT);
  body << "Text before the first boundary."
       << compound ("theorem", "First boundary.")
       << definition
       << compound ("equation*", "p^{-1}(U)=\\bigsqcup_i V_i")
       << "This paragraph completes the definition."
       << compound ("lemma", "Second boundary.")
       << "Text after the second boundary.";
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);

  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (document, "bounded.ath",
                                                records, error),
            error.c_str ());
  auto bold= std::find_if (records.begin (), records.end (), [] (const auto& r) {
    return r.origin == "bold-text";
  });
  QVERIFY (bold != records.end ());
  QCOMPARE (bold->definition_candidates.size (), (size_t) 2);
  QCOMPARE (bold->definition_candidates[0].first, 0);
  QCOMPARE (bold->definition_candidates[1].first, 1);
  QVERIFY (bold->definition_candidates[0].second.find ("equation*") !=
           std::string::npos);
}

void
TestArtifacts::doesNotLinkNonAdjacentProof () {
  MissingRangeModel noModel;
  tree body (DOCUMENT);
  body << compound ("label", "Separated theorem {")
       << compound ("theorem", "A statement.")
       << compound ("label", "Separated theorem }")
       << "An intervening discussion paragraph."
       << compound ("label", "Late proof {")
       << compound ("proof", "A proof.")
       << compound ("label", "Late proof }");
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (document, "separated.ath",
                                                records, error),
            error.c_str ());
  QCOMPARE (records.size (), (size_t) 2);
  QVERIFY (records[0].proof_uuid.empty ());
}

void
TestArtifacts::buildsIncrementallyAndPurgesDeletedDocuments () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  info.artifacts_path= "indexes/artifacts.db";
  info.enunciations_path= "indexes/enunciations.db";
  info.bold_text_path= "indexes/bold-text.db";
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (root / "A.ath", artifact_test_document ("compact operator"));
  write_document (root / "B.ath", artifact_test_document ("Fredholm operator"));

  AthenaArtifactsBuildResult first;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first, error),
            error.c_str ());
  QCOMPARE (first.documents_changed, (size_t) 2);
  QCOMPARE (first.artifacts, (size_t) 6);
  QVERIFY (fs::exists (root / "indexes/artifacts.db"));
  QVERIFY (fs::exists (root / "indexes/enunciations.db"));
  QVERIFY (fs::exists (root / "indexes/bold-text.db"));

  std::vector<AthenaArtifactRecord> records;
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  QCOMPARE (records.size (), (size_t) 6);
  std::string stable_uuid= records[0].content_uuid;
  QVERIFY (!records[0].proof_uuid.empty ());

  AthenaArtifactsBuildResult unchanged;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, unchanged, error),
            error.c_str ());
  QCOMPARE (unchanged.documents_changed, (size_t) 0);

  write_document (root / "A.ath", artifact_test_document ("compact map"));
  AthenaArtifactsBuildResult modified;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, modified, error),
            error.c_str ());
  QCOMPARE (modified.documents_changed, (size_t) 1);
  records.clear ();
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  QCOMPARE (records[0].content_uuid, stable_uuid);

  fs::remove (root / "B.ath");
  AthenaArtifactsBuildResult purged;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, purged, error),
            error.c_str ());
  QCOMPARE (purged.documents_deleted, (size_t) 1);
  records.clear ();
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  QCOMPARE (records.size (), (size_t) 3);
  for (const AthenaArtifactRecord& record: records)
    QCOMPARE (record.relative_path, std::string ("A.ath"));
}

void
TestArtifacts::reportsBuildPhasesInOrder () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (root / "A.ath", artifact_test_document ("compact operator"));

  std::vector<AthenaArtifactsBuildPhase> phases;
  AthenaArtifactsBuildResult result;
  QVERIFY2 (athena_artifacts_build (
              root, {}, true,
              [&] (const AthenaArtifactsProgressEvent& event) {
                phases.push_back (event.phase);
                return true;
              }, result, error), error.c_str ());
  QVERIFY (!phases.empty ());
  QCOMPARE (phases.front (), AthenaArtifactsBuildPhase::Preparing);
  QCOMPARE (phases.back (), AthenaArtifactsBuildPhase::Complete);
  QVERIFY (std::find (phases.begin (), phases.end (),
                      AthenaArtifactsBuildPhase::Extracting) != phases.end ());
  QVERIFY (std::find (
             phases.begin (), phases.end (),
             AthenaArtifactsBuildPhase::SelectingDefinitionRanges) !=
           phases.end ());
  QVERIFY (std::find (phases.begin (), phases.end (),
                      AthenaArtifactsBuildPhase::WritingDatabase) != phases.end ());
  QCOMPARE (std::count (phases.begin (), phases.end (),
                        AthenaArtifactsBuildPhase::Complete), 1);
}

QTEST_MAIN (TestArtifacts)
#include "artifacts_test.moc"
