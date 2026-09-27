/******************************************************************************
* MODULE     : document_node_model_test.cpp
* DESCRIPTION: Detached identity planning and typed property schema regressions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "drd_std.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace model= athena::document_node;
namespace node= athena::node;
namespace en= athena::enunciation;

namespace {

std::string deterministic_id (const model::identity_request& request) {
  std::uint64_t value= 1;
  for (int index: request.where) value= value * 257 + index + 1;
  std::ostringstream out;
  out << "00000000-0000-4000-8000-" << std::hex << std::setw (12)
      << std::setfill ('0') << value;
  return out.str ();
}

const std::string existing_id= "11111111-1111-4111-8111-111111111111";

void set_id (tree& source, const std::string& id) {
  node::metadata metadata;
  if (const auto* old= node::get (source)) metadata= *old;
  metadata.id= id;
  node::set (source, metadata);
}

tree canonical (tree body= tree (DOCUMENT, "Body")) {
  tree source (en::label (), body);
  node::metadata metadata;
  metadata.properties["kind"]= node::property (std::string ("future-kind"));
  metadata.properties["name"]= node::property (node::rich_text {tree ("")});
  metadata.properties["numbered"]= node::property (false);
  node::set (source, metadata);
  return source;
}

bool has_issue (const std::vector<model::diagnostic>& diagnostics,
                model::issue issue, const std::string& key= "") {
  return std::any_of (diagnostics.begin (), diagnostics.end (), [&] (const auto& item) {
    return item.code == issue && (key.empty () || item.property == key);
  });
}

} // namespace

class TestDocumentNodeModel: public QObject {
  Q_OBJECT

private slots:
  void initTestCase () { init_std_drd (); }
  void deterministic_paragraphs_and_headings ();
  void registry_roles_and_nested_bodies ();
  void drd_roles_and_code_boundaries ();
  void preserves_and_reserves_existing_ids ();
  void rejects_duplicates_and_allocator_collisions ();
  void ambiguous_roles_and_budgets_fail_before_allocation ();
  void attribution_year_and_inert_extensions ();
};

void TestDocumentNodeModel::deterministic_paragraphs_and_headings () {
  tree source (DOCUMENT, "Paragraph",
               tree (CONCAT, "Prefix ", compound ("section*", "Heading")),
               compound ("doc-title", "Title"));
  auto result= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (result.ok ());
  QCOMPARE (result.assigned.size (), std::size_t (5));
  QVERIFY (result.assigned[0].request.role == model::identity_role::body);
  QVERIFY (result.assigned[1].request.role == model::identity_role::paragraph);
  QVERIFY (result.assigned[2].request.role == model::identity_role::paragraph);
  QVERIFY (result.assigned[3].request.role == model::identity_role::heading);
  QVERIFY (result.assigned[4].request.role == model::identity_role::heading);
  QVERIFY (node::id ((*result.body)[1][0]).empty ());
  QVERIFY (node::id ((*result.body)[2][0]).empty ());
  QVERIFY (node::id (source).empty ());
  QVERIFY (node::id (source[0]).empty ());
  auto again= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (again.ok ());
  QCOMPARE (*again.body, *result.body);
  auto already_assigned= model::assign_detached_source_ids (
    *result.body, standard_drd_for_thread (), {}, {});
  QVERIFY (already_assigned.ok ());
  QVERIFY (already_assigned.assigned.empty ());
  QCOMPARE (*already_assigned.body, *result.body);
}

void TestDocumentNodeModel::registry_roles_and_nested_bodies () {
  const auto& registry= en::standard_registry ();
  model::role_resolver roles= [&] (const tree& source)
      -> std::optional<model::role_declaration> {
    if (!is_compound (source)) return std::nullopt;
    if (is_compound (source, "custom-heading", 1))
      return model::role_declaration {model::semantic_role::heading, "custom",
                                     {model::child_role::inline_content}};
    const string tag= as_string (L(source));
    const auto* legacy= registry.legacy (std::string (tag.data (), N(tag)));
    if (!legacy) return std::nullopt;
    std::vector<model::child_role> children;
    if (legacy->layout != en::legacy_layout::body)
      children.push_back (model::child_role::inline_content);
    children.push_back (model::child_role::body);
    return model::role_declaration {model::semantic_role::enunciation,
                                   registry.category (source), children};
  };
  tree nested= canonical (tree (DOCUMENT, compound ("custom-heading", "Nested"), "Text"));
  tree source (DOCUMENT, compound ("render-theorem", "Explicit title",
                                  tree (DOCUMENT, nested)));
  auto result= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), roles, deterministic_id);
  QVERIFY (result.ok ());
  QVERIFY (result.assigned[1].request.role == model::identity_role::enunciation);
  QVERIFY (result.assigned[1].request.category == "provable");
  QVERIFY (node::id ((*result.body)[0][0]).empty ());
  const tree& converted= (*result.body)[0][1][0];
  QVERIFY (!node::id (converted).empty ());
  QVERIFY (!node::id (converted[0]).empty ());
  QVERIFY (!node::id (converted[0][0]).empty ());
  QVERIFY (!node::id (converted[0][1]).empty ());
  QVERIFY (node::equal (node::get (converted)->properties.at ("kind"),
                       node::get (nested)->properties.at ("kind")));
}

void TestDocumentNodeModel::drd_roles_and_code_boundaries () {
  drd_info drd ("document-node-roles-test", standard_drd_for_thread ());
  const auto tag= make_tree_label ("test-body-and-code");
  drd->info (tag)= tag_info (2, 0, ARITY_NORMAL, CHILD_DETAILED)->
    type (0, TYPE_REGULAR)->accessible (0)->name (0, "body")->
    type (1, TYPE_CODE)->accessible (1);
  tree hidden (DOCUMENT, "Not content");
  tree source (DOCUMENT, tree (tag, tree (DOCUMENT, "Content"), hidden),
               tree (MACRO, "body", hidden), tree (QUOTE, hidden),
               tree (WITH, "mode", "prog", hidden));
  source << compound ("style", hidden);
  auto result= model::assign_detached_source_ids (source, drd, {}, deterministic_id);
  QVERIFY (result.ok ());
  QVERIFY (!node::id ((*result.body)[0][0][0]).empty ());
  QCOMPARE ((*result.body)[0][1], hidden);
  QVERIFY (node::id ((*result.body)[0][1]).empty ());
  for (int i= 1; i < N(source); ++i) QCOMPARE ((*result.body)[i], source[i]);
  QCOMPARE (source[0][0][0], tree ("Content"));
}

void TestDocumentNodeModel::preserves_and_reserves_existing_ids () {
  tree paragraph ("Existing");
  node::metadata metadata;
  metadata.id= existing_id;
  metadata.properties["test:opaque"]= node::property (node::rich_text {
    compound ("unknown-markup", "Stored")});
  metadata.properties["test:reference"]= node::property (node::reference {existing_id});
  node::set (paragraph, metadata);
  tree source (DOCUMENT, paragraph, "New");
  auto result= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (result.ok ());
  QCOMPARE (result.assigned.size (), std::size_t (2));
  QVERIFY (node::equal_metadata ((*result.body)[0], paragraph));
  (*result.body)[0]= tree ("Detached edit");
  QCOMPARE (source[0], paragraph);

  tree quoted (DOCUMENT, tree (QUOTE, paragraph));
  auto collision= model::assign_detached_source_ids (
    quoted, standard_drd_for_thread (), {}, [] (const auto&) { return existing_id; });
  QVERIFY (!collision.body.has_value ());
  QVERIFY (has_issue (collision.diagnostics, model::issue::duplicate_id));

  tree title ("Stored identity");
  set_id (title, existing_id);
  node::metadata root_metadata;
  root_metadata.properties["test:rich"]= node::property (node::rich_text {title});
  tree with_property (DOCUMENT, "Body");
  node::set (with_property, root_metadata);
  auto property_collision= model::assign_detached_source_ids (
    with_property, standard_drd_for_thread (), {}, [] (const auto&) { return existing_id; });
  QVERIFY (has_issue (property_collision.diagnostics, model::issue::duplicate_id));
}

void TestDocumentNodeModel::rejects_duplicates_and_allocator_collisions () {
  tree paragraph ("Repeated");
  set_id (paragraph, existing_id);
  tree duplicated (DOCUMENT, paragraph, copy (paragraph));
  int calls= 0;
  const auto allocate= [&] (const model::identity_request&) {
    ++calls;
    return existing_id;
  };
  auto duplicate= model::assign_detached_source_ids (
    duplicated, standard_drd_for_thread (), {}, allocate);
  QVERIFY (!duplicate.ok ());
  QVERIFY (has_issue (duplicate.diagnostics, model::issue::duplicate_id));
  QCOMPARE (calls, 0);
  tree source (DOCUMENT, "New");
  auto collision= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), {}, allocate);
  QCOMPARE (calls, 2);
  QVERIFY (!collision.body.has_value ());
  QVERIFY (collision.assigned.empty ());
  QVERIFY (node::id (source).empty ());
  auto invalid= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), {}, [] (const auto&) { return "not-a-uuid"; });
  QVERIFY (has_issue (invalid.diagnostics, model::issue::invalid_id));
}

void TestDocumentNodeModel::ambiguous_roles_and_budgets_fail_before_allocation () {
  int calls= 0;
  const auto allocate= [&] (const model::identity_request& request) {
    ++calls;
    return deterministic_id (request);
  };
  tree source (DOCUMENT, compound ("unknown-body-role", tree (DOCUMENT, "Content")));
  auto unknown= model::assign_detached_source_ids (
    source, standard_drd_for_thread (), {}, allocate);
  QVERIFY (has_issue (unknown.diagnostics, model::issue::unsupported_ambiguous_role));
  tree computed_mode (DOCUMENT, tree (WITH, "mode", tree (VALUE, "mode-variable"), "Body"));
  auto computed= model::assign_detached_source_ids (
    computed_mode, standard_drd_for_thread (), {}, allocate);
  QVERIFY (has_issue (computed.diagnostics, model::issue::unsupported_ambiguous_role));
  model::limits budget;
  budget.maximum_nodes= 1;
  auto limited= model::assign_detached_source_ids (
    tree (DOCUMENT, "Body"), standard_drd_for_thread (), {}, allocate, budget);
  QVERIFY (has_issue (limited.diagnostics, model::issue::resource_limit));
  auto envelope= model::assign_detached_source_ids (
    tree (DOCUMENT, compound ("body", tree (DOCUMENT, "Body"))),
    standard_drd_for_thread (), {}, allocate);
  QVERIFY (has_issue (envelope.diagnostics, model::issue::invalid_body));
  QCOMPARE (calls, 0);
}

void TestDocumentNodeModel::attribution_year_and_inert_extensions () {
  tree source= canonical ();
  node::metadata metadata= *node::get (source);
  metadata.properties["attribution"]= node::property (node::property::list {
    node::property (node::rich_text {compound ("future-person", "First author")}),
    node::property (node::rich_text {tree ("Second author")})});
  metadata.properties["year"]= node::property (std::string ("19XX"));
  metadata.properties["test:unknown"]= node::property (node::rich_text {
    compound ("future-markup", tree (EXTERN, "must-not-be-executed"))});
  node::set (source, metadata);
  tree before= copy (source);
  QVERIFY (model::validate_node_properties (source).empty ());
  QCOMPARE (source, before);

  metadata.properties["attribution"]= node::property (node::rich_text {tree ("Singleton")});
  metadata.properties["year"]= node::property (std::int64_t (1900));
  node::set (source, metadata);
  auto errors= model::validate_node_properties (source);
  QVERIFY (has_issue (errors, model::issue::wrong_property_type, "attribution"));
  QVERIFY (has_issue (errors, model::issue::wrong_property_type, "year"));
  metadata.properties["attribution"]= node::property (node::property::list {
    node::property (std::string ("Not structured text"))});
  metadata.properties.erase ("year");
  node::set (source, metadata);
  QVERIFY (has_issue (model::validate_node_properties (source),
                     model::issue::wrong_property_type, "attribution[0]"));
  metadata.properties.erase ("attribution");
  node::set (source, metadata);
  QVERIFY (model::validate_node_properties (source).empty ());
  model::limits budget;
  budget.maximum_nodes= 1;
  QVERIFY (has_issue (model::validate_node_properties (source, budget),
                     model::issue::resource_limit));
}

QTEST_MAIN (TestDocumentNodeModel)
#include "document_node_model_test.moc"
