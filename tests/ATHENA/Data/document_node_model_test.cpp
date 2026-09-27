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
#include "ATHENA/Data/document_node_copy.hpp"
#include "patch.hpp"
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
  void property_update_preserves_identity_and_replays ();
  void property_update_is_atomic_and_protects_bindings ();
  void property_update_rejects_rich_identity_collisions ();
  void property_update_handles_atoms_and_noops ();
  void property_drafts_reject_conflicts_and_protected_changes ();
  void incremental_identities_keep_text_edits_local ();
  void incremental_identities_classify_inserted_bodies ();
  void incremental_identities_reserve_rich_property_owners ();
  void incremental_identities_reject_stale_plans_and_rebase_batches ();
  void incremental_identities_rebuild_after_applied_rollback ();
  void incremental_identities_fail_closed_after_bad_reinitialization ();
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
    accessible (1)->type (1, TYPE_CODE);
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

void TestDocumentNodeModel::property_update_preserves_identity_and_replays () {
  tree statement= canonical ();
  set_id (statement, existing_id);
  tree source (DOCUMENT, statement, "Untouched");
  tree before= copy (source);
  tree title (CONCAT, "Hahn-Banach ", compound ("em", "Theorem"));
  model::property_edit edit;
  edit.set["name"]= node::property (node::rich_text {title});
  edit.set["year"]= node::property (std::string ("19XX"));
  edit.set["attribution"]= node::property (node::property::list {
    node::property (node::rich_text {tree ("Hahn")}),
    node::property (node::rich_text {tree ("Banach")})});
  auto prepared= model::prepare_property_edit (source, {0}, edit);
  QVERIFY (prepared.ok ());
  QVERIFY (prepared.change.has_value ());
  QVERIFY (prepared.change.value ()->k == MOD_SET_METADATA);
  QCOMPARE (source, before);
  auto inverse= invert (*prepared.change, source);
  auto changed= clean_apply (source, *prepared.change);
  QCOMPARE (node::id (changed[0]), existing_id);
  QVERIFY (strong_equal (changed[0][0], source[0][0]));
  QVERIFY (strong_equal (changed[1], source[1]));
  title[0]= "Changed outside";
  auto name= std::get<node::rich_text> (node::get (changed[0])->properties.at ("name").data).content;
  QCOMPARE (name[0], tree ("Hahn-Banach "));
  auto undone= clean_apply (changed, inverse);
  QCOMPARE (undone, source);
  QCOMPARE (clean_apply (undone, *prepared.change), changed);
}

void TestDocumentNodeModel::property_update_is_atomic_and_protects_bindings () {
  tree source= canonical ();
  node::metadata metadata= *node::get (source);
  metadata.properties[model::artifact_bindings_property]= node::property (
    node::property::dictionary {{"statement", node::property (existing_id)}});
  metadata.properties["future:extension"]= node::property (std::string ("Keep me"));
  node::set (source, metadata);
  tree before= copy (source);
  model::property_edit edit;
  edit.set["year"]= node::property (std::int64_t (2000));
  edit.set["kind"]= node::property (std::string ("lemma"));
  auto invalid= model::prepare_property_edit (source, {}, edit);
  QVERIFY (!invalid.ok ());
  QVERIFY (!invalid.change);
  QCOMPARE (source, before);
  edit.set.erase ("year");
  auto valid= model::prepare_property_edit (source, {}, edit);
  QVERIFY (valid.ok () && valid.change);
  auto output= clean_apply (source, *valid.change);
  QVERIFY (node::equal (node::get (output)->properties.at (model::artifact_bindings_property),
                       metadata.properties.at (model::artifact_bindings_property)));
  QVERIFY (node::equal (node::get (output)->properties.at ("future:extension"),
                       metadata.properties.at ("future:extension")));
  for (const std::string key: {"id", "uuid", model::artifact_bindings_property}) {
    edit= {};
    edit.set[key]= node::property (existing_id);
    QVERIFY (!model::prepare_property_edit (source, {}, edit).ok ());
    edit.set.clear ();
    edit.remove.push_back (key);
    QVERIFY (!model::prepare_property_edit (source, {}, edit).ok ());
  }
  edit= {};
  edit.remove= {"kind"};
  QVERIFY (!model::prepare_property_edit (source, {}, edit).ok ());
  edit.remove= {"future:extension", "future:extension"};
  QVERIFY (!model::prepare_property_edit (source, {}, edit).ok ());
  edit.remove= {"future:extension"};
  edit.set["future:extension"]= node::property (true);
  QVERIFY (!model::prepare_property_edit (source, {}, edit).ok ());
  metadata.properties[model::artifact_bindings_property]= node::property (true);
  node::set (source, metadata);
  QVERIFY (!model::validate_node_properties (source).empty ());
}

