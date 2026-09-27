/******************************************************************************
* MODULE     : enunciation_model_test.cpp
* DESCRIPTION: Detached enunciation registry and source preservation regressions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include <stdexcept>

#include "ATHENA/Data/enunciation_model.hpp"
#include "drd_std.hpp"

namespace en= athena::enunciation;
namespace node= athena::node;

namespace {

const std::string source_id= "11111111-1111-4111-8111-111111111111";
const std::string title_id= "22222222-2222-4222-8222-222222222222";
const std::string body_id= "33333333-3333-4333-8333-333333333333";
const std::string target_id= "44444444-4444-4444-8444-444444444444";

template<typename T> const T& value (const tree& source, const char* key) {
  return std::get<T> (node::get (source)->properties.at (key).data);
}

tree legacy_tree (const en::legacy_definition& alias, tree body) {
  tree result (make_tree_label (string (alias.tag.c_str ())));
  if (alias.layout != en::legacy_layout::body)
    result << compound ("strong", "Explicit title");
  result << body;
  return result;
}

} // namespace

class TestEnunciationModel: public QObject {
  Q_OBJECT

private slots:
  void initTestCase () { init_std_drd (); }
  void all_declared_aliases ();
  void stars_and_proof_variants ();
  void explicit_titles_and_metadata ();
  void numbering_requires_context ();
  void does_not_infer_names_or_links ();
  void unknown_and_malformed_content ();
  void conflicts_are_lossless ();
  void declarations_are_the_source_of_truth ();
};

void TestEnunciationModel::all_declared_aliases () {
  const auto& registry= en::standard_registry ();
  en::conversion_options options;
  options.numbering_preferences["number solutions"]= true;
  for (const auto& entry: registry.legacy_tags ()) {
    const auto& alias= entry.second;
    tree source= legacy_tree (alias, tree (DOCUMENT, "Body"));
    auto converted= en::convert_detached_source (source, registry, options);
    QCOMPARE (converted.converted, std::size_t (1));
    QVERIFY (converted.diagnostics.empty ());
    QVERIFY (en::is_canonical (converted.source));
    QVERIFY (value<std::string> (converted.source, "kind") == alias.kind);
    QCOMPARE (value<bool> (converted.source, "numbered"), alias.numbered);
    QCOMPARE (converted.source[0], tree (DOCUMENT, "Body"));
    QVERIFY (node::get (converted.source)->id.empty ());
    QVERIFY (registry.recognizes (source));
    QVERIFY (registry.category (source) == registry.category (converted.source));
    QVERIFY (registry.rendering (converted.source)->style == alias.render.style);
    auto repeated= en::convert_detached_source (converted.source, registry, options);
    QCOMPARE (repeated.converted, std::size_t (0));
    QCOMPARE (repeated.source, converted.source);
  }
}

void TestEnunciationModel::stars_and_proof_variants () {
  const auto& registry= en::standard_registry ();
  for (const char* tag: {"theorem*", "definition*", "remark*", "exercise*",
                        "problem*", "question*", "answer*", "solution*"}) {
    auto result= en::convert_detached_source (compound (tag, "Body"), registry);
    QVERIFY (en::is_canonical (result.source));
    QVERIFY (!value<bool> (result.source, "numbered"));
  }
  for (const char* tag: {"proof-alternative", "alternative-proof"}) {
    auto result= en::convert_detached_source (compound (tag, "Proof"), registry);
    QVERIFY (value<std::string> (result.source, "kind") == "proof");
    QVERIFY (value<std::string> (result.source, "variant") == "alternative");
    QVERIFY (registry.rendering (result.source)->qed);
  }
  for (const char* tag: {"proof-standard", "standard-proof"}) {
    auto result= en::convert_detached_source (compound (tag, "Proof"), registry);
    QVERIFY (value<std::string> (result.source, "variant") == "standard");
  }
  auto canonical= en::convert_detached_source (compound ("proof-alternative", "Body"));
  node::metadata metadata= *node::get (canonical.source);
  metadata.properties.erase ("legacy-tag");
  node::set (canonical.source, metadata);
  QVERIFY (registry.display_name (canonical.source) == "Proof (Alternative)");
  QVERIFY (registry.rendering (canonical.source)->style == "render-proof-alternative");
  QVERIFY (registry.legacy ("unknown*") == nullptr);
}

void TestEnunciationModel::explicit_titles_and_metadata () {
  tree title (CONCAT, "A title ", compound ("math", "x"));
  node::metadata title_metadata;
  title_metadata.id= title_id;
  node::set (title, title_metadata);
  tree body (DOCUMENT, compound ("label", "old-logical-label"), "Body");
  node::metadata body_metadata;
  body_metadata.id= body_id;
  node::set (body, body_metadata);
  tree source= compound ("render-proof-standard", title, body);
  node::metadata metadata;
  metadata.id= source_id;
  metadata.properties["target"]= node::property (node::reference {target_id});
  node::property::dictionary extension;
  extension["text"]= node::property (std::string ("Opaque"));
  extension["flag"]= node::property (true);
  extension["integer"]= node::property (std::int64_t (42));
  extension["real"]= node::property (1.25);
  extension["list"]= node::property (node::property::list {
    node::property (node::rich_text {compound ("em", "Extension content")}),
    node::property (node::reference {target_id})});
  metadata.properties["future"]= node::property (extension);
  node::set (source, metadata);

  auto result= en::convert_detached_source (source);
  QVERIFY (node::id (result.source) == source_id);
  QVERIFY (node::id (result.source[0]) == body_id);
  const auto& name= value<node::rich_text> (result.source, "name").content;
  QCOMPARE (name, title);
  QVERIFY (node::id (name) == title_id);
  QCOMPARE (result.source[0], body);
  QVERIFY (value<node::reference> (result.source, "target").id == target_id);
  QVERIFY (node::equal (node::get (result.source)->properties.at ("future"),
                       metadata.properties.at ("future")));
  QVERIFY (en::standard_registry ().rendering (result.source)->title_mode == "complete");

  result.source[0][1]= tree ("Changed detached body");
  QCOMPARE (source[1][1], tree ("Body"));
  tree detached_name= name;
  detached_name[0]= tree ("Changed detached title");
  QCOMPARE (source[0][0], tree ("A title "));
  QVERIFY (node::get (source)->properties.count ("kind") == 0);
}

void TestEnunciationModel::numbering_requires_context () {
  tree source= compound ("solution", "Body");
  auto unresolved= en::convert_detached_source (source);
  QCOMPARE (unresolved.source, source);
  QCOMPARE (unresolved.converted, std::size_t (0));
  QCOMPARE (unresolved.diagnostics.size (), std::size_t (1));
  QVERIFY (unresolved.diagnostics[0].issue ==
           en::conversion_issue::missing_numbering_preference);
  for (bool enabled: {false, true}) {
    en::conversion_options options;
    options.numbering_preferences["number solutions"]= enabled;
    auto result= en::convert_detached_source (source, options);
    QCOMPARE (value<bool> (result.source, "numbered"), enabled);
    auto starred= en::convert_detached_source (compound ("solution*", "Body"), options);
    QVERIFY (!value<bool> (starred.source, "numbered"));
  }
}

void TestEnunciationModel::does_not_infer_names_or_links () {
  tree statement= compound ("theorem", tree (DOCUMENT,
    tree (CONCAT, compound ("strong", "(Euler, 1748)"), " Body")));
  tree source (DOCUMENT, statement, compound ("proof", "Adjacent proof"),
               compound ("proof-of", compound ("reference", "old-label"), "Proof"));
  auto result= en::convert_detached_source (source);
  QCOMPARE (result.converted, std::size_t (3));
  QCOMPARE (value<node::rich_text> (result.source[0], "name").content, tree (""));
  QCOMPARE (result.source[0][0], statement[0]);
  QCOMPARE (value<node::rich_text> (result.source[2], "name").content, source[2][0]);
  for (int i= 0; i < N(result.source); ++i) {
    QVERIFY (node::get (result.source[i])->properties.count ("target") == 0);
    QVERIFY (node::id (result.source[i]).empty ());
  }
  QVERIFY (en::standard_registry ().rendering (result.source[2])->title_mode == "subject");
}

void TestEnunciationModel::unknown_and_malformed_content () {
  tree unknown (en::label (), tree (DOCUMENT, compound ("theorem", "Opaque")));
  node::metadata metadata;
  metadata.properties["kind"]= node::property (std::string ("future-kind"));
  metadata.properties["name"]= node::property (node::rich_text {tree ("Future")});
  metadata.properties["numbered"]= node::property (false);
  metadata.properties["future"]= node::property (std::int64_t (7));
  node::set (unknown, metadata);
  tree malformed= compound ("render-theorem", "Title", "Body", "Extra");
  tree quoted (QUOTE, compound ("theorem", "Quoted"));
  tree macro (MACRO, "body", compound ("theorem", "Template"));
  tree source (DOCUMENT, unknown, malformed, quoted, macro);
  auto result= en::convert_detached_source (source);
  QVERIFY (node::equal_metadata (result.source[0], unknown));
  QVERIFY (en::is_canonical (result.source[0][0][0]));
  QCOMPARE (result.source[1], malformed);
  QCOMPARE (result.source[2], quoted);
  QCOMPARE (result.source[3], macro);
  QCOMPARE (result.converted, std::size_t (1));
  QCOMPARE (result.diagnostics.size (), std::size_t (1));
  QVERIFY (result.diagnostics[0].where == std::vector<int> {1});
  QVERIFY (en::is_canonical (result.source[0]));
  QVERIFY (en::standard_registry ().recognizes (unknown));
  QVERIFY (en::standard_registry ().rendering (unknown) == nullptr);

  auto nested= en::convert_detached_source (
    compound ("unknown-wrapper", compound ("lemma", "Nested")));
  QVERIFY (is_compound (nested.source, "unknown-wrapper", 1));
  QVERIFY (en::is_canonical (nested.source[0]));
}

void TestEnunciationModel::conflicts_are_lossless () {
  tree source= compound ("theorem", "Body");
  node::metadata metadata;
  metadata.id= source_id;
  metadata.properties["kind"]= node::property (std::int64_t (42));
  node::set (source, metadata);
  auto result= en::convert_detached_source (source);
  QCOMPARE (result.source, source);
  QCOMPARE (result.converted, std::size_t (0));
  QCOMPARE (result.diagnostics.size (), std::size_t (1));
  QVERIFY (result.diagnostics[0].issue == en::conversion_issue::conflicting_property);
  QVERIFY (node::equal_metadata (result.source, source));
}

void TestEnunciationModel::declarations_are_the_source_of_truth () {
  auto json= en::standard_registry ().declaration ();
  json["future-root"]= {1, 2, 3};
  auto& first= json["kinds"][0];
  first["future-kind"]= {{"opaque", true}};
  first["category"]= "custom-category";
  first["legacy"][0]["future-alias"]= "preserved";
  en::registry registry (json.dump ());
  QVERIFY (registry.declaration () == json);
  QVERIFY (registry.kind ("theorem")->declaration["future-kind"]["opaque"] == true);
  QVERIFY (registry.legacy ("theorem")->declaration["future-alias"] == "preserved");
  QVERIFY (registry.category (compound ("theorem", "Body")) == "custom-category");

  json["kinds"][1]["legacy"][0]["tag"]= "theorem";
  QVERIFY_EXCEPTION_THROWN (en::registry (json.dump ()), std::invalid_argument);
  json= registry.declaration ();
  json["kinds"][0]["legacy"][0]["numbered"]= "true";
  QVERIFY_EXCEPTION_THROWN (en::registry (json.dump ()), std::invalid_argument);
}

QTEST_MAIN (TestEnunciationModel)
#include "enunciation_model_test.moc"
