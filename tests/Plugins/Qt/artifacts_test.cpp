/******************************************************************************
* MODULE     : artifacts_test.cpp
* DESCRIPTION: Tests for ATHENA semantic artifact extraction and indexing
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include <QtTest/QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/artifact_identity.hpp"
#include "ATHENA/Data/artifact_document.hpp"
#include "ATHENA/Data/artifact_range_llm.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "converter.hpp"
#include "convert.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;

class TestArtifacts: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void validatesDefinitionRangeModelOutput ();
  void expandsDefinitionRangeWindowProgressively ();
  void selectsDefinitionRangeWithConfiguredModel ();
  void extractsEnunciationsBoldTextAndProofLink ();
  void filtersNonNamesBeforeDefinitionRangeInference ();
  void appliesVaultArtifactTitleFilterIncrementally ();
  void boundsBoldDefinitionCandidatesAtEnunciations ();
  void isolatesStructuredBoldBlocks ();
  void locatesStoredParagraphRange ();
  void locatesStructuredEnunciationsAfterEdits ();
  void excludesOnlyArtifactDefiningOccurrences ();
  void excludesEnunciationTitlesFromRadioactiveLinks ();
  void doesNotLinkNonAdjacentProof ();
  void buildsIncrementallyAndPurgesDeletedDocuments ();
  void preservesAccentedArtifactTextAcrossWorkerAndDatabase ();
  void storesAndDisambiguatesSameNamedArtifacts ();
  void navigatesArtifactAndLoadsDisambiguationPage ();
  void keepsRadioactiveRangeMacroLociAlive ();
  void preservesBoldIdentityWhenDuplicateIsInsertedBefore ();
  void doesNotTransferDeletedDuplicateToInsertedDuplicate ();
  void rejectsIdentityTransferWhenParagraphIsCopiedExactly ();
  void preservesExactDuplicateIdentityGroupByDocumentOrder ();
  void preservesUniqueBoldIdentityAcrossHostEdit ();
  void preservesUnanchoredEnunciationsWhenOneIsInsertedBefore ();
  void doesNotTransferDeletedUnanchoredDuplicateEnunciation ();
  void preservesIdentityAcrossTrustedPathRenames ();
  void matchesRadioactiveLinksByCaseAndInflection ();
  void matchesPossessiveAndEponymRadioactiveLinks ();
  void prefersLongestRadioactiveArtifactTerm ();
  void linksAmbiguousRadioactiveArtifactTerms ();
  void namesEnunciationsStrictlyAndSkipsCompletions ();
  void preservesUnicodeRadioactiveMatchOffsets ();
  void matchesLargeRadioactiveArtifactIndexWithinBudget ();
  void reportsBuildPhasesInOrder ();
  void excludesExternalResourcesFromDefinitionRangeRequests ();
  void resumesDefinitionRangeSelectionFromCheckpoint ();
  void delegatedFailureLeavesDatabaseUnchanged ();
};

void
TestArtifacts::validatesDefinitionRangeModelOutput () {
  const std::vector<std::pair<int,std::string>> paragraphs= {
    {-1, "Before"}, {0, "Focus"}, {1, "After"}, {2, "Later"}
  };
  auto parse= [&] (const char* output, bool fallback= false) {
    return athena_artifact_parse_definition_range_output (
      output, paragraphs, fallback);
  };

  QCOMPARE (parse ("[0]"), std::vector<int> ({0}));
  QVERIFY (parse ("[]").empty ());
  QCOMPARE (parse ("Answer: [-1, 0, 1]"),
            std::vector<int> ({-1, 0, 1}));
  QCOMPARE (parse ("Answer: [-1, 1]"),
            std::vector<int> ({-1, 0, 1}));
  QVERIFY (parse ("[2]").empty ());
  QCOMPARE (parse ("[0, 2]"), std::vector<int> ({0, 1, 2}));
  QVERIFY (parse ("[1, 0]").empty ());
  QVERIFY (parse ("[0, 0]").empty ());
  QVERIFY (parse ("[0, 9]").empty ());
  QVERIFY (parse ("[0, prose]").empty ());
  QCOMPARE (parse ("[2]", true), std::vector<int> ({0}));
  QCOMPARE (parse ("[0, 2]", true), std::vector<int> ({0, 1, 2}));
  QCOMPARE (parse ("[]", true), std::vector<int> ({0}));
}

void
TestArtifacts::expandsDefinitionRangeWindowProgressively () {
  AthenaArtifactRangeRequest local;
  local.keyword_latex= "local";
  AthenaArtifactRangeRequest extended;
  extended.keyword_latex= "extended";
  for (int offset=-5; offset<=5; offset++) {
    local.paragraphs.push_back ({offset, std::to_string (offset)});
    extended.paragraphs.push_back ({offset, std::to_string (offset)});
  }
  AthenaArtifactRangeRequest isolated;
  isolated.keyword_latex= "isolated";
  isolated.paragraphs= {{0, "focus"}};

  int wave= 0;
  std::vector<std::vector<std::pair<int,int>>> observed;
  std::atomic<size_t> completed (0);
  auto selected= athena_artifact_select_definition_ranges_progressively (
    {local, extended, isolated},
    [&] (const std::vector<AthenaArtifactRangeRequest>& requests)
      -> std::vector<std::vector<int>> {
      wave++;
      observed.push_back ({});
      for (const AthenaArtifactRangeRequest& request: requests)
        observed.back ().push_back ({request.paragraphs.front ().first,
                                     request.paragraphs.back ().first});
      if (wave == 1) {
        return std::vector<std::vector<int>> {
          {0}, {-1, 0, 1}
        };
      }
      if (wave == 2) {
        return std::vector<std::vector<int>> {
          {-2, -1, 0, 1, 2}
        };
      }
      return std::vector<std::vector<int>> {
        {-1, 0, 1}
      };
    }, nullptr, &completed);

  QCOMPARE (wave, 3);
  QCOMPARE (completed.load (), (size_t) 3);
  QCOMPARE (selected.size (), (size_t) 3);
  QCOMPARE (selected[0], std::vector<int> ({0}));
  QCOMPARE (selected[1], std::vector<int> ({-1, 0, 1}));
  QCOMPARE (selected[2], std::vector<int> ({0}));
  QCOMPARE (observed.size (), (size_t) 3);
  const std::vector<std::pair<int,int>> first= {{-1, 1}, {-1, 1}};
  const std::vector<std::pair<int,int>> second= {{-2, 2}};
  const std::vector<std::pair<int,int>> third= {{-4, 4}};
  QCOMPARE (observed[0], first);
  QCOMPARE (observed[1], second);
  QCOMPARE (observed[2], third);
}

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
  std::vector<AthenaArtifactRangeRequest> requests= {
    {"covering map", paragraphs}, {"local covering condition", paragraphs}
  };
  std::atomic<size_t> completed (0);
  auto batched= athena_artifact_select_definition_ranges (
    requests, athena_artifact_range_model_path (), nullptr, &completed);
  QCOMPARE (batched.size (), (size_t) 2);
  QCOMPARE (completed.load (), (size_t) 2);
  std::vector<int> selected= batched[0];
  QVERIFY (std::find (batched[1].begin (), batched[1].end (), 0) !=
           batched[1].end ());
  std::vector<int> selectedAfterBatch=
    athena_artifact_select_definition_range ("covering map", paragraphs);
  athena_artifact_range_model_release ();
  QVERIFY (std::find (selected.begin (), selected.end (), 0) != selected.end ());
  QVERIFY (std::find (selected.begin (), selected.end (), 1) != selected.end ());
  for (int offset: selected) QVERIFY (offset >= -1 && offset <= 2);
  QCOMPARE (selectedAfterBatch, selected);
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
            << " when it maps bounded sets to relatively compact sets.";
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

static tree
bold_paragraph_document (
  const std::vector<std::pair<std::string,std::string>>& paragraphs,
  const char* keyword= "shared concept") {
  tree body (DOCUMENT);
  for (const auto& paragraph_parts: paragraphs) {
    tree paragraph (CONCAT);
    paragraph << paragraph_parts.first.c_str () << compound ("strong", keyword)
              << paragraph_parts.second.c_str ();
    body << paragraph;
  }
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  return document;
}

static tree
named_theorem_document (const std::string& name, const std::string& statement) {
  tree body (DOCUMENT);
  std::string content= "(" + name + ") " + statement;
  body << compound ("label", ("theorem:" + name + " {").c_str ())
       << compound ("theorem", compound ("strong", content.c_str ()))
       << compound ("label", ("theorem:" + name + " }").c_str ());
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  return document;
}

static tree
structured_theorem_document (int prefix_count= 0, bool folded= false) {
  tree body (DOCUMENT);
  for (int i=0; i<prefix_count; i++)
    body << tree (("Prelude paragraph " + std::to_string (i) + ".").c_str ());
  tree opening (CONCAT);
  opening << "theorem:Kernel of " << compound ("math", "f") << " {";
  tree closing (CONCAT);
  closing << "theorem:Kernel of " << compound ("math", "f") << " }";
  tree theorem_body (DOCUMENT);
  theorem_body << compound ("label", opening)
               << compound (
                    "theorem",
                    compound ("strong",
                              "(Kernel theorem) The kernel is normal."))
               << compound ("label", closing);
  if (folded)
    body << compound ("folded", "Collapsed theorem", theorem_body);
  else
    for (int i=0; i<N(theorem_body); i++) body << theorem_body[i];
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  return document;
}

static QString
scheme_quote (QString value) {
  value.replace ('\\', "\\\\");
  value.replace ('"', "\\\"");
  return '"' + value + '"';
}

static const AthenaArtifactRecord*
record_with_display_text (const std::vector<AthenaArtifactRecord>& records,
                          const std::string& text) {
  auto found= std::find_if (records.begin (), records.end (), [&] (const auto& r) {
    return r.origin == "enunciation" && r.display_text == text;
  });
  return found == records.end () ? nullptr : &*found;
}

static bool
exec_test_sql (const fs::path& path, const std::string& sql,
               std::string& error) {
  sqlite3* database= nullptr;
  if (sqlite3_open (path.string ().c_str (), &database) != SQLITE_OK) {
    error= database ? sqlite3_errmsg (database) : "Could not open test database";
    if (database) sqlite3_close (database);
    return false;
  }
  char* message= nullptr;
  int status= sqlite3_exec (database, sql.c_str (), nullptr, nullptr, &message);
  if (status != SQLITE_OK) error= message ? message : sqlite3_errmsg (database);
  sqlite3_free (message);
  sqlite3_close (database);
  return status == SQLITE_OK;
}

static int
query_test_int (const fs::path& path, const std::string& sql,
                std::string& error) {
  sqlite3* database= nullptr;
  if (sqlite3_open (path.string ().c_str (), &database) != SQLITE_OK) {
    error= database ? sqlite3_errmsg (database) : "Could not open test database";
    if (database) sqlite3_close (database);
    return -1;
  }
  sqlite3_stmt* statement= nullptr;
  int result= -1;
  if (sqlite3_prepare_v2 (database, sql.c_str (), -1, &statement, nullptr) !=
      SQLITE_OK)
    error= sqlite3_errmsg (database);
  else if (sqlite3_step (statement) == SQLITE_ROW)
    result= sqlite3_column_int (statement, 0);
  else
    error= sqlite3_errmsg (database);
  sqlite3_finalize (statement);
  sqlite3_close (database);
  return result;
}

static std::string
sql_literal (const std::string& value) {
  char* quoted= sqlite3_mprintf ("%Q", value.c_str ());
  std::string result= quoted ? quoted : "NULL";
  sqlite3_free (quoted);
  return result;
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
  QCOMPARE (records[0].semantic_names,
            std::vector<std::string> (
              {"Every finite rank operator is compact."}));
  QVERIFY (records[0].proof_uuid.rfind ("@order:", 0) == 0);
  QCOMPARE (records[1].type, std::string ("completion"));
  QVERIFY (records[1].semantic_names.empty ());
  QCOMPARE (records[2].origin, std::string ("bold-text"));
  QCOMPARE (records[2].display_text, std::string ("compact operator"));
  QCOMPARE (records[2].semantic_names,
            std::vector<std::string> ({"compact operator"}));
  QVERIFY (records[2].keyword_tree.find ("<strong|compact operator>") !=
           std::string::npos);
  QCOMPARE (records[2].paragraph_offsets, std::vector<int> ({0}));
}

void
TestArtifacts::filtersNonNamesBeforeDefinitionRangeInference () {
  MissingRangeModel noModel;
  tree body (DOCUMENT);
  const std::vector<const char*> filtered= {
    "no", "not",
    "am", "is", "are", "was", "were", "be", "being", "been",
    "do", "does", "did", "doing", "done",
    "have", "has", "had", "having",
    "can", "cannot", "could", "may", "might", "must",
    "shall", "should", "will", "would",
    "ain't", "aren't", "can't", "couldn't", "didn't", "doesn't", "don't",
    "hadn't", "hasn't", "haven't", "isn't", "mightn't", "mustn't",
    "needn't", "shan't", "shouldn't", "wasn't", "weren't", "won't",
    "wouldn't", "CAN"
  };
  for (const char* word: filtered) {
    tree paragraph (CONCAT);
    paragraph << "Context " << compound ("strong", word) << ".";
    body << paragraph;
  }
  tree step (CONCAT);
  step << compound ("strong", "2") << ". Apply the construction.";
  tree concept (CONCAT);
  concept << "A " << compound ("strong", "2-category")
          << " has objects, morphisms, and 2-morphisms.";
  tree predicate (CONCAT);
  predicate << "A map is " << compound ("strong", "continuous")
            << " if inverse images of open sets are open.";
  body << step << concept << predicate;
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);

  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (
              document, "numeric-step.ath", records, error),
            error.c_str ());
  QCOMPARE (records.size (), (size_t) 2);
  QCOMPARE (records[0].origin, std::string ("bold-text"));
  QCOMPARE (records[0].display_text, std::string ("2-category"));
  QCOMPARE (records[1].display_text, std::string ("continuous"));
}

void
TestArtifacts::appliesVaultArtifactTitleFilterIncrementally () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {}, error),
            error.c_str ());
  write_document (root / "Filtered.ath",
                  artifact_test_document ("compact operator"));
  QVERIFY2 (athena_artifact_title_filter_write (
              root, {"compact operator"}, error), error.c_str ());

  AthenaArtifactsBuildResult filtered;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, filtered, error),
            error.c_str ());
  QCOMPARE (filtered.documents_changed, (size_t) 1);
  QCOMPARE (filtered.artifacts, (size_t) 2);

  QVERIFY2 (athena_artifact_title_filter_write (root, {}, error),
            error.c_str ());
  AthenaArtifactsBuildResult restored;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, restored, error),
            error.c_str ());
  QCOMPARE (restored.documents_changed, (size_t) 1);
  QCOMPARE (restored.artifacts, (size_t) 3);
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
TestArtifacts::isolatesStructuredBoldBlocks () {
  MissingRangeModel noModel;
  tree first_body (DOCUMENT);
  first_body << compound ("strong", "Group Theory")
             << compound ("transclude", "", "Group Theory.Aspect", "",
                          "Aspect Abstract Algebra -> Group Theory");
  tree second_body (DOCUMENT);
  second_body << compound ("strong", "Ring Theory")
              << "Unrelated second block.";
  tree body (DOCUMENT);
  body << "A long introductory paragraph that belongs to neither block."
       << compound ("note", first_body)
       << compound ("note", second_body);
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);

  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (
              document, "Scope.ath", records, error), error.c_str ());
  std::vector<const AthenaArtifactRecord*> bold;
  for (const auto& record: records)
    if (record.origin == "bold-text") bold.push_back (&record);
  QCOMPARE (bold.size (), (size_t) 2);
  QCOMPARE (bold[0]->definition_candidates.size (), (size_t) 2);
  QCOMPARE (bold[1]->definition_candidates.size (), (size_t) 2);
  QVERIFY (bold[0]->definition_candidates[0].second.find (
             "Group Theory") != std::string::npos);
  QVERIFY (bold[0]->definition_candidates[1].second.find (
             "Group Theory.Aspect") != std::string::npos);
  QVERIFY (bold[0]->definition_candidates[1].second.find (
             "Unrelated second block") == std::string::npos);

  AthenaArtifactRecord located_record= *bold[0];
  located_record.paragraph_offsets= {0, 1};
  AthenaArtifactParagraphLocation location;
  QVERIFY2 (athena_artifact_locate_paragraph (
              document, located_record, location, error), error.c_str ());
  QVERIFY (location.parent == path (1) * 0);
  QCOMPARE (location.focus_child, 0);
  QCOMPARE (location.first_child, 0);
  QCOMPARE (location.last_child, 1);
}

void
TestArtifacts::locatesStoredParagraphRange () {
  MissingRangeModel noModel;
  tree definition (CONCAT);
  definition << "A map is a " << compound ("strong", "covering map")
             << " when it satisfies the local condition.";
  tree body (DOCUMENT);
  body << "Earlier paragraph."
       << definition
       << compound ("equation*", "p^{-1}(U)=\\bigsqcup_i V_i")
       << "The definition continues here."
       << "A later paragraph in the same segment."
       << compound ("theorem", "A segment boundary.");
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);

  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (document, "located.ath",
                                                records, error),
            error.c_str ());
  auto bold= std::find_if (records.begin (), records.end (), [] (const auto& r) {
    return r.origin == "bold-text";
  });
  QVERIFY (bold != records.end ());
  bold->paragraph_offsets= {0, 1};

  AthenaArtifactParagraphLocation location;
  QVERIFY2 (athena_artifact_locate_paragraph (
              document, *bold, location, error), error.c_str ());
  QCOMPARE (location.focus_child, 1);
  QCOMPARE (location.first_child, 1);
  QCOMPARE (location.last_child, 3);

  bold->paragraph_offsets= {0, 2};
  error.clear ();
  QVERIFY (!athena_artifact_locate_paragraph (
    document, *bold, location, error));
  QCOMPARE (error, std::string ("Artifact paragraph range is not continuous"));
}

void
TestArtifacts::locatesStructuredEnunciationsAfterEdits () {
  tree original= structured_theorem_document ();
  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (
              original, "Structured.ath", records, error), error.c_str ());
  auto theorem= std::find_if (
    records.begin (), records.end (), [] (const auto& record) {
      return record.origin == "enunciation";
    });
  QVERIFY (theorem != records.end ());
  QVERIFY (!is_atomic (extract (original, "body")[0][0]));

  path source_path;
  QVERIFY2 (athena_artifact_locate_source (
              original, *theorem, source_path, error), error.c_str ());
  QCOMPARE (source_path, path (1));

  tree edited= structured_theorem_document (40);
  error.clear ();
  QVERIFY2 (athena_artifact_locate_source (
              edited, *theorem, source_path, error), error.c_str ());
  QCOMPARE (source_path, path (41));

  tree folded= structured_theorem_document (40, true);
  error.clear ();
  QVERIFY2 (athena_artifact_locate_source (
              folded, *theorem, source_path, error), error.c_str ());
  QCOMPARE (source_path, path (40, 1, 1));
}

void
TestArtifacts::excludesOnlyArtifactDefiningOccurrences () {
  MissingRangeModel noModel;
  tree definition (CONCAT);
  definition << "A " << compound ("strong", "middle concept")
             << " is defined here.";
  tree body (DOCUMENT);
  body << "The middle concept is used before its definition."
       << definition
       << "The middle concept is used after its definition.";
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);

  std::vector<AthenaArtifactRecord> records;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (
              document, "Definition.ath", records, error), error.c_str ());
  auto bold= std::find_if (records.begin (), records.end (), [] (const auto& r) {
    return r.origin == "bold-text" && r.display_text == "middle concept";
  });
  QVERIFY (bold != records.end ());
  AthenaArtifactParagraphLocation location;
  QVERIFY2 (athena_artifact_locate_paragraph (
              document, *bold, location, error), error.c_str ());
  QCOMPARE (location.parent, path ());
  QCOMPARE (location.focus_child, 1);
  string serialized_keyword=
    tree_to_texmacs (subtree (body, path (1) * 1));
  QCOMPARE (std::string (as_charp (serialized_keyword), N(serialized_keyword)),
            bold->keyword_tree);
  QVERIFY (athena_artifact_is_defining_occurrence (
    document, path (1) * 1 * 0, *bold));
  QVERIFY (!athena_artifact_is_defining_occurrence (
    document, path (0), *bold));
  QVERIFY (!athena_artifact_is_defining_occurrence (
    document, path (2), *bold));

  tree theorem_document= named_theorem_document (
    "Euler's theorem",
    "Every homogeneous function satisfies Euler's identity.");
  records.clear ();
  error.clear ();
  QVERIFY2 (athena_artifacts_extract_document (
              theorem_document, "Theorem.ath", records, error),
            error.c_str ());
  auto theorem= std::find_if (
    records.begin (), records.end (), [] (const auto& r) {
      return r.origin == "enunciation" &&
             r.anchor_stem == "theorem:Euler's theorem";
    });
  QVERIFY (theorem != records.end ());
  QVERIFY (athena_artifact_is_defining_occurrence (
    theorem_document, path (1) * 0 * 0, *theorem));
  QVERIFY (!athena_artifact_is_defining_occurrence (
    theorem_document, path (0) * 0, *theorem));
}

void
TestArtifacts::excludesEnunciationTitlesFromRadioactiveLinks () {
  tree definition_body (DOCUMENT);
  definition_body << compound ("strong", "Vector space")
                  << "A vector space is a module over a field.";
  tree body (DOCUMENT);
  body << compound ("definition", definition_body);
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);

  path title_path;
  QVERIFY (athena_artifact_enunciation_title_path (
    subtree (body, path (0)), title_path));
  QCOMPARE (title_path, path (0) * 0);
  QVERIFY (athena_artifact_is_enunciation_title (
    document, path (0) * 0 * 0 * 0));
  QVERIFY (!athena_artifact_is_enunciation_title (
    document, path (0) * 0 * 1));

  tree marked=
    athena_artifact_radioactive_suppress_enunciation_titles (body);
  tree title= subtree (marked, path (0) * 0 * 0);
  QVERIFY (is_func (title, WITH, 3));
  QCOMPARE (title[0], tree ("athena-radioactive-links-suppressed"));
  QCOMPARE (title[1], tree ("true"));
  QCOMPARE (title[2], compound ("strong", "Vector space"));
  QCOMPARE (subtree (marked, path (0) * 0 * 1),
            tree ("A vector space is a module over a field."));
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

  size_t selected_ranges= 0;
  AthenaArtifactsBuildOptions options;
  options.range_selector=
    [&] (const std::vector<AthenaArtifactRangeRequest>& requests,
         std::vector<std::vector<int>>& results,
         const AthenaArtifactRangeSelectionProgress&,
         std::string&) {
      selected_ranges += requests.size ();
      results.assign (requests.size (), std::vector<int> ({0}));
      return true;
    };

  AthenaArtifactsBuildResult first;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first, error, options),
            error.c_str ());
  QCOMPARE (first.documents_changed, (size_t) 2);
  QCOMPARE (selected_ranges, (size_t) 2);
  QCOMPARE (first.artifacts, (size_t) 6);
  QVERIFY (fs::exists (root / "indexes/artifacts.db"));
  QVERIFY (fs::exists (root / "indexes/enunciations.db"));
  QVERIFY (fs::exists (root / "indexes/bold-text.db"));

  std::vector<AthenaArtifactRecord> records;
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  QCOMPARE (records.size (), (size_t) 6);
  std::string stable_uuid= records[0].content_uuid;
  AthenaArtifactRecord exact_record;
  bool exact_found= false;
  QVERIFY2 (athena_artifact_query_uuid (
              root, records[0].uuid, exact_record, exact_found, error),
            error.c_str ());
  QVERIFY (exact_found);
  QCOMPARE (exact_record.uuid, records[0].uuid);
  QCOMPARE (exact_record.content_uuid, records[0].content_uuid);
  QVERIFY (!records[0].proof_uuid.empty ());

  AthenaArtifactsBuildResult unchanged;
  QVERIFY2 (athena_artifacts_build (
              root, {}, true, {}, unchanged, error, options),
            error.c_str ());
  QCOMPARE (unchanged.documents_changed, (size_t) 0);
  QCOMPARE (selected_ranges, (size_t) 2);

  QVERIFY2 (athena_artifacts_mark_document_stale (root, "B.ath", error),
            error.c_str ());
  AthenaArtifactsBuildResult stale;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, stale, error, options),
            error.c_str ());
  QCOMPARE (stale.documents_changed, (size_t) 1);
  QCOMPARE (selected_ranges, (size_t) 2);

  write_document (root / "A.ath", artifact_test_document ("compact map"));
  AthenaArtifactsBuildResult modified;
  QVERIFY2 (athena_artifacts_build (
              root, {}, true, {}, modified, error, options),
            error.c_str ());
  QCOMPARE (modified.documents_changed, (size_t) 1);
  QCOMPARE (selected_ranges, (size_t) 3);
  records.clear ();
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  QCOMPARE (records[0].content_uuid, stable_uuid);

  fs::remove (root / "B.ath");
  AthenaArtifactsBuildResult purged;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, purged, error, options),
            error.c_str ());
  QCOMPARE (purged.documents_deleted, (size_t) 1);
  QCOMPARE (selected_ranges, (size_t) 3);
  records.clear ();
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  QCOMPARE (records.size (), (size_t) 3);
  for (const AthenaArtifactRecord& record: records)
    QCOMPARE (record.relative_path, std::string ("A.ath"));
  int deleted_history= query_test_int (
    root / "indexes/artifacts.db",
    "SELECT COUNT(*) FROM artifact_identity_history WHERE path='B.ath' "
    "AND decision='deleted' AND evidence='source-document-removed';", error);
  QVERIFY2 (deleted_history >= 0, error.c_str ());
  QCOMPARE (deleted_history, 3);
}

void
TestArtifacts::preservesAccentedArtifactTextAcrossWorkerAndDatabase () {
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

  string keyword= utf8_to_cork ("Ces\xc3\xa0ro summation");
  string anchor= utf8_to_cork ("H\xc3\xb6lder theorem {");
  string statement= utf8_to_cork ("A th\xc3\xa9or\xc3\xa8me statement.");
  tree body (DOCUMENT);
  body << compound ("label", anchor)
       << compound ("theorem", statement);
  tree paragraph (CONCAT);
  paragraph << "A sequence is " << compound ("strong", keyword) << ".";
  body << paragraph;
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  write_document (root / "Accented.ath", document);

  AthenaArtifactsBuildResult first;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> records;
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  auto found= std::find_if (records.begin (), records.end (), [] (const auto& r) {
    return r.origin == "bold-text";
  });
  QVERIFY (found != records.end ());
  QCOMPARE (found->display_text, std::string ("Ces\xc3\xa0ro summation"));
  auto enunciation= std::find_if (
    records.begin (), records.end (), [] (const auto& r) {
      return r.origin == "enunciation";
    });
  QVERIFY (enunciation != records.end ());
  QCOMPARE (enunciation->anchor_stem, std::string ("H\xc3\xb6lder theorem"));
  QCOMPARE (enunciation->display_text,
            std::string ("A th\xc3\xa9or\xc3\xa8me statement."));
  QCOMPARE (enunciation->semantic_names,
            std::vector<std::string> (
              {"A th\xc3\xa9or\xc3\xa8me statement."}));
  QCOMPARE (found->semantic_names,
            std::vector<std::string> ({"Ces\xc3\xa0ro summation"}));
  string expected_keyword= tree_to_texmacs (compound ("strong", keyword));
  QCOMPARE (found->keyword_tree,
            std::string (as_charp (expected_keyword),
                         (size_t) N(expected_keyword)));

  int utf8_rows= query_test_int (
    root / "indexes/artifacts.db",
    "SELECT COUNT(*) FROM artifacts WHERE hex(display_text)="
    "'436573C3A0726F2073756D6D6174696F6E';", error);
  QVERIFY2 (utf8_rows >= 0, error.c_str ());
  QCOMPARE (utf8_rows, 1);
  int semantic_name_rows= query_test_int (
    root / "indexes/artifacts.db",
    "SELECT COUNT(*) FROM artifact_names WHERE hex(name)="
    "'436573C3A0726F2073756D6D6174696F6E';", error);
  QVERIFY2 (semantic_name_rows >= 0, error.c_str ());
  QCOMPARE (semantic_name_rows, 1);

}

void
TestArtifacts::storesAndDisambiguatesSameNamedArtifacts () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (
    root / "Euler.ath",
    bold_paragraph_document ({{"First source defines ", " one way."},
                              {"A second source defines ", " differently."}},
                             "Euler's theorem"));

  AthenaArtifactsBuildResult built;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, built, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> all;
  QVERIFY2 (athena_artifacts_query (root, all, error), error.c_str ());
  std::vector<AthenaArtifactRecord> same_name;
  for (const AthenaArtifactRecord& record: all)
    if (record.origin == "bold-text" &&
        record.display_text == "Euler's theorem") same_name.push_back (record);
  QCOMPARE (same_name.size (), (size_t) 2);
  QVERIFY (same_name[0].uuid != same_name[1].uuid);
  QVERIFY (same_name[0].content_uuid != same_name[1].content_uuid);
  int stored_names= query_test_int (
    root / info.artifacts_path,
    "SELECT COUNT(*) FROM artifact_names WHERE name='Euler''s theorem';",
    error);
  QVERIFY2 (stored_names >= 0, error.c_str ());
  QCOMPARE (stored_names, 2);

  auto matches= athena_artifact_radioactive_matches_for_records (
    same_name, "Eulerian theorem applies here.");
  QCOMPARE (matches.size (), (size_t) 1);
  QCOMPARE (matches[0].uuids.size (), (size_t) 2);
  QCOMPARE (matches[0].uuids[0], same_name[0].uuid);
  QCOMPARE (matches[0].uuids[1], same_name[1].uuid);
  QCOMPARE (matches[0].disambiguation_key,
            athena_artifact_radioactive_key (same_name[0]));
  std::string target= athena_artifact_radioactive_destination (matches[0]);
  QVERIFY (target.rfind ("tmfs://artifact-disambiguation/", 0) == 0);
  QCOMPARE (target.substr (std::string ("tmfs://artifact-disambiguation/").size ()),
            matches[0].disambiguation_key);

  tree page= athena_artifact_disambiguation_document (same_name, "Pagella");
  string source= tree_to_texmacs (page);
  std::string serialized (as_charp (source), (size_t) N(source));
  QVERIFY (serialized.find ("font|Pagella") != std::string::npos);
  QVERIFY (serialized.find ("tmfs://artifact/" + same_name[0].uuid) !=
           std::string::npos);
  QVERIFY (serialized.find ("tmfs://artifact/" + same_name[1].uuid) !=
           std::string::npos);
  QVERIFY (serialized.find ("Select the intended one") != std::string::npos);
  QVERIFY (serialized.find ("<tabular|") != std::string::npos);
  QVERIFY (serialized.find ("<itemize|") == std::string::npos);
  QVERIFY (serialized.find ("page-medium|automatic") != std::string::npos);
  QVERIFY (serialized.find ("table-hmode|exact") != std::string::npos);
  QVERIFY (serialized.find ("cell-width|16em") != std::string::npos);
  QVERIFY2 (serialized.find (
              "<associate|athena-radioactive-links-suppressed|true>") !=
            std::string::npos, serialized.c_str ());
  QVERIFY (serialized.find ("<samp|") != std::string::npos);
}

void
TestArtifacts::navigatesArtifactAndLoadsDisambiguationPage () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.filePath ("vault").toStdString ());
  QVERIFY (fs::create_directories (root));

  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (root / "Banach.ath", structured_theorem_document ());
  write_document (
    root / "Euler functions.ath",
    named_theorem_document (
      "Euler's theorem",
      "Every homogeneous function satisfies Euler's identity."));
  write_document (
    root / "Euler polyhedra.ath",
    named_theorem_document (
      "Euler's theorem",
      "Every convex polyhedron satisfies V minus E plus F equals two."));
  tree middleDefinition (CONCAT);
  middleDefinition << "A " << compound ("strong", "middle concept")
                   << " is defined in the middle of this document.";
  tree middleBody (DOCUMENT);
  middleBody << "First paragraph."
             << "Second paragraph."
             << middleDefinition
             << "Last paragraph.";
  tree middleDocument (DOCUMENT);
  middleDocument << compound ("TeXmacs", "2.1.4")
                 << compound ("style", "generic")
                 << compound ("body", middleBody);
  write_document (root / "Middle.ath", middleDocument);
  tree demoBody (DOCUMENT);
  demoBody << "Runtime navigation fixture.";
  tree demoDocument (DOCUMENT);
  demoDocument << compound ("TeXmacs", "2.1.4")
               << compound ("style", "generic")
               << compound ("body", demoBody);
  write_document (root / "Demo.ath", demoDocument);

  AthenaArtifactsBuildResult built;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, built, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> records;
  QVERIFY2 (athena_artifacts_query (root, records, error), error.c_str ());
  const AthenaArtifactRecord* banach= nullptr;
  const AthenaArtifactRecord* middle= nullptr;
  std::vector<AthenaArtifactRecord> euler;
  for (const AthenaArtifactRecord& record: records) {
    if (record.relative_path == "Banach.ath" &&
        record.origin == "enunciation")
      banach= &record;
    if (record.origin == "bold-text" && record.display_text == "middle concept")
      middle= &record;
    if (record.anchor_stem == "theorem:Euler's theorem")
      euler.push_back (record);
  }
  QVERIFY (banach != nullptr);
  QVERIFY (middle != nullptr);
  QCOMPARE (euler.size (), (size_t) 2);
  path banachSource;
  QVERIFY2 (athena_artifact_locate_source (
              structured_theorem_document (80), *banach,
              banachSource, error), error.c_str ());
  QCOMPARE (banachSource, path (81));
  write_document (root / "Banach.ath", structured_theorem_document (80));
  QString disambiguationUrl= QString::fromStdString (
    "tmfs://artifact-disambiguation/" +
    athena_artifact_radioactive_key (euler.front ()));

  QString home= temporary.filePath ("home");
  QVERIFY (QDir ().mkpath (home + "/progs"));
  QVERIFY (QDir ().mkpath (home + "/fonts"));
  QVERIFY (QDir ().mkpath (home + "/system"));
  QString resultPath= temporary.filePath ("navigation-result.txt");
  QString sourcePath= QString::fromStdString ((root / "Banach.ath").string ());
  QString middlePath= QString::fromStdString ((root / "Middle.ath").string ());
  QString demoPath= QString::fromStdString ((root / "Demo.ath").string ());
  QString uniqueUrl= QString::fromStdString ("tmfs://artifact/" + banach->uuid);
  QString middleUrl= QString::fromStdString ("tmfs://artifact/" + middle->uuid);

  QString script= QString (R"SCM(
(set-preference "check for updates" "off")
(delayed (:pause 1000)
  (begin
    (load-vault-dir (system->url %1))
    (delayed (:pause 500)
      (begin
        (load-buffer (system->url %2))
        (let ((before-y (get-scroll-y)))
          (go-to-url %3)
          (string-save
            (string-append
              "immediate-buffer="
              (if (== (url->system (current-buffer)) %2) "1" "0")
              "\n")
            (system->url %4))
          (delayed (:pause 1200)
            (begin
            (string-save
              (string-append
                (string-load (system->url %4))
                "same-buffer="
                (if (== (url->system (current-buffer)) %2) "1" "0")
                "\ncurrent-buffer=" (url->system (current-buffer))
                "\nsame-position="
                (let* ((matches
                         (tree-search-indices
                           (buffer-tree)
                           (lambda (node)
                             (and (tree-compound? node)
                                  (== (tree-label node) 'theorem)))))
                       (expected
                         (and (pair? matches)
                              (append (tree->path (buffer-tree))
                                      (car matches)))))
                  (if (and expected
                           (list-starts? (cursor-path) expected))
                      "1" "0"))
                "\nsame-cursor=" (object->string (cursor-path))
                "\nsame-expected="
                (let* ((matches
                         (tree-search-indices
                           (buffer-tree)
                           (lambda (node)
                             (and (tree-compound? node)
                                  (== (tree-label node) 'theorem)))))
                       (expected
                         (and (pair? matches)
                              (append (tree->path (buffer-tree))
                                      (car matches)))))
                  (object->string expected))
                "\ncursor-accessible="
                (if (cursor-accessible?) "1" "0")
                "\nscroll-moved="
                (if (!= (get-scroll-y) before-y) "1" "0")
                "\n")
              (system->url %4))
            (load-buffer (system->url %5))
            (go-to-url %6)
            (delayed (:pause 1200)
              (begin
                (string-save
                  (string-append
                    (string-load (system->url %4))
                    "disambiguation-buffer="
                    (if (and (== (url->system (current-buffer)) %6)
                             (buffer-exists? (system->url %6)))
                        "1" "0")
                    "\n")
                  (system->url %4))
                (go-to-url %8)
                (string-save
                  (string-append
                    (string-load (system->url %4))
                    "candidate-immediate-buffer="
                    (if (== (url->system (current-buffer)) %7) "1" "0")
                    "\n")
                  (system->url %4))
                (delayed (:pause 1200)
                  (begin
                    (let* ((matches
                             (tree-search-indices
                               (buffer-tree)
                               (lambda (node)
                                 (and (tree-compound? node)
                                      (== (tree-label node) 'strong)
                                      (== (tree->string (tree-ref node 0))
                                          "middle concept")))))
                           (keyword-path (and (pair? matches) (car matches)))
                           (paragraph-path
                             (and keyword-path
                                  (list-head keyword-path
                                             (- (length keyword-path) 1))))
                           (expected
                             (and paragraph-path
                                  (append (tree->path (buffer-tree))
                                          paragraph-path))))
                      (string-save
                        (string-append
                          (string-load (system->url %4))
                          "middle-buffer="
                          (if (== (url->system (current-buffer)) %7) "1" "0")
                          "\nmiddle-position="
                          (if (and expected
                                   (list-starts? (cursor-path) expected))
                              "1" "0")
                          "\nmiddle-cursor=" (object->string (cursor-path))
                          "\nmiddle-expected=" (object->string expected)
                          "\n")
                        (system->url %4)))
                    (quit-TeXmacs))))))))))))
)SCM")
    .arg (scheme_quote (QString::fromStdString (root.string ())))
    .arg (scheme_quote (sourcePath))
    .arg (scheme_quote (uniqueUrl))
    .arg (scheme_quote (resultPath))
    .arg (scheme_quote (demoPath))
    .arg (scheme_quote (disambiguationUrl))
    .arg (scheme_quote (middlePath))
    .arg (scheme_quote (middleUrl));
  QFile init (home + "/progs/my-init-texmacs.scm");
  QVERIFY (init.open (QIODevice::WriteOnly | QIODevice::Text));
  QByteArray initBytes= script.toUtf8 ();
  QCOMPARE (init.write (initBytes), (qint64) initBytes.size ());
  init.close ();

  QString executable=
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../src/ATHENA.bin");
  QVERIFY2 (QFile::exists (executable), qPrintable (executable));
  QString launcherDirectory= temporary.filePath ("bin");
  QVERIFY (QDir ().mkpath (launcherDirectory));
  QString launcher= launcherDirectory + "/ATHENA";
  QVERIFY2 (QFile::link (executable, launcher), qPrintable (launcher));
  QProcess process;
  QProcessEnvironment environment= QProcessEnvironment::systemEnvironment ();
  environment.insert (
    "ATHENA_PATH",
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../../ATHENA"));
  QString executableDirectory= QFileInfo (executable).absolutePath ();
  environment.insert ("ATHENA_BIN_PATH", executableDirectory);
  environment.insert (
    "PATH", launcherDirectory + ':' + environment.value ("PATH"));
  environment.insert ("ATHENA_HOME_PATH", home);
  environment.insert ("QT_QPA_PLATFORM", "offscreen");
  environment.insert ("TM_REEXEC", "1");
  process.setProcessEnvironment (environment);
  process.setProgram (launcher);
  process.setArguments (
    {"--no-splash-screen", "--platform", "offscreen", demoPath});
  process.start ();
  QVERIFY2 (process.waitForFinished (30000), qPrintable (process.errorString ()));

  QByteArray output= process.readAllStandardOutput () +
                     process.readAllStandardError ();
  QByteArray diagnostic;
  for (const QByteArray& line: output.split ('\n'))
    if (!line.contains ("approximating font")) diagnostic += line + '\n';
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QFile result (resultPath);
  QVERIFY2 (result.open (QIODevice::ReadOnly | QIODevice::Text),
            diagnostic.right (8192).constData ());
  QByteArray assertions= result.readAll ();
  QByteArray navigationDiagnostic= assertions + '\n' + diagnostic.right (8192);
  QVERIFY2 (assertions.contains ("immediate-buffer=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("same-buffer=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("same-position=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("cursor-accessible=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("scroll-moved=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("disambiguation-buffer=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("candidate-immediate-buffer=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("middle-buffer=1"),
            navigationDiagnostic.constData ());
  QVERIFY2 (assertions.contains ("middle-position=1"),
            navigationDiagnostic.constData ());
}

void
TestArtifacts::keepsRadioactiveRangeMacroLociAlive () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.filePath ("vault").toStdString ());
  QVERIFY (fs::create_directories (root));

  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (
    root / "Euler.ath",
    named_theorem_document (
      "Euler's theorem",
      "Every homogeneous function satisfies Euler's identity."));
  tree demoBody (DOCUMENT);
  demoBody << compound ("range", "Eulerian theorem has several meanings.",
                        "0", "38");
  tree demoDocument (DOCUMENT);
  demoDocument << compound ("TeXmacs", "2.1.4")
               << compound ("style", "generic")
               << compound ("body", demoBody);
  write_document (root / "Demo.ath", demoDocument);

  AthenaArtifactsBuildResult built;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, built, error),
            error.c_str ());

  QString home= temporary.filePath ("home");
  QVERIFY (QDir ().mkpath (home + "/progs"));
  QVERIFY (QDir ().mkpath (home + "/fonts"));
  QVERIFY (QDir ().mkpath (home + "/system"));
  QString resultPath= temporary.filePath ("range-result.txt");
  QString demoPath= QString::fromStdString ((root / "Demo.ath").string ());
  QString script= QString (R"SCM(
(set-preference "check for updates" "off")
(delayed (:pause 500)
  (begin
    (load-vault-dir (system->url %1))
    (delayed (:pause 300)
      (begin
        (load-buffer (system->url %2))
        (delayed (:pause 1200)
          (begin
            (string-save "range-locus-alive=1\n" (system->url %3))
            (quit-TeXmacs)))))))
)SCM")
    .arg (scheme_quote (QString::fromStdString (root.string ())))
    .arg (scheme_quote (demoPath))
    .arg (scheme_quote (resultPath));
  QFile init (home + "/progs/my-init-texmacs.scm");
  QVERIFY (init.open (QIODevice::WriteOnly | QIODevice::Text));
  QCOMPARE (init.write (script.toUtf8 ()), (qint64) script.toUtf8 ().size ());
  init.close ();

  QString executable=
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../src/ATHENA.bin");
  QString launcherDirectory= temporary.filePath ("bin");
  QVERIFY (QDir ().mkpath (launcherDirectory));
  QString launcher= launcherDirectory + "/ATHENA";
  QVERIFY2 (QFile::link (executable, launcher), qPrintable (launcher));
  QProcess process;
  QProcessEnvironment environment= QProcessEnvironment::systemEnvironment ();
  environment.insert (
    "ATHENA_PATH",
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../../ATHENA"));
  environment.insert ("ATHENA_BIN_PATH", QFileInfo (executable).absolutePath ());
  environment.insert ("PATH", launcherDirectory + ':' + environment.value ("PATH"));
  environment.insert ("ATHENA_HOME_PATH", home);
  environment.insert ("QT_QPA_PLATFORM", "offscreen");
  environment.insert ("TM_REEXEC", "1");
  process.setProcessEnvironment (environment);
  process.setProgram (launcher);
  process.setArguments ({"--no-splash-screen", "--platform", "offscreen"});
  process.start ();
  QVERIFY2 (process.waitForFinished (30000), qPrintable (process.errorString ()));
  QByteArray diagnostic= process.readAllStandardOutput () +
                         process.readAllStandardError ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QVERIFY2 (!diagnostic.contains ("The required path does not exist"),
            diagnostic.right (8192).constData ());
  QFile result (resultPath);
  QVERIFY2 (result.open (QIODevice::ReadOnly | QIODevice::Text),
            diagnostic.right (8192).constData ());
  QVERIFY (result.readAll ().contains ("range-locus-alive=1"));
}

void
TestArtifacts::preservesBoldIdentityWhenDuplicateIsInsertedBefore () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (
    root / "A.ath",
    bold_paragraph_document ({{"First host defines ", " precisely."},
                              {"Second host applies ", " elsewhere."}}));
  AthenaArtifactsBuildResult first_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  QCOMPARE (before.size (), (size_t) 2);
  std::string first_content_uuid= before[0].content_uuid;
  std::string first_artifact_uuid= before[0].uuid;
  std::string second_content_uuid= before[1].content_uuid;
  std::string second_artifact_uuid= before[1].uuid;

  write_document (
    root / "A.ath",
    bold_paragraph_document ({{"Inserted host mentions ", " newly."},
                              {"First host defines ", " precisely."},
                              {"Second host applies ", " elsewhere."}}));
  AthenaArtifactsBuildResult second_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, second_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), (size_t) 3);
  QCOMPARE (after[1].content_uuid, first_content_uuid);
  QCOMPARE (after[1].uuid, first_artifact_uuid);
  QCOMPARE (after[2].content_uuid, second_content_uuid);
  QCOMPARE (after[2].uuid, second_artifact_uuid);
  QVERIFY (after[0].content_uuid != first_content_uuid);
  QVERIFY (after[0].content_uuid != second_content_uuid);
  QCOMPARE (after[0].identity_decision, std::string ("new"));
}

void
TestArtifacts::doesNotTransferDeletedDuplicateToInsertedDuplicate () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (
    root / "A.ath",
    bold_paragraph_document ({{"Old removed host uses ", " once."},
                              {"Persistent host uses ", " twice."}}));
  AthenaArtifactsBuildResult first_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  QCOMPARE (before.size (), (size_t) 2);
  std::string removed_uuid= before[0].content_uuid;
  std::string persistent_uuid= before[1].content_uuid;

  write_document (
    root / "A.ath",
    bold_paragraph_document ({{"Persistent host uses ", " twice."},
                              {"Unrelated inserted host uses ", " later."}}));
  AthenaArtifactsBuildResult second_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, second_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), (size_t) 2);
  QCOMPARE (after[0].content_uuid, persistent_uuid);
  QVERIFY (after[1].content_uuid != removed_uuid);
  QVERIFY (after[1].content_uuid != persistent_uuid);
  QCOMPARE (after[1].identity_decision, std::string ("new"));
}

void
TestArtifacts::rejectsIdentityTransferWhenParagraphIsCopiedExactly () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  auto paragraph= std::make_pair (std::string ("A copied host defines "),
                                  std::string (" exactly."));
  write_document (root / "A.ath", bold_paragraph_document ({paragraph}));
  AthenaArtifactsBuildResult first_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  QCOMPARE (before.size (), (size_t) 1);
  std::string old_content_uuid= before[0].content_uuid;
  std::string old_artifact_uuid= before[0].uuid;

  write_document (root / "A.ath",
                  bold_paragraph_document ({paragraph, paragraph}));
  AthenaArtifactsBuildResult second_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, second_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), (size_t) 2);
  for (const AthenaArtifactRecord& record: after) {
    QVERIFY (record.content_uuid != old_content_uuid);
    QVERIFY (record.uuid != old_artifact_uuid);
    QCOMPARE (record.identity_decision, std::string ("ambiguous"));
  }
}

void
TestArtifacts::preservesExactDuplicateIdentityGroupByDocumentOrder () {
  auto observation= [] (const char* uuid, int order) {
    AthenaArtifactIdentityObservation value;
    value.uuid= uuid;
    value.origin= "bold-text";
    value.type= "definition";
    value.focus= "identical keyword";
    value.host= "The identical host defines identical keyword.";
    value.before= "same left context";
    value.after= "same right context";
    value.display= "identical keyword";
    value.document_order= order;
    return value;
  };
  std::vector<AthenaArtifactIdentityObservation> old_values= {
    observation ("first-uuid", 4), observation ("second-uuid", 9)};
  std::vector<AthenaArtifactIdentityObservation> new_values= {
    observation ("", 14), observation ("", 19)};
  old_values[0].display= old_values[1].display= "<#6982><#5FF5>";
  new_values[0].display= new_values[1].display= "concept";

  AthenaArtifactIdentityResult result=
    athena_artifact_associate_identities (old_values, new_values);
  QCOMPARE (result.decisions.size (), (size_t) 2);
  QCOMPARE (result.decisions[0].kind,
            AthenaArtifactIdentityDecisionKind::Matched);
  QCOMPARE (result.decisions[0].old_index, 0);
  QCOMPARE (result.decisions[0].evidence,
            std::string ("exact-duplicate-group-order"));
  QCOMPARE (result.decisions[1].kind,
            AthenaArtifactIdentityDecisionKind::Matched);
  QCOMPARE (result.decisions[1].old_index, 1);
  QVERIFY (result.deleted_old_indices.empty ());
}

void
TestArtifacts::preservesUniqueBoldIdentityAcrossHostEdit () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (
    root / "A.ath",
    bold_paragraph_document ({{"Original wording for ", " here."}}));
  AthenaArtifactsBuildResult first_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  QCOMPARE (before.size (), (size_t) 1);

  write_document (
    root / "A.ath",
    bold_paragraph_document ({{"Substantially revised wording for ",
                               " in the edited paragraph."}}));
  AthenaArtifactsBuildResult second_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, second_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), (size_t) 1);
  QCOMPARE (after[0].content_uuid, before[0].content_uuid);
  QCOMPARE (after[0].uuid, before[0].uuid);
  QCOMPARE (after[0].identity_evidence, std::string ("unique-focus"));
}

void
TestArtifacts::preservesUnanchoredEnunciationsWhenOneIsInsertedBefore () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  tree first_body (DOCUMENT);
  first_body << compound ("theorem", "The first stable statement.")
             << compound ("lemma", "The second stable statement.");
  tree first_document (DOCUMENT);
  first_document << compound ("TeXmacs", "2.1.4")
                 << compound ("style", "generic")
                 << compound ("body", first_body);
  write_document (root / "A.ath", first_document);
  AthenaArtifactsBuildResult first_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  const AthenaArtifactRecord* theorem_before=
    record_with_display_text (before, "The first stable statement.");
  const AthenaArtifactRecord* lemma_before=
    record_with_display_text (before, "The second stable statement.");
  QVERIFY (theorem_before);
  QVERIFY (lemma_before);
  std::string theorem_uuid= theorem_before->content_uuid;
  std::string lemma_uuid= lemma_before->content_uuid;

  tree second_body (DOCUMENT);
  second_body << compound ("proposition", "A newly inserted statement.")
              << compound ("theorem", "The first stable statement.")
              << compound ("lemma", "The second stable statement.");
  tree second_document (DOCUMENT);
  second_document << compound ("TeXmacs", "2.1.4")
                  << compound ("style", "generic")
                  << compound ("body", second_body);
  write_document (root / "A.ath", second_document);
  AthenaArtifactsBuildResult second_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, second_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  const AthenaArtifactRecord* theorem_after=
    record_with_display_text (after, "The first stable statement.");
  const AthenaArtifactRecord* lemma_after=
    record_with_display_text (after, "The second stable statement.");
  QVERIFY (theorem_after);
  QVERIFY (lemma_after);
  QCOMPARE (theorem_after->content_uuid, theorem_uuid);
  QCOMPARE (lemma_after->content_uuid, lemma_uuid);
}

void
TestArtifacts::doesNotTransferDeletedUnanchoredDuplicateEnunciation () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  tree first_body (DOCUMENT);
  first_body << "Removed left context."
             << compound ("theorem", "A duplicated statement.")
             << "Removed right context."
             << "Persistent left context."
             << compound ("theorem", "A duplicated statement.")
             << "Persistent right context.";
  tree first_document (DOCUMENT);
  first_document << compound ("TeXmacs", "2.1.4")
                 << compound ("style", "generic")
                 << compound ("body", first_body);
  write_document (root / "A.ath", first_document);
  AthenaArtifactsBuildResult first_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, first_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  QCOMPARE (before.size (), (size_t) 2);
  std::string removed_uuid= before[0].content_uuid;
  std::string persistent_uuid= before[1].content_uuid;

  tree second_body (DOCUMENT);
  second_body << "Persistent left context."
              << compound ("theorem", "A duplicated statement.")
              << "Persistent right context."
              << "Inserted left context."
              << compound ("theorem", "A duplicated statement.")
              << "Inserted right context.";
  tree second_document (DOCUMENT);
  second_document << compound ("TeXmacs", "2.1.4")
                  << compound ("style", "generic")
                  << compound ("body", second_body);
  write_document (root / "A.ath", second_document);
  AthenaArtifactsBuildResult second_build;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, second_build, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), (size_t) 2);
  QCOMPARE (after[0].content_uuid, persistent_uuid);
  QVERIFY (after[1].content_uuid != removed_uuid);
  QVERIFY (after[1].content_uuid != persistent_uuid);
  QCOMPARE (after[1].identity_decision, std::string ("new"));
}

void
TestArtifacts::preservesIdentityAcrossTrustedPathRenames () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  fs::create_directories (root / "Old");
  write_document (
    root / "Old/A.ath", artifact_test_document ("compact operator"));
  write_document (
    root / "Old/B.ath", artifact_test_document ("Fredholm operator"));

  AthenaArtifactsBuildResult initial;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, initial, error),
            error.c_str ());
  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  std::map<std::string,std::string> uuid_by_key;
  for (const AthenaArtifactRecord& record: before)
    uuid_by_key[record.relative_path + char (31) + record.display_text]=
      record.uuid;

  fs::rename (root / "Old", root / "New");
  QVERIFY2 (athena_artifacts_apply_path_rename (
              root, "Old", "New", true, error), error.c_str ());
  fs::rename (root / "New/A.ath", root / "New/Renamed.ath");
  QVERIFY2 (athena_artifacts_apply_path_rename (
              root, "New/A.ath", "New/Renamed.ath", false, error),
            error.c_str ());

  AthenaArtifactsBuildResult rebuilt;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, rebuilt, error),
            error.c_str ());
  QCOMPARE (rebuilt.documents_changed, (size_t) 0);
  QCOMPARE (rebuilt.documents_deleted, (size_t) 0);
  std::vector<AthenaArtifactRecord> after;
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), before.size ());
  for (const AthenaArtifactRecord& record: after) {
    std::string old_path= record.relative_path == "New/Renamed.ath" ?
      "Old/A.ath" : "Old/B.ath";
    std::string key= old_path + char (31) + record.display_text;
    QVERIFY (uuid_by_key.count (key));
    QCOMPARE (record.uuid, uuid_by_key[key]);
  }
  AthenaArtifactRecord exact;
  bool found= false;
  QVERIFY2 (athena_artifact_query_uuid (
              root, after.front ().uuid, exact, found, error), error.c_str ());
  QVERIFY (found);
  QVERIFY (exact.relative_path == "New/Renamed.ath" ||
           exact.relative_path == "New/B.ath");
}

static AthenaArtifactRecord
radioactive_record (const char* uuid, const char* term,
                    const char* origin= "bold-text") {
  AthenaArtifactRecord record;
  record.uuid= uuid;
  record.origin= origin;
  record.display_text= term;
  record.semantic_names= {term};
  if (record.origin != "bold-text") {
    record.type= "provable";
    record.anchor_stem= std::string ("theorem:") + term;
  }
  return record;
}

void
TestArtifacts::matchesRadioactiveLinksByCaseAndInflection () {
  std::vector<AthenaArtifactRecord> records= {
    radioactive_record ("compact-operator", "compact operator")};
  string text= "COMPACT OPERATORS and compact operator";
  auto matches= athena_artifact_radioactive_matches_for_records (records, text);
  QCOMPARE (matches.size (), (size_t) 2);
  QCOMPARE (matches[0].uuids, std::vector<std::string> ({"compact-operator"}));
  QCOMPARE (text (matches[0].start, matches[0].end),
            string ("COMPACT OPERATORS"));
  QCOMPARE (text (matches[1].start, matches[1].end),
            string ("compact operator"));
}

void
TestArtifacts::matchesPossessiveAndEponymRadioactiveLinks () {
  std::vector<AthenaArtifactRecord> records= {
    radioactive_record ("euler", "Euler theorem"),
    radioactive_record ("noether", "Noether space"),
    radioactive_record ("artin", "Artin ring"),
    radioactive_record ("gauss", "Gauss measure"),
    radioactive_record ("lagrange", "Lagrange identity")};
  string text= utf8_to_cork (
    "Euler's theorem; EULERIAN THEOREM; Euler\xE2\x80\x99s theorem. "
    "Noetherian spaces and Noether's space. Artinian rings. "
    "Gaussian measures. Lagrangian identities.");
  auto matches= athena_artifact_radioactive_matches_for_records (records, text);
  QCOMPARE (matches.size (), (size_t) 8);
  std::vector<std::string> expected= {
    "euler", "euler", "euler", "noether", "noether", "artin",
    "gauss", "lagrange"};
  QCOMPARE (matches.size (), expected.size ());
  for (size_t i=0; i<matches.size (); i++)
    QCOMPARE (matches[i].uuids, std::vector<std::string> ({expected[i]}));

  std::vector<AthenaArtifactRecord> ordinary= {
    radioactive_record ("median", "median")};
  auto false_match= athena_artifact_radioactive_matches_for_records (
    ordinary, "med");
  QVERIFY (false_match.empty ());
}

void
TestArtifacts::prefersLongestRadioactiveArtifactTerm () {
  std::vector<AthenaArtifactRecord> records= {
    radioactive_record ("operator", "operator"),
    radioactive_record ("compact-operator", "compact operator")};
  string text= "A compact operator is an operator.";
  auto matches= athena_artifact_radioactive_matches_for_records (records, text);
  QCOMPARE (matches.size (), (size_t) 2);
  QCOMPARE (matches[0].uuids,
            std::vector<std::string> ({"compact-operator"}));
  QCOMPARE (matches[1].uuids, std::vector<std::string> ({"operator"}));
}

void
TestArtifacts::linksAmbiguousRadioactiveArtifactTerms () {
  std::vector<AthenaArtifactRecord> records= {
    radioactive_record ("first", "Banach space"),
    radioactive_record ("second", "banach spaces")};
  auto matches= athena_artifact_radioactive_matches_for_records (
    records, "A Banach space is complete.");
  QCOMPARE (matches.size (), (size_t) 1);
  QCOMPARE (matches[0].uuids,
            std::vector<std::string> ({"first", "second"}));
  QCOMPARE (matches[0].disambiguation_key.size (), (size_t) 64);
  QCOMPARE (athena_artifact_radioactive_destination (matches[0]),
            std::string ("tmfs://artifact-disambiguation/") +
              matches[0].disambiguation_key);
}

void
TestArtifacts::namesEnunciationsStrictlyAndSkipsCompletions () {
  MissingRangeModel noModel;
  AthenaArtifactRecord named= radioactive_record (
    "lagrange", "(Lagrange) Every finite group has a useful subgroup.",
    "enunciation");
  named.anchor_stem= "theorem:Let";
  named.semantic_names= {"Lagrange"};
  AthenaArtifactRecord unnamed= radioactive_record (
    "unnamed", "Let X be a compact Hausdorff space.", "enunciation");
  unnamed.anchor_stem= "theorem:Let";
  AthenaArtifactRecord proof= radioactive_record (
    "proof", "(Lagrange) Apply the orbit-stabilizer theorem.",
    "enunciation");
  proof.type= "completion";
  proof.anchor_stem= "proof:Lagrange";
  proof.semantic_names.clear ();
  AthenaArtifactRecord dated= radioactive_record (
    "einstein", "(Einstein, 2026) A revised field equation.",
    "enunciation");
  dated.semantic_names= {"Einstein, 2026", "Einstein"};
  AthenaArtifactRecord we_have= radioactive_record (
    "we-have", "We have a natural isomorphism between these functors.",
    "enunciation");
  we_have.anchor_stem= "theorem:We have";

  QCOMPARE (cork_to_utf8 (athena_artifact_radioactive_name (named)),
            string ("Lagrange"));
  QCOMPARE (cork_to_utf8 (athena_artifact_radioactive_name (unnamed)),
            string ("Let X be a compact Hausdorff space."));
  QVERIFY (athena_artifact_radioactive_name (proof) == "");
  QCOMPARE (cork_to_utf8 (athena_artifact_radioactive_name (dated)),
            string ("Einstein, 2026"));
  QCOMPARE (cork_to_utf8 (athena_artifact_radioactive_name (we_have)),
            string ("We have a natural isomorphism between these functors."));

  tree forensic_body (DOCUMENT);
  forensic_body
    << compound ("label", "question:Show that {")
    << compound ("question", "Show that U(H) is isomorphic to SU(2).")
    << compound ("label", "question:Show that }")
    << compound ("label", "question:52297528 {")
    << compound ("question", "A complete non-Latin mathematical statement.")
    << compound ("label", "question:52297528 }")
    << compound ("label", "theorem:Einstein 2026 {")
    << compound ("theorem", compound (
         "strong", "(Einstein, 2026) A revised field equation."))
    << compound ("label", "theorem:Einstein 2026 }")
    << compound ("label", "theorem:X d {")
    << compound ("theorem", "(X,d) is a complete metric space.")
    << compound ("label", "theorem:X d }")
    << compound ("label", "question {")
    << compound ("question", compound (
         "big-figure", compound ("image", "assets/problem.png", "600px",
                                  "", "", ""), ""))
    << compound ("label", "question }");
  tree forensic_document (DOCUMENT);
  forensic_document << compound ("TeXmacs", "2.1.4")
                    << compound ("style", "generic")
                    << compound ("body", forensic_body);
  std::vector<AthenaArtifactRecord> forensic_records;
  std::string forensic_error;
  QVERIFY2 (athena_artifacts_extract_document (
              forensic_document, "forensic.ath", forensic_records,
              forensic_error), forensic_error.c_str ());
  QCOMPARE (forensic_records.size (), (size_t) 4);
  QCOMPARE (forensic_records[0].anchor_stem, std::string ("question:Show that"));
  QCOMPARE (forensic_records[0].semantic_names,
            std::vector<std::string> (
              {"Show that U(H) is isomorphic to SU(2)."}));
  QCOMPARE (forensic_records[1].anchor_stem, std::string ("question:52297528"));
  QCOMPARE (forensic_records[1].semantic_names,
            std::vector<std::string> (
              {"A complete non-Latin mathematical statement."}));
  QCOMPARE (forensic_records[2].semantic_names,
            std::vector<std::string> ({"Einstein, 2026", "Einstein"}));
  QCOMPARE (forensic_records[3].semantic_names,
            std::vector<std::string> (
              {"(X,d) is a complete metric space."}));
  for (const AthenaArtifactRecord& record: forensic_records)
    QVERIFY (record.anchor_stem != "question");

  std::vector<AthenaArtifactRecord> records= {
    named, unnamed, proof, dated, we_have};
  auto matches= athena_artifact_radioactive_matches_for_records (
    records,
    "Lagrangian results apply. Let is not a title. "
    "Let X be a compact Hausdorff space. Einstein made a proposal; "
    "Einstein, 2026 is the revised version. "
    "We have a natural isomorphism between these functors.");
  QCOMPARE (matches.size (), (size_t) 5);
  QCOMPARE (matches[0].uuids, std::vector<std::string> ({"lagrange"}));
  QCOMPARE (matches[1].uuids, std::vector<std::string> ({"unnamed"}));
  QCOMPARE (matches[2].uuids, std::vector<std::string> ({"einstein"}));
  QCOMPARE (matches[3].uuids, std::vector<std::string> ({"einstein"}));
  QCOMPARE (matches[4].uuids, std::vector<std::string> ({"we-have"}));
  for (const auto& match: matches)
    QVERIFY (std::find (match.uuids.begin (), match.uuids.end (), "proof") ==
             match.uuids.end ());

  std::string long_statement= "A theorem whose complete statement is ";
  long_statement.append (400, 'x');
  tree body (DOCUMENT);
  body << compound ("label", "theorem:A theorem {")
       << compound ("theorem", long_statement.c_str ());
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  std::vector<AthenaArtifactRecord> extracted;
  std::string error;
  QVERIFY2 (athena_artifacts_extract_document (
              document, "long.ath", extracted, error), error.c_str ());
  auto theorem= std::find_if (
    extracted.begin (), extracted.end (), [] (const auto& record) {
      return record.origin == "enunciation";
    });
  QVERIFY (theorem != extracted.end ());
  QCOMPARE (theorem->display_text, long_statement);
  QCOMPARE (theorem->semantic_names,
            std::vector<std::string> ({long_statement}));
}

void
TestArtifacts::preservesUnicodeRadioactiveMatchOffsets () {
  std::vector<AthenaArtifactRecord> records= {
    radioactive_record ("frechet", "Fr<#e9>chet theorem", "enunciation")};
  string text= utf8_to_cork ("The FR\xC3\x89" "CHET theorems apply.");
  auto matches= athena_artifact_radioactive_matches_for_records (records, text);
  QCOMPARE (matches.size (), (size_t) 1);
  QCOMPARE (cork_to_utf8 (text (matches[0].start, matches[0].end)),
            string ("FR\xC3\x89" "CHET theorems"));
}

void
TestArtifacts::matchesLargeRadioactiveArtifactIndexWithinBudget () {
  std::vector<AthenaArtifactRecord> records;
  records.reserve (5000);
  for (int i=0; i<5000; i++) {
    std::string suffix= std::to_string (i);
    records.push_back (radioactive_record (
      ("artifact-" + suffix).c_str (),
      ("semantic concept " + suffix).c_str ()));
  }
  string text;
  for (int i=0; i<1000; i++)
    text << "Ordinary prose without an artifact match. ";
  text << "SEMANTIC CONCEPT 4999";

  QElapsedTimer timer;
  timer.start ();
  auto matches= athena_artifact_radioactive_matches_for_records (records, text);
  qint64 elapsed= timer.elapsed ();
  QCOMPARE (matches.size (), (size_t) 1);
  QCOMPARE (matches[0].uuids,
            std::vector<std::string> ({"artifact-4999"}));
  QVERIFY2 (elapsed < 3000,
            qPrintable (QString ("Large radioactive-link match took %1 ms")
                          .arg (elapsed)));
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

void
TestArtifacts::excludesExternalResourcesFromDefinitionRangeRequests () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  tree paragraph (CONCAT);
  paragraph << "The " << compound ("strong", "heat ball")
            << " controls the mean-value formula."
            << compound ("image", "https://i.sstatic.net/vxyGk.png", "373px",
                         "", "", "");
  tree body (DOCUMENT);
  body << paragraph
       << compound (
            "big-figure",
            compound ("image", "https://i.sstatic.net/vxyGk.png", "373px",
                      "", "", ""),
            "A heat ball.");
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  write_document (root / "Remote image.ath", document);

  size_t requests_seen= 0;
  bool external_resource_seen= false;
  AthenaArtifactsBuildOptions options;
  options.range_selector=
    [&] (const std::vector<AthenaArtifactRangeRequest>& requests,
         std::vector<std::vector<int>>& results,
         const AthenaArtifactRangeSelectionProgress&,
         std::string&) {
      for (const AthenaArtifactRangeRequest& request: requests) {
        requests_seen++;
        for (const auto& candidate: request.paragraphs)
          external_resource_seen |=
            candidate.second.find ("i.sstatic.net") != std::string::npos ||
            candidate.second.find ("<image|") != std::string::npos;
      }
      results.assign (requests.size (), std::vector<int> ({0}));
      return true;
    };
  AthenaArtifactsBuildResult built;
  QVERIFY2 (athena_artifacts_build (
              root, {}, true, {}, built, error, options), error.c_str ());
  QVERIFY (requests_seen >= 1);
  QVERIFY (!external_resource_seen);
}

void
TestArtifacts::resumesDefinitionRangeSelectionFromCheckpoint () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  tree body (DOCUMENT);
  for (int i=0; i<129; i++) {
    tree paragraph (CONCAT);
    paragraph << "A definition of "
              << compound ("strong", ("checkpoint term " +
                                        std::to_string (i)).c_str ())
              << " is given here.";
    body << paragraph;
  }
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", "generic")
           << compound ("body", body);
  write_document (root / "Checkpoint.ath", document);

  int first_calls= 0;
  AthenaArtifactsBuildOptions interrupted;
  interrupted.range_selector=
    [&] (const std::vector<AthenaArtifactRangeRequest>& requests,
         std::vector<std::vector<int>>& results,
         const AthenaArtifactRangeSelectionProgress&,
         std::string& selector_error) {
      first_calls++;
      if (first_calls == 2) {
        selector_error= "simulated interruption after checkpoint";
        return false;
      }
      results.assign (requests.size (), std::vector<int> ({0}));
      return true;
    };
  AthenaArtifactsBuildResult failed;
  QVERIFY (!athena_artifacts_build (
    root, {}, true, {}, failed, error, interrupted));
  QCOMPARE (error, std::string ("simulated interruption after checkpoint"));
  QCOMPARE (first_calls, 2);
  QCOMPARE (query_test_int (
              root / info.artifacts_path,
              "SELECT COUNT(*) FROM artifact_range_cache;", error),
            128);

  std::vector<size_t> resumed_batches;
  AthenaArtifactsBuildOptions resumed;
  resumed.range_selector=
    [&] (const std::vector<AthenaArtifactRangeRequest>& requests,
         std::vector<std::vector<int>>& results,
         const AthenaArtifactRangeSelectionProgress&,
         std::string&) {
      resumed_batches.push_back (requests.size ());
      results.assign (requests.size (), std::vector<int> ({0}));
      return true;
    };
  AthenaArtifactsBuildResult built;
  error.clear ();
  QVERIFY2 (athena_artifacts_build (
              root, {}, true, {}, built, error, resumed), error.c_str ());
  QCOMPARE (resumed_batches, std::vector<size_t> ({1}));
  QCOMPARE (built.bold_texts, (size_t) 129);
}

void
TestArtifacts::delegatedFailureLeavesDatabaseUnchanged () {
  MissingRangeModel noModel;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  write_document (root / "A.ath", artifact_test_document ("original term"));
  AthenaArtifactsBuildResult initial;
  QVERIFY2 (athena_artifacts_build (root, {}, true, {}, initial, error),
            error.c_str ());

  std::vector<AthenaArtifactRecord> before;
  QVERIFY2 (athena_artifacts_query (root, before, error), error.c_str ());
  write_document (root / "A.ath", artifact_test_document ("changed term"));
  AthenaArtifactsBuildOptions options;
  options.range_selector=
    [] (const std::vector<AthenaArtifactRangeRequest>&,
        std::vector<std::vector<int>>&,
        const AthenaArtifactRangeSelectionProgress&,
        std::string& selectorError) {
      selectorError= "simulated delegation failure";
      return false;
    };
  AthenaArtifactsBuildResult failed;
  error.clear ();
  QVERIFY (!athena_artifacts_build (
    root, {}, true, {}, failed, error, options));
  QCOMPARE (error, std::string ("simulated delegation failure"));

  std::vector<AthenaArtifactRecord> after;
  error.clear ();
  QVERIFY2 (athena_artifacts_query (root, after, error), error.c_str ());
  QCOMPARE (after.size (), before.size ());
  for (size_t i=0; i<before.size (); i++) {
    QCOMPARE (after[i].display_text, before[i].display_text);
    QCOMPARE (after[i].content_uuid, before[i].content_uuid);
  }
}

QTEST_MAIN (TestArtifacts)
#include "artifacts_test.moc"