void TestDocumentNodeModel::property_update_rejects_rich_identity_collisions () {
  tree paragraph ("Identity owner");
  set_id (paragraph, existing_id);
  tree source (DOCUMENT, canonical (), paragraph);
  model::property_edit edit;
  edit.set["name"]= node::property (node::rich_text {paragraph});
  auto conflict= model::prepare_property_edit (source, {0}, edit);
  QVERIFY (has_issue (conflict.diagnostics, model::issue::duplicate_id));
  QVERIFY (!conflict.change);
  QVERIFY (!model::prepare_property_edit (source, {-1}, edit).ok ());
  QVERIFY (!model::prepare_property_edit (source, {2}, edit).ok ());
  QVERIFY (!model::prepare_property_edit (source, {1, 0}, edit).ok ());
  QVERIFY (!model::prepare_property_edit (tree (UNINIT), {}, edit).ok ());
  edit.set["name"]= node::property (node::rich_text {node::duplicate (paragraph)});
  auto accepted= model::prepare_property_edit (source, {0}, edit);
  QVERIFY (accepted.ok () && accepted.change);
  auto output= clean_apply (source, *accepted.change);
  // Replacing a rich property with its existing identity is not a duplicate.
  QVERIFY (model::prepare_property_edit (output, {0}, edit).ok ());
  model::limits budget;
  budget.maximum_nodes= 1;
  QVERIFY (!model::prepare_property_edit (source, {0}, edit, budget).ok ());
}

void TestDocumentNodeModel::property_update_handles_atoms_and_noops () {
  tree source (DOCUMENT, "Atomic paragraph");
  model::property_edit edit;
  edit.ensure_id= true;
  edit.set["test:enabled"]= node::property (false);
  auto prepared= model::prepare_property_edit (source, {0}, edit);
  QVERIFY (prepared.ok () && prepared.change);
  QVERIFY (node::valid_id (prepared.id));
  auto inverse= invert (*prepared.change, source);
  auto output= clean_apply (source, *prepared.change);
  QCOMPARE (node::id (output[0]), prepared.id);
  auto repeated= model::prepare_property_edit (output, {0}, edit);
  QVERIFY (repeated.ok () && !repeated.change);
  QCOMPARE (repeated.id, prepared.id);
  auto undone= clean_apply (output, inverse);
  QCOMPARE (undone, source);
  QCOMPARE (node::id (clean_apply (undone, *prepared.change)[0]), prepared.id);
  edit= {};
  edit.remove= {"test:enabled"};
  auto removal= model::prepare_property_edit (output, {0}, edit);
  QVERIFY (removal.ok () && removal.change);
  output= clean_apply (output, *removal.change);
  QCOMPARE (node::id (output[0]), prepared.id);
  QVERIFY (node::get (output[0])->properties.empty ());
}

