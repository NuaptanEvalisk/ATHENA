/******************************************************************************
* MODULE     : redundant_wikilinks_test.cpp
* DESCRIPTION: Tests for radioactive-link Vault maintenance cleanup
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/redundant_wikilinks.hpp"
#include "drd_std.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {

tree
document_with_body (tree body) {
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", tuple ("generic"))
           << compound ("body", body);
  return document;
}

tree
block_document (const char* anchor, const char* name) {
  tree body (DOCUMENT);
  body << compound ("label", (std::string (anchor) + " {").c_str ())
       << compound ("definition",
                    tree (CONCAT, compound ("strong", name), " is defined."))
       << compound ("label", (std::string (anchor) + " }").c_str ());
  return document_with_body (body);
}

tree
wikilink (const char* display, const char* uuid) {
  return compound (
    "hlink", display,
    (std::string ("tmfs://wikilink/") + uuid + "/hint/anchor").c_str ());
}

size_t
count_wikilinks (const tree& value) {
  if (is_atomic (value)) return 0;
  size_t count= is_compound (value, "hlink", 2) ? 1 : 0;
  for (int i=0; i<N(value); ++i) count += count_wikilinks (value[i]);
  return count;
}

AthenaArtifactRecord
artifact (const char* uuid, const char* path, const char* anchor) {
  AthenaArtifactRecord record;
  record.uuid= uuid;
  record.type= "definition";
  record.origin= "enunciation";
  record.relative_path= path;
  record.anchor_stem= anchor;
  record.display_text= "Vector space is defined.";
  record.semantic_names= {"Vector space"};
  record.keyword_tree= "definition";
  record.document_order= 0;
  return record;
}

} // namespace

class TestRedundantWikilinks: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void removesOnlyVerifiedBlockTargets ();
};

void
TestRedundantWikilinks::initTestCase () {
  init_std_drd ();
}

void
TestRedundantWikilinks::removesOnlyVerifiedBlockTargets () {
  tree source_body (DOCUMENT);
  source_body << wikilink ("Vector spaces", "block-target")
              << wikilink ("Vector space", "file-target")
              << wikilink ("Vector space", "heading-target")
              << wikilink ("Vector space", "wrong-block")
              << wikilink ("A vector space", "block-target");

  std::vector<AthenaRedundantWikilinkDocument> documents= {
    {"Source.ath", document_with_body (source_body), 0},
    {"Target.ath",
     block_document ("definition:Vector space", "Vector space"), 0},
    {"Other.ath", block_document ("definition:Other", "Other"), 0},
    {"Duplicate.ath",
     block_document ("definition:Vector space", "Vector space"), 0}
  };
  std::vector<AthenaVaultMapNode> nodes= {
    {"block-target", "Target.ath", "", "definition:Vector space {"},
    {"file-target", "Target.ath", "", ""},
    {"heading-target", "Target.ath", "", "H1 Vector space"},
    {"wrong-block", "Other.ath", "", "definition:Other {"}
  };
  std::vector<AthenaArtifactRecord> artifacts= {
    artifact ("artifact-one", "Target.ath", "definition:Vector space"),
    artifact ("artifact-two", "Duplicate.ath", "definition:Vector space")
  };

  AthenaRedundantWikilinkStats stats;
  athena_remove_redundant_block_wikilinks (
    documents, nodes, artifacts, stats);

  QCOMPARE (stats.files_scanned, (size_t) 4);
  QCOMPARE (stats.block_wikilinks_scanned, (size_t) 3);
  QCOMPARE (stats.full_text_matches, (size_t) 2);
  QCOMPARE (stats.links_removed, (size_t) 1);
  QCOMPARE (stats.files_changed, (size_t) 1);
  QCOMPARE (stats.unverified_targets, (size_t) 1);
  QCOMPARE (documents[0].removals, (size_t) 1);
  QCOMPARE (count_wikilinks (documents[0].document), (size_t) 4);
}

QTEST_MAIN (TestRedundantWikilinks)
#include "redundant_wikilinks_test.moc"
