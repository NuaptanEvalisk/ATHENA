/******************************************************************************
* MODULE     : vault_anchors_test.cpp
* DESCRIPTION: native structural vault anchor regressions
*******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/vault_anchors.hpp"
#include "converter.hpp"

namespace {

tree label (string value) {
  return compound ("label", value);
}

tree statement (const char* tag, string title, string body_text) {
  tree body (DOCUMENT);
  body << compound ("strong", title) << body_text;
  return compound (tag, body);
}

} // namespace

class TestVaultAnchors: public QObject {
  Q_OBJECT

private slots:
  void heading_anchor ();
  void cjk_heading_anchor ();
  void wraps_enunciation ();
  void deterministic_duplicate_suffix ();
  void updates_stale_wrapper_and_renames ();
  void removes_dead_pair ();
  void associates_following_proof ();
};

void
TestVaultAnchors::heading_anchor () {
  tree body (DOCUMENT);
  body << compound ("section", "Introduction") << "Text";

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.headings, (size_t) 1);
  QCOMPARE (N(result.body), 3);
  QCOMPARE (result.body[0], label ("H1 Introduction"));
  QCOMPARE (result.body[1], body[0]);
}

void
TestVaultAnchors::cjk_heading_anchor () {
  string title= utf8_to_cork ("标题");
  tree body (DOCUMENT);
  body << compound ("section", title);

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.headings, (size_t) 1);
  QCOMPARE (result.body[0][0]->label, string ("H1 <#6807><#9898>"));
}

void
TestVaultAnchors::wraps_enunciation () {
  tree body (DOCUMENT);
  body << statement ("definition", "Banach space", "A complete normed space.");

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.wrapped, (size_t) 1);
  QCOMPARE (N(result.body), 3);
  QCOMPARE (result.body[0], label ("definition:Banach space {"));
  QCOMPARE (result.body[2], label ("definition:Banach space }"));
}

void
TestVaultAnchors::deterministic_duplicate_suffix () {
  tree body (DOCUMENT);
  body << compound ("example", "Same body")
       << compound ("example", "Same body");

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.wrapped, (size_t) 2);
  QCOMPARE (result.body[0], label ("example:Same body {"));
  QCOMPARE (result.body[2], label ("example:Same body }"));
  QCOMPARE (result.body[3], label ("example:Same body (1) {"));
  QCOMPARE (result.body[5], label ("example:Same body (1) }"));
}

void
TestVaultAnchors::updates_stale_wrapper_and_renames () {
  tree body (DOCUMENT);
  body << label ("theorem:Old {")
       << statement ("theorem", "(New title)", "Statement")
       << label ("theorem:Old }");

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.updated, (size_t) 1);
  QCOMPARE (result.summary.renames.size (), (size_t) 2);
  QCOMPARE (result.body[0], label ("theorem:New title {"));
  QCOMPARE (result.body[2], label ("theorem:New title }"));
  QCOMPARE (result.summary.renames[0].first, string ("theorem:Old {"));
  QCOMPARE (result.summary.renames[0].second, string ("theorem:New title {"));
}

void
TestVaultAnchors::removes_dead_pair () {
  tree body (DOCUMENT);
  body << label ("obsolete {") << "   " << label ("obsolete }") << "Body";

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.dead_pairs, (size_t) 1);
  QCOMPARE (N(result.body), 1);
  QCOMPARE (result.body[0], tree ("Body"));
}

void
TestVaultAnchors::associates_following_proof () {
  tree body (DOCUMENT);
  body << statement ("theorem", "(Named result)", "Statement")
       << compound ("proof", "Proof body");

  VaultAnchorTransform result= vault_anchor_transform (body);
  QCOMPARE (result.summary.wrapped, (size_t) 2);
  QCOMPARE (result.body[0], label ("theorem:Named result {"));
  QCOMPARE (result.body[3], label ("proof:Named result {"));
  QCOMPARE (result.body[5], label ("proof:Named result }"));
}

QTEST_APPLESS_MAIN (TestVaultAnchors)
#include "vault_anchors_test.moc"