void TestDocumentNodeModel::property_drafts_reject_conflicts_and_protected_changes () {
  tree source (DOCUMENT, canonical ());
  set_id (source[0], existing_id);
  auto metadata= *node::get (source[0]);
  metadata.properties["test:opaque-title"]= node::property (node::rich_text {
    tree (EXTERN, "never-executed")});
  metadata.properties["athena:artifact-bindings"]= node::property (node::property::dictionary {
    {"statement", node::property (std::string ("22222222-2222-4222-8222-222222222222"))}});
  node::set (source[0], metadata);
  tree captured= model::property_header (source[0]);
  QCOMPARE (N(captured), 0);
  metadata.properties["year"]= node::property (std::string ("19XX"));
  tree draft= copy (captured);
  node::set (draft, metadata);
  // Body changes do not conflict with independent property editing.
  source[0][0]= tree (DOCUMENT, "Edited body");
  auto prepared= model::prepare_property_replacement (source, {0}, captured, draft);
  QVERIFY (prepared.ok () && prepared.change);
  tree changed= clean_apply (source, *prepared.change);
  QCOMPARE (changed[0][0], source[0][0]);
  QCOMPARE (node::id (changed[0]), existing_id);
  QVERIFY (node::equal (node::get (changed[0])->properties.at ("athena:artifact-bindings"),
                       node::get (source[0])->properties.at ("athena:artifact-bindings")));
  QVERIFY (has_issue (model::prepare_property_replacement (changed, {0}, captured, draft).diagnostics,
                     model::issue::stale_properties));
  metadata.id= "33333333-3333-4333-8333-333333333333";
  node::set (draft, metadata);
  QVERIFY (has_issue (model::prepare_property_replacement (source, {0}, captured, draft).diagnostics,
                     model::issue::protected_property));
  metadata.id= existing_id;
  metadata.properties.erase ("athena:artifact-bindings");
  node::set (draft, metadata);
  QVERIFY (has_issue (model::prepare_property_replacement (source, {0}, captured, draft).diagnostics,
                     model::issue::protected_property));
  auto noop= model::prepare_property_replacement (source, {0}, captured, captured);
  QVERIFY (noop.ok () && !noop.change);
  QCOMPARE (clean_apply (changed, invert (*prepared.change, source)), source);
  QVERIFY (!model::prepare_property_replacement (source, {9}, captured, captured).ok ());
}

