/******************************************************************************
* MODULE     : native_editor_actions.cpp
* DESCRIPTION: Validated parameterized native editor actions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "native_editor_actions.hpp"

#include "document_commands.hpp"
#include "document_style_commands.hpp"
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
    "open-document-font-selector",
    "letter-today",
    "tmdoc-explain-synopsis",
    "make-alter-colors"
  };
  return ids.contains (id);
}

bool
valid_focus_action_id (const QString& id) {
  static const QSet<QString> ids {
    "numbered-toggle",
    "alternate-toggle",
    "inactive-toggle",
    "algorithm-toggle-number",
    "algorithm-toggle-name",
    "algorithm-toggle-specification",
    "note-toggle-custom",
    "titled-toggle-name",
    "frame-toggle-title",
    "float-toggle-wide",
    "floatable-toggle-wide",
    "turn-floating",
    "turn-non-floating",
    "cursor-toggle-anchor",
    "set-marginal-note-hpos",
    "set-marginal-note-valign",
    "set-balloon-halign",
    "set-balloon-valign",
    "toggle-insertion-positioning",
    "toggle-insertion-positioning-not",
    "slide-insert-title",
    "slide-insert-graphics"
  };
  return ids.contains (id);
}

bool
valid_focus_action_value (const QJsonObject& action, QString* error) {
  const QString id= action.value ("id").toString ();
  const bool needsValue=
    id == "set-marginal-note-hpos" ||
    id == "set-marginal-note-valign" ||
    id == "set-balloon-halign" ||
    id == "set-balloon-valign" ||
    id == "toggle-insertion-positioning" ||
    id == "toggle-insertion-positioning-not";
  if (!needsValue) return true;
  if (!has_string (action, "value"))
    return fail_validation (error, "focus action requires value");
  const QString value= action.value ("value").toString ();
  if (id == "set-marginal-note-hpos")
    return QSet<QString> {
      "normal", "left", "right", "even-left", "even-right"
    }.contains (value) ? true :
      fail_validation (error, "invalid marginal note horizontal position");
  if (id == "set-marginal-note-valign")
    return QSet<QString> {"t", "c", "b"}.contains (value) ? true :
      fail_validation (error, "invalid marginal note vertical alignment");
  if (id == "set-balloon-halign")
    return QSet<QString> {
      "Left", "left", "center", "right", "Right"
    }.contains (value) ? true :
      fail_validation (error, "invalid balloon horizontal alignment");
  if (id == "set-balloon-valign")
    return QSet<QString> {
      "Bottom", "bottom", "center", "top", "Top"
    }.contains (value) ? true :
      fail_validation (error, "invalid balloon vertical alignment");
  if (id == "toggle-insertion-positioning")
    return QSet<QString> {"t", "h", "b"}.contains (value) ? true :
      fail_validation (error, "invalid float position");
  if (id == "toggle-insertion-positioning-not")
    return value == "f" ? true :
      fail_validation (error, "invalid excluded float position");
  return true;
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
  else if (id == "open-document-font-selector")
    (void) call ("open-document-font-selector");
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
  if (op == "init-env")
    return has_string (action, "var") && has_string (action, "value") ?
             true : fail_validation (error, "init-env requires var/value");
  if (op == "init-default")
    return has_string (action, "var") ?
             true : fail_validation (error, "init-default requires var");
  if (op == "set-main-style")
    return has_string (action, "style") ?
             true : fail_validation (error, "set-main-style requires style");
  if (op == "set-document-language")
    return has_string (action, "language") ?
             true : fail_validation (
               error, "set-document-language requires language");
  if (op == "set-default-document-language")
    return true;
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
  if (op == "focus-variant")
    return has_string (action, "tag") ?
             true : fail_validation (error, "focus-variant requires tag");
  if (op == "focus-action")
    return has_string (action, "id") &&
           valid_focus_action_id (action.value ("id").toString ()) &&
           valid_focus_action_value (action, error) ?
             true : fail_validation (error, "unknown focus action id");
  if (op == "focus-set-label")
    return has_string (action, "value") ?
             true : fail_validation (error, "focus-set-label requires value");
  if (op == "focus-parameter") {
    if (!has_string (action, "scope") || !has_string (action, "name"))
      return fail_validation (
        error, "focus-parameter requires scope/name");
    const QString scope= action.value ("scope").toString ();
    if (scope != "global" && scope != "local")
      return fail_validation (error, "invalid focus parameter scope");
    const bool reset= action.value ("reset").toBool (false);
    if (!reset && !has_string (action, "value"))
      return fail_validation (
        error, "focus-parameter requires value unless reset");
    return true;
  }
  if (op == "focus-style-option")
    return has_string (action, "name") ?
             true : fail_validation (
               error, "focus-style-option requires name");
  if (op == "focus-search")
    return true;
  if (op == "focus-document-package") {
    if (!has_string (action, "action") || !has_string (action, "name"))
      return fail_validation (
        error, "focus-document-package requires action/name");
    const QString kind= action.value ("action").toString ();
    if (kind != "add" && kind != "remove" && kind != "edit")
      return fail_validation (
        error, "invalid focus document package action");
    return true;
  }
  if (op == "focus-document-edit-style")
    return true;
  if (op == "focus-document-install-style")
    return has_string (action, "path") ?
             true : fail_validation (
               error, "focus-document-install-style requires path");
  if (op == "focus-document-default-theme")
    return true;
  if (op == "focus-slide-switch")
    return action.value ("index").isDouble () &&
           action.value ("index").toInt (-1) >= 0 ?
             true : fail_validation (
               error, "focus-slide-switch requires nonnegative index");
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
  else if (op == "init-env")
    ed->init_env (
      native_action_string (action.value ("var")),
      tree (native_action_string (action.value ("value"))));
  else if (op == "init-default")
    ed->init_default (native_action_string (action.value ("var")));
  else if (op == "set-main-style")
    document_set_main_style (
      native_action_string (action.value ("style")));
  else if (op == "set-document-language")
    document_set_language (
      native_action_string (action.value ("language")));
  else if (op == "set-default-document-language")
    document_set_default_language ();
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
  else if (op == "focus-variant") {
    path focus= ed->focus_get ();
    if (!ed->test_subtree (focus)) return;
    tree target= ed->the_subtree (focus);
    string tag= native_action_string (action.value ("tag"));
    object variants= call ("focus-variants-of", object (target));
    bool allowed= false;
    if (is_list (variants)) {
      array<object> values= as_array_object (variants);
      for (int i=0; i<N(values); ++i) {
        string value;
        if (is_symbol (values[i])) value= as_symbol (values[i]);
        else if (is_string (values[i])) value= as_string (values[i]);
        else continue;
        if (value == tag) {
          allowed= true;
          break;
        }
      }
    }
    if (allowed) (void) call ("variant-set", object (target), symbol_object (tag));
  }
  else if (op == "focus-action") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !ed->test_subtree (focus)) return;
    tree target= ed->the_subtree (focus);
    const QString id= action.value ("id").toString ();
    if (id == "numbered-toggle" && state.numbered_available)
      (void) call ("numbered-toggle", object (target));
    else if (id == "alternate-toggle" && state.alternate_available)
      (void) call ("alternate-toggle", object (target));
    else if (id == "inactive-toggle" && state.hidden_toggle_available)
      (void) call ("inactive-toggle", object (target));
    else if (id == "algorithm-toggle-number" &&
             state.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT) &&
             !state.algorithm_named)
      (void) call ("algorithm-toggle-number", object (target));
    else if (id == "algorithm-toggle-name" &&
             state.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT))
      (void) call ("algorithm-toggle-name", object (target));
    else if (id == "algorithm-toggle-specification" &&
             state.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT))
      (void) call ("algorithm-toggle-specification", object (target));
    else if (id == "note-toggle-custom" &&
             state.has (ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT))
      (void) call ("note-toggle-custom", object (target));
    else if (id == "titled-toggle-name" &&
             state.has (ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT) &&
             state.figure_context)
      (void) call ("titled-toggle-name", object (target));
    else if (id == "frame-toggle-title" &&
             state.has (ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT))
      (void) call ("frame-toggle-title", object (target));
    else if (id == "float-toggle-wide" &&
             state.multicol_style &&
             (state.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
              state.has (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT)))
      (void) call ("float-toggle-wide", object (target));
    else if (id == "floatable-toggle-wide" &&
             state.multicol_style &&
             state.has (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT))
      (void) call ("floatable-toggle-wide", object (target));
    else if (id == "turn-floating" &&
             state.has (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT))
      (void) call ("turn-floating", object (target));
    else if (id == "turn-non-floating" &&
             state.float_context_available) {
      path p= ed->search_upwards ("float");
      if (is_nil (p)) p= ed->search_upwards ("wide-float");
      if (!is_nil (p) && ed->test_subtree (p))
        (void) call ("turn-non-floating", object (ed->the_subtree (p)));
    }
    else if (id == "cursor-toggle-anchor" &&
             (state.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
              state.has (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT)))
      (void) call ("cursor-toggle-anchor");
    else if (id == "set-marginal-note-hpos" &&
             state.has (ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT))
      generic_set_marginal_note_hpos (
        native_action_string (action.value ("value")));
    else if (id == "set-marginal-note-valign" &&
             state.has (ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT))
      generic_set_marginal_note_valign (
        native_action_string (action.value ("value")));
    else if (id == "set-balloon-halign" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT))
      generic_set_balloon_halign (
        native_action_string (action.value ("value")));
    else if (id == "set-balloon-valign" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT))
      generic_set_balloon_valign (
        native_action_string (action.value ("value")));
    else if (id == "toggle-insertion-positioning" &&
             (state.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
              state.has (ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT)))
      generic_toggle_insertion_positioning (
        native_action_string (action.value ("value")));
    else if (id == "toggle-insertion-positioning-not" &&
             (state.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
              state.has (ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT)))
      generic_toggle_insertion_positioning_not (
        native_action_string (action.value ("value")));
    else if (id == "slide-insert-title" &&
             state.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) &&
             state.slide_propose_title)
      (void) call ("native-slide-insert-title", object (target));
    else if (id == "slide-insert-graphics" &&
             state.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) &&
             state.slide_propose_graphics)
      (void) call ("native-slide-insert-graphics", object (target));
  }
  else if (op == "focus-set-label") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () ||
        !state.has (ACTOR_FOCUS_TOOLBAR_HAS_LABEL) ||
        !ed->test_subtree (focus))
      return;
    (void) generic_focus_set_label (
      ed->the_subtree (focus),
      native_action_string (action.value ("value")));
  }
  else if (op == "focus-parameter") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid ()) return;
    const string scope= native_action_string (action.value ("scope"));
    const string name= native_action_string (action.value ("name"));
    const std::vector<actor_focus_parameter_snapshot>* parameters=
      scope == "global" ? &state.global_parameters:
      scope == "local" ? &state.local_parameters: nullptr;
    if (parameters == nullptr) return;
    const std::string nativeName (
      name.data (), static_cast<std::size_t> (N(name)));
    bool allowed= false;
    for (const auto& parameter: *parameters)
      if (parameter.name == nativeName) {
        allowed= true;
        break;
      }
    if (!allowed) return;

    object mode;
    if (scope == "global") mode= keyword_object ("global");
    else {
      path focus= ed->focus_get ();
      if (!ed->test_subtree (focus)) return;
      tree target= ed->the_subtree (focus);
      array<object> items;
      items << keyword_object ("local")
            << symbol_object (as_string (L (target)));
      mode= as_list_object (items);
    }
    if (action.value ("reset").toBool (false))
      generic_parameter_reset (name, mode);
    else
      generic_parameter_set (
        name, object (native_action_string (action.value ("value"))), mode);
  }
  else if (op == "focus-style-option") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid ()) return;
    const string name= native_action_string (action.value ("name"));
    const std::string nativeName (
      name.data (), static_cast<std::size_t> (N(name)));
    bool allowed= false;
    for (const auto& option: state.style_options)
      if (option.name == nativeName) {
        allowed= true;
        break;
      }
    if (allowed) document_toggle_style_package (name);
  }
  else if (op == "focus-search") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !ed->test_subtree (focus) ||
        (!state.has (ACTOR_FOCUS_TOOLBAR_HAS_SEARCH_MENU) &&
         !state.has (ACTOR_FOCUS_TOOLBAR_CAN_SEARCH)))
      return;
    generic_focus_open_search_tool (ed->the_subtree (focus));
  }
  else if (op == "focus-document-package") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () ||
        (!state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
         !state.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)))
      return;
    const string name= native_action_string (action.value ("name"));
    const std::string nativeName (
      name.data (), static_cast<std::size_t> (N(name)));
    const QString kind= action.value ("action").toString ();
    bool available= false;
    for (const auto& package: state.document_packages)
      if (package.value == nativeName) {
        available= true;
        break;
      }
    bool current= false;
    for (const auto& package: state.current_packages)
      if (package.value == nativeName) {
        current= true;
        break;
      }
    if (kind == "add" && available) {
      if (!document_has_style_package (name))
        document_add_style_package (name);
    }
    else if (kind == "remove" && current)
      document_remove_style_package (name);
    else if (kind == "edit" && current)
      (void) call ("edit-package-source", object (name));
  }
  else if (op == "focus-document-edit-style") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () ||
        (!state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
         !state.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)))
      return;
    (void) call ("edit-style-source");
  }
  else if (op == "focus-document-install-style") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () ||
        (!state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
         !state.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)))
      return;
    url source= url_system (
      native_action_string (action.value ("path")));
    if (document_install_custom_style (source))
      document_set_main_style (document_custom_style_file_name (source));
  }
  else if (op == "focus-document-default-theme") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () || state.document_theme_kind != "basic") return;
    for (const auto& theme: state.document_themes)
      if (theme.checked) {
        string name (
          theme.value.data (), static_cast<int> (theme.value.size ()));
        document_remove_style_package (name);
      }
  }
  else if (op == "focus-slide-switch") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () ||
        !state.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT))
      return;
    const int index= action.value ("index").toInt (-1);
    if (index < 0 ||
        index >= static_cast<int> (state.slide_names.size ()))
      return;
    (void) call ("screens-switch-to", object (index));
  }
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
