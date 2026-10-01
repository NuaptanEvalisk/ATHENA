/******************************************************************************
* MODULE     : native_editor_actions.cpp
* DESCRIPTION: Validated parameterized native editor actions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "native_editor_actions.hpp"

#include "format_commands.hpp"
#include "generic_editor_commands.hpp"
#include "scheme.hpp"
#include "Scheme/Scheme/native_interfaces.hpp"
#include "Subsystems/Qt/QTMReverseHierarchyGraph.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace {

string
native_action_string (const QJsonValue& value) {
  ASSERT (value.isString (), "native editor action string expected");
  const QString text= value.toString ();
  const QByteArray bytes= text.toUtf8 ();
  ASSERT (QString::fromUtf8 (bytes) == text,
          "invalid UTF-8 native editor action string");
  return string (bytes.constData (), bytes.size ());
}

tree
native_action_tree (const QJsonValue& value) {
  if (value.isString ()) return tree (native_action_string (value));
  ASSERT (value.isArray (),
          "native editor action tree must be string or array");
  const QJsonArray values= value.toArray ();
  ASSERT (!values.isEmpty () && values[0].isString (),
          "native editor action compound tree needs a tag");
  tree result (as_tree_label (native_action_string (values[0])),
               values.size () - 1);
  for (int i= 1; i < values.size (); ++i)
    result[i - 1]= native_action_tree (values[i]);
  return result;
}

bool
has_string (const QJsonObject& object, const char* name) {
  return object.value (QString::fromLatin1 (name)).isString ();
}

bool
has_bool (const QJsonObject& object, const char* name) {
  return object.value (QString::fromLatin1 (name)).isBool ();
}

bool
valid_large_mode (const QJsonValue& value) {
  return value.isBool () ||
         (value.isString () && value.toString () == "default");
}

int
native_action_large_mode (const QJsonValue& value) {
  if (value.isBool ()) return value.toBool () ? 1 : 0;
  ASSERT (value.isString () && value.toString () == "default",
          "native editor bracket large mode expected");
  return -1;
}

bool
fail_validation (QString* error, const QString& message) {
  if (error != nullptr) *error= message;
  return false;
}

bool
valid_business_id (const QString& id) {
  static const QSet<QString> ids {
    "insert-wikilink",
    "insert-transclusion",
    "insert-material-citation",
    "insert-referenced-materials",
    "open-latex-formula-dialog",
    "insert-include-dialog",
    "insert-link-image-dialog",
    "insert-inline-image-dialog",
    "insert-thumbnails-dialog",
    "make-graphics",
    "insert-small-table",
    "insert-big-table",
    "insert-small-figure",
    "insert-big-figure",
    "insert-floating-figure",
    "insert-floating-table",
    "insert-floating-algorithm",
    "letter-today",
    "tmdoc-explain-synopsis",
    "make-alter-colors"
  };
  return ids.contains (id);
}

void
execute_business_id (const QString& id) {
  if (id == "insert-wikilink") (void) call ("insert-wikilink");
  else if (id == "insert-transclusion") (void) call ("insert-transclude");
  else if (id == "insert-material-citation")
    (void) call ("insert-material-citation");
  else if (id == "insert-referenced-materials")
    (void) call ("insert-referenced-materials");
  else if (id == "open-latex-formula-dialog")
    (void) call ("open-latex-formula-dialog");
  else if (id == "insert-include-dialog")
    (void) call ("native-insert-include-dialog");
  else if (id == "insert-link-image-dialog")
    (void) call ("native-insert-link-image-dialog");
  else if (id == "insert-inline-image-dialog")
    (void) call ("native-insert-inline-image-dialog");
  else if (id == "insert-thumbnails-dialog")
    (void) call ("native-insert-thumbnails-dialog");
  else if (id == "make-graphics") (void) call ("make-graphics");
  else if (id == "insert-small-table")
    (void) call ("native-insert-small-table");
  else if (id == "insert-big-table")
    (void) call ("native-insert-big-table");
  else if (id == "insert-small-figure")
    (void) call ("native-insert-small-figure");
  else if (id == "insert-big-figure")
    (void) call ("native-insert-big-figure");
  else if (id == "insert-floating-figure")
    (void) call ("native-insert-floating-figure");
  else if (id == "insert-floating-table")
    (void) call ("native-insert-floating-table");
  else if (id == "insert-floating-algorithm")
    (void) call ("native-insert-floating-algorithm");
  else if (id == "letter-today") {
    (void) call ("make-header", symbol_object ("letter-date"));
    (void) call ("make", symbol_object ("date"), object (0));
  }
  else if (id == "tmdoc-explain-synopsis")
    (void) call ("tmdoc-insert-explain-synopsis");
  else if (id == "make-alter-colors") (void) call ("make-alter-colors");
  else FAILED ("unknown native editor business id");
}

void
make_section (editor ed, string tag) {
  if (ed->selection_active_any ()) {
    if (!ed->selection_active_small ()) return;
    ed->make_compound (as_tree_label (tag));
    return;
  }
  if (!ed->make_return_after ()) ed->make_compound (as_tree_label (tag));
}

void
make_equation_like (editor ed, string tag) {
  if (ed->selection_active_any () && !ed->selection_active_small ()) return;
  ed->make_compound (as_tree_label (tag));
  ed->ensure_trailing_proof_paragraph ();
}

void
make_aux (editor ed, string env, string var, string fallback) {
  string aux= ed->defined_at_cursor (var) ? ed->get_env_string (var) : fallback;
  if (ed->make_return_after ()) return;
  tree value (as_tree_label (env), tree (aux), tree (DOCUMENT, ""));
  ed->insert_tree (value);
}

} // namespace

bool
native_editor_action_validate (const QJsonObject& action, QString* error) {
  const QJsonValue opValue= action.value ("op");
  if (!opValue.isString () || opValue.toString ().trimmed ().isEmpty ())
    return fail_validation (error, "editor action requires non-empty op");
  const QString op= opValue.toString ();

  if (op == "insert-tree")
    return action.contains ("value") &&
           (action.value ("value").isString () ||
            action.value ("value").isArray ()) ?
             true : fail_validation (error, "insert-tree requires value");
  if (op == "make")
    return has_string (action, "tag") &&
           (!action.contains ("arity") || action.value ("arity").isDouble ()) ?
             true : fail_validation (error, "make requires tag and optional arity");
  if (op == "make-with" || op == "make-style-with" ||
      op == "make-line-with")
    return has_string (action, "var") && has_string (action, "value") ?
             true : fail_validation (error, op + " requires var/value");
  if (op == "interactive-line-with")
    return has_string (action, "var") ?
             true : fail_validation (error, "interactive-line-with requires var");
  if (op == "make-mod-active")
    return has_string (action, "tag") ?
             true : fail_validation (error, "make-mod-active requires tag");
  if (op == "make-script")
    return has_bool (action, "sup") && has_bool (action, "right") ?
             true : fail_validation (error, "make-script requires sup/right");
  if (op == "make-wide" || op == "make-wide-under")
    return has_string (action, "value") &&
           (!action.contains ("stretch") || action.value ("stretch").isBool ()) ?
             true : fail_validation (error, op + " requires value");
  if (op == "insert-big")
    return action.contains ("value") ?
             true : fail_validation (error, "insert-big requires value");
  if (op == "emulate-keyboard")
    return has_string (action, "keys") ?
             true : fail_validation (error, "emulate-keyboard requires keys");
  if (op == "make-space")
    return has_string (action, "value") ?
             true : fail_validation (error, "make-space requires value");
  if (op == "math-bracket-open" || op == "math-bracket-close")
    return has_string (action, "left") && has_string (action, "right") &&
           valid_large_mode (action.value ("large")) ?
             true : fail_validation (
               error, op + " requires left/right/large");
  if (op == "math-separator")
    return has_string (action, "value") &&
           valid_large_mode (action.value ("large")) ?
             true : fail_validation (
               error, "math-separator requires value/large");
  if (op == "insert-long-arrow")
    return has_string (action, "value") &&
           (!action.contains ("below") || has_bool (action, "below")) ?
             true : fail_validation (
               error, "insert-long-arrow requires value and optional below");
  if (op == "insert-alphabet")
    return has_string (action, "tag") && has_string (action, "value") ?
             true : fail_validation (
               error, "insert-alphabet requires tag/value");
  if (op == "make-section" || op == "make-unnamed-section" ||
      op == "make-header" || op == "tmdoc-branch" ||
      op == "make-equation-like" ||
      op == "make-tmlist" || op == "make-toggle" ||
      op == "make-switch" || op == "make-unroll" ||
      op == "make-overlays" || op == "make-overlay" ||
      op == "make-insertion")
    return has_string (action, "tag") ?
             true : fail_validation (error, op + " requires tag");
  if (op == "make-star")
    return has_string (action, "tag") && has_string (action, "style") ?
             true : fail_validation (error, "make-star requires tag/style");
  if (op == "make-switch-list")
    return has_string (action, "tag") && has_string (action, "list") ?
             true : fail_validation (
               error, "make-switch-list requires tag/list");
  if (op == "make-aux")
    return has_string (action, "env") && has_string (action, "var") &&
           has_string (action, "fallback") ?
             true : fail_validation (
               error, "make-aux requires env/var/fallback");
  if (op == "make-alternate")
    return has_string (action, "prompt") &&
           has_string (action, "default") &&
           has_string (action, "tag") ?
             true : fail_validation (
               error, "make-alternate requires prompt/default/tag");
  if (op == "business")
    return has_string (action, "id") &&
           valid_business_id (action.value ("id").toString ()) ?
             true : fail_validation (error, "unknown native business id");

  if (op == "make-fraction" || op == "make-sqrt" ||
      op == "make-var-sqrt" || op == "make-neg" ||
      op == "make-above" || op == "make-below" ||
      op == "make-screens" || op == "make-label" ||
      op == "make-balloon" || op == "make-marginal-note" ||
      op == "make-note-ref" || op == "make-note-inline" ||
      op == "make-note-wide" || op == "make-note-footnote" ||
      op == "make-doc-data" || op == "make-abstract-data" ||
      op == "make-cd" || op == "insert-reverse-hierarchy-graph" ||
      op == "make-experimental-build-warning")
    return true;

  return fail_validation (error, "unsupported native editor action op: " + op);
}

void
native_editor_action_execute (editor ed, const QJsonObject& action) {
  ASSERT (!is_nil (ed), "native editor action without editor");
  QString error;
  ASSERT (native_editor_action_validate (action, &error),
          "invalid native editor action");
  const string op= native_action_string (action.value ("op"));

  if (op == "insert-tree")
    ed->insert_tree (native_action_tree (action.value ("value")));
  else if (op == "make") {
    int arity= action.contains ("arity") ?
      action.value ("arity").toInt (-1) : -1;
    const string tag= native_action_string (action.value ("tag"));
    if (arity < 0) (void) call ("make", symbol_object (tag));
    else (void) call ("make", symbol_object (tag), object (arity));
  }
  else if (op == "make-with")
    (void) call (
      "make-with",
      object (native_action_string (action.value ("var"))),
      object (native_action_string (action.value ("value"))));
  else if (op == "make-style-with")
    ed->make_style_with (native_action_string (action.value ("var")),
                         native_action_string (action.value ("value")));
  else if (op == "make-line-with")
    format_make_line_with (
      native_action_string (action.value ("var")),
      tree (native_action_string (action.value ("value"))));
  else if (op == "interactive-line-with")
    (void) call (
      "make-interactive-line-with",
      object (native_action_string (action.value ("var"))));
  else if (op == "make-mod-active")
    ed->make_mod_active (
      as_tree_label (native_action_string (action.value ("tag"))));
  else if (op == "make-script")
    ed->make_script (action.value ("sup").toBool (),
                     action.value ("right").toBool ());
  else if (op == "make-fraction") ed->make_fraction ();
  else if (op == "make-sqrt") ed->make_sqrt ();
  else if (op == "make-var-sqrt") ed->make_var_sqrt ();
  else if (op == "make-neg") ed->make_neg ();
  else if (op == "make-above") ed->make_above ();
  else if (op == "make-below") ed->make_below ();
  else if (op == "make-wide" || op == "make-wide-under") {
    const string value= native_action_string (action.value ("value"));
    const bool stretch= action.value ("stretch").toBool (false);
    if (op == "make-wide") ed->make_wide (value, stretch);
    else ed->make_wide_under (value, stretch);
  }
  else if (op == "insert-big")
    ed->insert_tree (tree (BIG, native_action_tree (action.value ("value"))));
  else if (op == "emulate-keyboard")
    ed->emulate_keyboard (native_action_string (action.value ("keys")));
  else if (op == "make-space")
    ed->make_space (native_action_string (action.value ("value")));
  else if (op == "math-bracket-open")
    ed->math_bracket_open (
      native_action_string (action.value ("left")),
      native_action_string (action.value ("right")),
      native_action_large_mode (action.value ("large")));
  else if (op == "math-bracket-close")
    ed->math_bracket_close (
      native_action_string (action.value ("right")),
      native_action_string (action.value ("left")),
      native_action_large_mode (action.value ("large")));
  else if (op == "math-separator")
    ed->math_separator (
      native_action_string (action.value ("value")),
      native_action_large_mode (action.value ("large")));
  else if (op == "insert-long-arrow") {
    const string value= native_action_string (action.value ("value"));
    const bool below= action.value ("below").toBool (false);
    tree arrow= below ? tree (LONG_ARROW, tree (value), "", "") :
                        tree (LONG_ARROW, tree (value), "");
    ed->insert_tree (arrow, below ? path (2, 0) : path (1, 0));
  }
  else if (op == "insert-alphabet")
    ed->insert_tree (
      tree (as_tree_label (native_action_string (action.value ("tag"))),
            tree (native_action_string (action.value ("value")))));
  else if (op == "make-section")
    make_section (ed, native_action_string (action.value ("tag")));
  else if (op == "make-unnamed-section")
    (void) call (
      "make-unnamed-section",
      symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-header")
    (void) call (
      "make-header",
      symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-star")
    (void) call (
      "make*",
      symbol_object (native_action_string (action.value ("tag"))),
      object (native_action_string (action.value ("style"))));
  else if (op == "tmdoc-branch")
    (void) call (
      "tmdoc-make-branch",
      symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-equation-like")
    make_equation_like (ed, native_action_string (action.value ("tag")));
  else if (op == "make-tmlist")
    (void) call (
      "make-tmlist", symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-toggle")
    (void) call (
      "make-toggle", symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-switch")
    (void) call (
      "make-switch", symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-unroll")
    (void) call (
      "make-unroll", symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-switch-list")
    (void) call (
      "make-switch-list",
      symbol_object (native_action_string (action.value ("tag"))),
      symbol_object (native_action_string (action.value ("list"))));
  else if (op == "make-overlays")
    (void) call (
      "make-overlays",
      symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-overlay")
    (void) call (
      "make-overlay",
      symbol_object (native_action_string (action.value ("tag"))));
  else if (op == "make-screens") (void) call ("make-screens");
  else if (op == "make-insertion")
    generic_make_insertion (native_action_string (action.value ("tag")));
  else if (op == "make-label") generic_make_label ();
  else if (op == "make-balloon") generic_make_balloon ();
  else if (op == "make-marginal-note") generic_make_marginal_note ();
  else if (op == "make-note-ref") generic_make_note_ref ();
  else if (op == "make-note-inline") generic_make_note_inline ();
  else if (op == "make-note-wide") generic_make_note_wide ();
  else if (op == "make-note-footnote") generic_make_note_footnote ();
  else if (op == "make-doc-data") {
    tree value (
      as_tree_label ("doc-data"),
      tree (as_tree_label ("doc-title"), tree ("")));
    ed->insert_tree (value, path (0, 0, 0));
  }
  else if (op == "make-abstract-data") {
    tree value (
      as_tree_label ("abstract-data"),
      tree (as_tree_label ("abstract"), tree ("")));
    ed->insert_tree (value, path (0, 0, 0));
  }
  else if (op == "make-aux")
    make_aux (
      ed,
      native_action_string (action.value ("env")),
      native_action_string (action.value ("var")),
      native_action_string (action.value ("fallback")));
  else if (op == "make-alternate")
    format_make_alternate (
      native_action_string (action.value ("prompt")),
      object (native_action_string (action.value ("default"))),
      as_tree_label (native_action_string (action.value ("tag"))));
  else if (op == "make-cd") athena_make_commutative_diagram ();
  else if (op == "insert-reverse-hierarchy-graph")
    reverse_hierarchy_graph_insert ();
  else if (op == "make-experimental-build-warning")
    generic_make_experimental_build_warning ();
  else if (op == "business")
    execute_business_id (action.value ("id").toString ());
  else
    FAILED ("unhandled validated native editor action");
}