void TestDocumentNodeModel::incremental_identities_keep_text_edits_local () {
  tree source (DOCUMENT, 10000);
  for (int i=0; i<N(source); ++i) source[i]= tree ("Text");
  tree untouched= source[9999];
  model::source_identity_state state;
  QVERIFY (state.initialize (source).empty ());
  auto initial= state.prepare (source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (initial.ok ());
  QCOMPARE (initial.changes.size (), std::size_t (10001));
  state.apply (source, initial);
  QVERIFY (strong_equal (untouched, source[9999]));
  QVERIFY (!state.pending ());
  auto insertion= mod_insert (path (5000), 2, "new");
  raw_apply (source, insertion); state.observe (insertion);
  model::limits tiny; tiny.maximum_nodes= 4;
  auto typed= state.prepare (source, standard_drd_for_thread (), {}, {}, tiny);
  QVERIFY (typed.ok ());
  QVERIFY (typed.changes.empty ());
  QCOMPARE (typed.scope, model::source_path {5000});
  state.apply (source, typed);
  QCOMPARE (source[5000]->label, string ("Tenewxt"));
  QVERIFY (strong_equal (untouched, source[9999]));
}

void TestDocumentNodeModel::incremental_identities_classify_inserted_bodies () {
  tree source (DOCUMENT, "Original");
  model::source_identity_state state;
  QVERIFY (state.initialize (source).empty ());
  state.apply (source, state.prepare (source, standard_drd_for_thread (), {}, deterministic_id));
  const auto root_id= node::id (source);
  auto inserted= mod_insert (path (), 1, tree (DOCUMENT, canonical (tree (DOCUMENT, "New paragraph"))));
  raw_apply (source, inserted); state.observe (inserted);
  auto plan= state.prepare (source, standard_drd_for_thread (), model::standard_source_role,
    [] (const auto&) { return node::new_id (); });
  QVERIFY (plan.ok ());
  QCOMPARE (plan.assigned.size (), std::size_t (3));
  state.apply (source, plan);
  QCOMPARE (node::id (source), root_id);
  QVERIFY (!node::id (source[1]).empty ());
  QVERIFY (!node::id (source[1][0]).empty ());
  QVERIFY (!node::id (source[1][0][0]).empty ());
  auto noop= state.prepare (source, standard_drd_for_thread (), {}, {});
  QVERIFY (noop.ok () && noop.changes.empty ());
}

void TestDocumentNodeModel::incremental_identities_reserve_rich_property_owners () {
  tree source (DOCUMENT, tree (CONCAT, "First"), "Second");
  tree rich (CONCAT, "Rich identity"); set_id (rich[0], existing_id);
  node::metadata metadata;
  metadata.properties["test:rich"]= node::property (node::rich_text {rich});
  node::set (source[0], metadata);
  model::source_identity_state state;
  QVERIFY (state.initialize (source).empty ());
  auto initial= state.prepare (source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (initial.ok ()); state.apply (source, initial);
  auto edit= mod_insert (path (0, 0), 1, "!");
  raw_apply (source, edit); state.observe (edit);
  auto plan= state.prepare (source, standard_drd_for_thread (), {}, {});
  QVERIFY (plan.ok ()); state.apply (source, plan);
  tree original= copy (source[1]);
  tree duplicate ("Duplicate rich identity"); set_id (duplicate, existing_id);
  auto replace= mod_assign (path (1), duplicate);
  raw_apply (source, replace); state.observe (replace);
  auto failed= state.prepare (source, standard_drd_for_thread (), {}, {});
  QVERIFY (has_issue (failed.diagnostics, model::issue::duplicate_id));
  QVERIFY (failed.changes.empty ());
  raw_apply (source, mod_assign (path (1), original));
  QVERIFY (state.cancelled (source).empty ());
  QVERIFY (!state.pending ());
  QCOMPARE (node::id (source[1]), node::id (original));
}

void TestDocumentNodeModel::incremental_identities_reject_stale_plans_and_rebase_batches () {
  tree source (DOCUMENT, "First", "Second", "Third");
  model::source_identity_state state;
  QVERIFY (state.initialize (source).empty ());
  auto initial= state.prepare (source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (initial.ok ()); state.apply (source, initial);
  auto edit= mod_insert (path (2), 0, "!");
  raw_apply (source, edit); state.observe (edit);
  auto stale= state.prepare (source, standard_drd_for_thread (), {}, {});
  auto removal= mod_remove (path (), 0, 1);
  raw_apply (source, removal); state.observe (removal);
  QVERIFY_EXCEPTION_THROWN (state.apply (source, stale), std::logic_error);
  auto fresh= state.prepare (source, standard_drd_for_thread (), {}, {});
  QVERIFY (fresh.ok () && fresh.changes.empty () && fresh.scope.empty ());
  state.apply (source, fresh);
  edit= mod_insert (path (1), 0, "?");
  raw_apply (source, edit); state.observe (edit);
  auto local= state.prepare (source, standard_drd_for_thread (), {}, {});
  QVERIFY (local.ok ()); QCOMPARE (local.scope, model::source_path {1});
  state.apply (source, local);
}

void TestDocumentNodeModel::incremental_identities_rebuild_after_applied_rollback () {
  tree source (DOCUMENT, "First", "Second");
  model::source_identity_state state;
  QVERIFY (state.initialize (source).empty ());
  auto initial= state.prepare (source, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (initial.ok ()); state.apply (source, initial);
  const tree before= copy (source);
  auto insertion= mod_insert (path (), 0, tree (DOCUMENT, "Inserted"));
  raw_apply (source, insertion); state.observe (insertion);
  auto plan= state.prepare (source, standard_drd_for_thread (), {},
    [] (const auto&) { return node::new_id (); });
  QVERIFY (plan.ok ()); state.apply (source, plan);
  // A save/preflight can apply the plan before the history transaction fails.
  source= copy (before);
  QVERIFY (state.cancelled (source).empty ());
  QVERIFY (!state.pending ());
  auto edit= mod_insert (path (1), 0, "!");
  raw_apply (source, edit); state.observe (edit);
  auto local= state.prepare (source, standard_drd_for_thread (), {}, {});
  QVERIFY (local.ok ());
  QVERIFY (local.changes.empty ());
  QCOMPARE (local.scope, model::source_path {1});
  state.apply (source, local);
}

void TestDocumentNodeModel::incremental_identities_fail_closed_after_bad_reinitialization () {
  tree source (DOCUMENT, "First");
  model::source_identity_state state;
  QVERIFY (state.pending ());
  QVERIFY (state.initialize (source).empty ());
  state.apply (source, state.prepare (source, standard_drd_for_thread (), {}, deterministic_id));
  QVERIFY (!state.pending ());
  tree duplicate (DOCUMENT, copy (source[0]), copy (source[0]));
  QVERIFY (has_issue (state.cancelled (duplicate), model::issue::duplicate_id));
  QVERIFY (state.pending ());
  auto plan= state.prepare (duplicate, standard_drd_for_thread (), {}, deterministic_id);
  QVERIFY (!plan.ok ());
  QVERIFY_EXCEPTION_THROWN (state.apply (duplicate, plan), std::logic_error);
  QVERIFY (state.cancelled (source).empty ());
  QVERIFY (!state.pending ());
}

QTEST_MAIN (TestDocumentNodeModel)
#include "document_node_model_test.moc"
