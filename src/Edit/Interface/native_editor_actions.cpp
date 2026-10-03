/******************************************************************************
* MODULE     : native_editor_actions.cpp
* DESCRIPTION: Validated parameterized native editor actions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "native_editor_actions.hpp"

#include "ATHENA/Data/program_model.hpp"
#include "document_commands.hpp"
#include "document_style_commands.hpp"
#include "format_commands.hpp"
#include "generic_editor_commands.hpp"
#include "language.hpp"
#include "scheme.hpp"
#include "tree_select.hpp"
#include "Subsystems/Qt/QTMFormulaAstViewer.hpp"
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
popup_spell_range (editor ed, path& first, path& last, string* word= nullptr) {
  if (is_nil (ed)) return false;
  range_set ranges= ed->get_alt_selection ("spell-live");
  path cursor= ed->the_path ();
  for (int i= 0; i + 1 < N(ranges); i += 2) {
    if (!path_less_eq (ranges[i], cursor) ||
        !path_less (cursor, ranges[i + 1]))
      continue;
    first= ranges[i];
    last= ranges[i + 1];
    if (word != nullptr) {
      tree selected= selection_compute (ed->the_root (), first, last);
      if (!is_atomic (selected) || N(selected->label) == 0) return false;
      *word= selected->label;
    }
    return true;
  }
  return false;
}

void
popup_spell_replace (editor ed, string replacement) {
  path first, last;
  if (!popup_spell_range (ed, first, last)) {
    ed->set_message ("No live spelling error at cursor", "spell check");
    return;
  }
  ed->start_editing ();
  range_set selected;
  selected << first << last;
  ed->selection_set_range_set (selected);
  call ("clipboard-cut", object ("dummy"));
  ed->var_insert_tree (tree (replacement), path (N(replacement)));
  ed->end_editing ();
  ed->set_message (
    "Corrected spelling to '" * replacement * "'", "spell check");
}

void
popup_spell_add (editor ed) {
  path first, last;
  string word;
  if (!popup_spell_range (ed, first, last, &word)) {
    ed->set_message ("No live spelling error at cursor", "spell check");
    return;
  }
  tree language= ed->get_env_value ("language", first);
  string lan= is_atomic (language) ? as_string (language):
                                    ed->get_init_string ("language");
  spell_insert (lan, word);
  spell_done (lan);
  ed->set_message (
    "Added '" * word * "' to dictionary", "spell check");
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
    "document-background-pattern",
    "document-background-gradient",
    "document-background-picture",
    "document-no-style",
    "document-toggle-source-mode",
    "document-toggle-preamble-mode",
    "document-update-all",
    "document-update-buffer",
    "document-update-materials",
    "document-update-table-of-contents",
    "document-update-index",
    "document-update-glossary",
    "document-extract-style-file",
    "document-extract-style-package",
    "document-refresh-inclusions",
    "document-refresh-pictures",
    "document-character-count",
    "document-word-count",
    "document-line-count",
    "document-toggle-save-aux",
    "open-document-paragraph-format",
    "open-document-page-format",
    "open-document-metadata",
    "view-toggle-full-screen-edit",
    "view-toggle-full-screen",
    "view-toggle-panorama",
    "view-toggle-slideshow",
    "view-toggle-remote-control",
    "view-fit-screen",
    "view-fit-width",
    "view-toggle-persistent-fit-width",
    "view-toggle-typewriter",
    "view-toggle-snap-pages",
    "view-heading-unfold-all",
    "go-save-position",
    "automate-block-if",
    "automate-block-if-else",
    "automate-block-for",
    "automate-block-while",
    "automate-block-assign",
    "automate-block-intersperse",
    "automate-block-tag",
    "automate-inline-if",
    "automate-inline-if-else",
    "automate-inline-for",
    "automate-inline-while",
    "automate-inline-assign",
    "automate-inline-intersperse",
    "automate-inline-tag",
    "automate-output-string",
    "automate-output-inline",
    "automate-output-block",
    "popup-resolve-artifact",
    "popup-formula-ast",
    "popup-spell-add",
    "editor-clear-selection",
    "editor-clear-undo-history",
    "editor-ai-completion",
    "editor-ai-completion-new-buffer",
    "editor-ai-completion-custom",
    "editor-copy-image",
    "document-flatten-transclusions",
    "focus-transclusion-before",
    "focus-transclusion-select",
    "focus-transclusion-after",
    "focus-materials-append",
    "focus-materials-update",
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
    "slide-insert-graphics",
    "poster-block-toggle-titled",
    "poster-block-toggle-wide",
    "table-toggle-parwidth",
    "sqrt-toggle",
    "dueto-add",
    "document-insert-title",
    "document-insert-abstract",
    "poster-insert-title",
    "tmdoc-insert-title",
    "tmdoc-insert-copyright",
    "poster-insert-up",
    "poster-insert-down",
    "script-insert-up",
    "script-insert-down",
    "document-insert-screens",
    "table-make-subtable",
    "table-join-selected-cells",
    "table-reset-cell-span",
    "edit-focus-macro-source"
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
execute_business_id (editor ed, const QString& id) {
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
  else if (id == "document-background-pattern")
    (void) call ("native-document-background-pattern-dialog");
  else if (id == "document-background-gradient")
    (void) call ("native-document-background-gradient-dialog");
  else if (id == "document-background-picture")
    (void) call ("native-document-background-picture-dialog");
  else if (id == "document-no-style") document_set_no_style ();
  else if (id == "document-toggle-source-mode") document_toggle_source_mode ();
  else if (id == "document-toggle-preamble-mode")
    document_toggle_preamble_mode ();
  else if (id == "document-update-all")
    (void) call ("update-document", object (string ("all")));
  else if (id == "document-update-buffer")
    (void) call ("update-document", object (string ("buffer")));
  else if (id == "document-update-materials")
    (void) call ("update-document", object (string ("materials")));
  else if (id == "document-update-table-of-contents")
    (void) call ("update-document", object (string ("table-of-contents")));
  else if (id == "document-update-index")
    (void) call ("update-document", object (string ("index")));
  else if (id == "document-update-glossary")
    (void) call ("update-document", object (string ("glossary")));
  else if (id == "document-extract-style-file")
    (void) call ("extract-style-file", object (true));
  else if (id == "document-extract-style-package")
    (void) call ("extract-style-file", object (false));
  else if (id == "document-refresh-inclusions")
    (void) call ("inclusions-gc");
  else if (id == "document-refresh-pictures")
    (void) call ("picture-gc");
  else if (id == "document-character-count")
    (void) call ("show-character-count");
  else if (id == "document-word-count")
    (void) call ("show-word-count");
  else if (id == "document-line-count")
    (void) call ("show-line-count");
  else if (id == "document-toggle-save-aux")
    (void) call ("toggle-save-aux");
  else if (id == "open-document-paragraph-format")
    (void) call ("open-document-paragraph-format");
  else if (id == "open-document-page-format")
    (void) call ("open-document-page-format");
  else if (id == "open-document-metadata")
    (void) call ("open-document-metadata");
  else if (id == "view-toggle-full-screen-edit")
    (void) call ("toggle-full-screen-edit-mode");
  else if (id == "view-toggle-full-screen")
    (void) call ("toggle-full-screen-mode");
  else if (id == "view-toggle-panorama")
    (void) call ("toggle-panorama-mode");
  else if (id == "view-toggle-slideshow")
    (void) call ("toggle-slideshow-mode");
  else if (id == "view-toggle-remote-control")
    (void) call ("toggle-remote-control-mode");
  else if (id == "view-fit-screen")
    (void) call ("fit-to-screen");
  else if (id == "view-fit-width")
    (void) call ("fit-to-screen-width");
  else if (id == "view-toggle-persistent-fit-width")
    (void) call ("toggle-persistent-fit-width");
  else if (id == "view-toggle-typewriter")
    (void) call ("toggle-typewriter-mode");
  else if (id == "view-toggle-snap-pages")
    (void) call ("toggle-snap-to-pages");
  else if (id == "view-heading-unfold-all")
    ed->heading_unfold_all ();
  else if (id == "go-save-position")
    (void) call ("cursor-history-add", object (ed->the_path ()));
  else if (id == "automate-block-if") (void) call ("make-block-if");
  else if (id == "automate-block-if-else")
    (void) call ("make-block-if-else");
  else if (id == "automate-block-for") (void) call ("make-block-for");
  else if (id == "automate-block-while") (void) call ("make-block-while");
  else if (id == "automate-block-assign") (void) call ("make-block-assign");
  else if (id == "automate-block-intersperse")
    (void) call ("make-block-intersperse");
  else if (id == "automate-block-tag") (void) call ("make-block-tag");
  else if (id == "automate-inline-if") (void) call ("make-inline-if");
  else if (id == "automate-inline-if-else")
    (void) call ("make-inline-if-else");
  else if (id == "automate-inline-for") (void) call ("make-inline-for");
  else if (id == "automate-inline-while") (void) call ("make-inline-while");
  else if (id == "automate-inline-assign")
    (void) call ("make-inline-assign");
  else if (id == "automate-inline-intersperse")
    (void) call ("make-inline-intersperse");
  else if (id == "automate-inline-tag") (void) call ("make-inline-tag");
  else if (id == "automate-output-string") (void) call ("make-output-string");
  else if (id == "automate-output-inline") (void) call ("make-inline-output");
  else if (id == "automate-output-block") (void) call ("make-block-output");
  else if (id == "popup-resolve-artifact") {
    if (!ed->selection_active_any ()) return;
    (void) call ("resolve-selection-as-artifact-name");
  }
  else if (id == "popup-formula-ast") {
    path root= ed->semantic_root (ed->the_path ());
    if (!ed->test_subtree (root)) {
      ed->set_message (
        "The current formula could not be resolved.", "Formula AST");
      return;
    }
    ast_viewer_show_tree (copy (ed->the_subtree (root)), "Formula AST");
  }
  else if (id == "popup-spell-add")
    popup_spell_add (ed);
  else if (id == "editor-clear-selection")
    ed->selection_clear ("primary");
  else if (id == "editor-clear-undo-history")
    ed->clear_undo_history ();
  else if (id == "editor-ai-completion")
    (void) call ("codex-ai-completion");
  else if (id == "editor-ai-completion-new-buffer")
    (void) call ("codex-ai-completion-new-buffer");
  else if (id == "editor-ai-completion-custom")
    (void) call ("codex-ai-completion-custom");
  else if (id == "editor-copy-image")
    (void) call ("clipboard-copy-image", object (string ("")));
  else if (id == "document-flatten-transclusions")
    (void) call ("vault-flatten-document");
  else if (id == "focus-transclusion-before")
    (void) call ("vault-go-before-transclusion");
  else if (id == "focus-transclusion-select")
    (void) call ("vault-select-transclusion");
  else if (id == "focus-transclusion-after")
    (void) call ("vault-go-after-transclusion");
  else if (id == "focus-materials-append")
    (void) call ("materials-append-references");
  else if (id == "focus-materials-update")
    (void) call ("materials-update-current-document");
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
  if (op == "make-program")
    return has_string (action, "language") &&
           !action.value ("language").toString ().trimmed ().isEmpty () ?
             true : fail_validation (error, "make-program requires language");
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
  if (op == "document-package")
    return has_string (action, "action") && has_string (action, "name") ?
             true : fail_validation (error, "document-package requires action/name");
  if (op == "document-edit-style")
    return true;
  if (op == "document-install-style")
    return has_string (action, "path") ?
             true : fail_validation (error, "document-install-style requires path");
  if (op == "export-pdf-embedded")
    return has_string (action, "path") ?
             true : fail_validation (
               error, "export-pdf-embedded requires path");
  if (op == "document-default-theme")
    return true;
  if (op == "popup-spell-replace")
    return has_string (action, "replacement") ?
             true : fail_validation (
               error, "popup-spell-replace requires replacement");
  if (op == "selection-clipboard") {
    if (!has_string (action, "operation") || !has_string (action, "key"))
      return fail_validation (
        error, "selection-clipboard requires operation/key");
    const QString operation= action.value ("operation").toString ();
    if (!QSet<QString> {"copy", "cut", "paste"}.contains (operation))
      return fail_validation (error, "invalid selection-clipboard operation");
    return !action.contains ("format") || has_string (action, "format") ?
             true : fail_validation (
               error, "selection-clipboard format must be a string");
  }
  if (op == "selection-format-default") {
    if (!has_string (action, "direction") || !has_string (action, "format"))
      return fail_validation (
        error, "selection-format-default requires direction/format");
    return QSet<QString> {"import", "export"}.contains (
             action.value ("direction").toString ()) ?
             true : fail_validation (
               error, "invalid selection-format-default direction");
  }
  if (op == "redo-branch")
    return action.value ("index").isDouble () &&
           action.value ("index").toInt (-1) >= 0 ?
             true : fail_validation (
               error, "redo-branch requires nonnegative index");
  if (op == "focus-materials-style")
    return has_string (action, "style") ?
             true : fail_validation (
               error, "focus-materials-style requires style");
  if (op == "document-citation-style") {
    if (!has_bool (action, "default"))
      return fail_validation (error, "document-citation-style requires default");
    return action.value ("default").toBool () || has_string (action, "style") ?
             true : fail_validation (
               error, "document-citation-style requires style when non-default");
  }
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
  if (op == "focus-hidden-field")
    return action.value ("index").isDouble () &&
           action.value ("index").toInt (-1) >= 0 &&
           has_string (action, "value") ?
             true : fail_validation (
               error, "focus-hidden-field requires index/value");
  if (op == "focus-automatic-section-rename")
    return has_string (action, "value") ?
             true : fail_validation (
               error, "focus-automatic-section-rename requires value");
  if (op == "focus-set-cell-mode") {
    if (!has_string (action, "mode"))
      return fail_validation (error, "focus-set-cell-mode requires mode");
    return QSet<QString> {"cell", "row", "column", "table"}.contains (
             action.value ("mode").toString ()) ?
             true : fail_validation (error, "invalid cell mode");
  }
  if (op == "focus-set-effect-pen") {
    if (!has_string (action, "pen"))
      return fail_validation (error, "focus-set-effect-pen requires pen");
    return QSet<QString> {
      "gaussian", "oval", "rectangular", "motion"
    }.contains (action.value ("pen").toString ()) ?
      true : fail_validation (error, "invalid effect pen");
  }
  if (op == "focus-overlay-switch")
    return action.value ("index").isDouble () &&
           action.value ("index").toInt (-1) >= 1 ?
             true : fail_validation (
               error, "focus-overlay-switch requires positive index");
  if (op == "focus-overlay-reference")
    return action.value ("index").isDouble () &&
           action.value ("index").toInt (-1) >= 1 ?
             true : fail_validation (
               error, "focus-overlay-reference requires positive index");
  if (op == "focus-text-data") {
    if (!has_string (action, "kind"))
      return fail_validation (error, "focus-text-data requires kind");
    const QString kind= action.value ("kind").toString ();
    if (kind == "title-hidden-toggle") return true;
    if (!has_string (action, "value"))
      return fail_validation (error, "focus-text-data requires value");
    const QString value= action.value ("value").toString ();
    if (kind == "title-element")
      return QSet<QString> {
        "doc-subtitle", "doc-author", "doc-date", "today",
        "doc-misc", "doc-note", "doc-running-title", "doc-running-author"
      }.contains (value) ? true :
        fail_validation (error, "invalid title element");
    if (kind == "author-element")
      return QSet<QString> {
        "author-affiliation", "author-email", "author-homepage",
        "author-misc", "author-note"
      }.contains (value) ? true :
        fail_validation (error, "invalid author element");
    if (kind == "abstract-element")
      return QSet<QString> {
        "abstract-arxiv", "abstract-acm", "abstract-msc",
        "abstract-pacs", "abstract-keywords"
      }.contains (value) ? true :
        fail_validation (error, "invalid abstract element");
    if (kind == "title-clustering")
      return QSet<QString> {
        "none", "affiliation", "all"
      }.contains (value) ? true :
        fail_validation (error, "invalid title clustering");
    return fail_validation (error, "invalid focus-text-data kind");
  }
  if (op == "focus-section-switch")
    return action.value ("index").isDouble () &&
           action.value ("index").toInt (-1) >= 0 ?
             true : fail_validation (
               error, "focus-section-switch requires nonnegative index");
  if (op == "focus-embedded-image") {
    if (!has_string (action, "kind"))
      return fail_validation (error, "focus-embedded-image requires kind");
    const QString kind= action.value ("kind").toString ();
    if (QSet<QString> {"save-all", "link-all", "embed-this", "embed-all"}
          .contains (kind))
      return true;
    if (!QSet<QString> {"save-as", "link-as", "link-copies-as"}.contains (kind))
      return fail_validation (error, "invalid embedded image action");
    return has_string (action, "path") ?
      true : fail_validation (error, "embedded image action requires path");
  }
  if (op == "focus-document-package") {
    if (!has_string (action, "action") || !has_string (action, "name"))
      return fail_validation (
        error, "focus-document-package requires action/name");
    const QString kind= action.value ("action").toString ();
    if (kind != "add" && kind != "remove" &&
        kind != "edit" && kind != "toggle")
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
  else if (op == "make-program") {
    const string language= native_action_string (action.value ("language"));
    const std::string native_language (
      language.data (), static_cast<std::size_t> (N(language)));
    tree selected= "";
    if (ed->selection_active_normal ()) selected= ed->selection_get_cut ();
    tree value= athena::program::create (native_language);
    ed->insert_tree (value, path (0, 0, 0));
    if (selected != "") ed->insert_tree (selected, end (selected));
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
  else if (op == "document-package") {
    actor_document_menu_snapshot state= ed->document_menu_state_snapshot ();
    if (!state.ready) return;
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
    else if (kind == "toggle" && available)
      document_toggle_style_package (name);
    else if (kind == "remove" && current)
      document_remove_style_package (name);
    else if (kind == "edit" && current)
      (void) call ("edit-package-source", object (name));
  }
  else if (op == "document-edit-style") {
    if (!ed->document_menu_state_snapshot ().ready) return;
    (void) call ("edit-style-source");
  }
  else if (op == "document-install-style") {
    if (!ed->document_menu_state_snapshot ().ready) return;
    url source= url_system (native_action_string (action.value ("path")));
    if (document_install_custom_style (source))
      document_set_main_style (document_custom_style_file_name (source));
  }
  else if (op == "export-pdf-embedded")
    (void) call (
      "wrapped-print-to-pdf-embeded-with-tm",
      object (url_system (native_action_string (action.value ("path")))));
  else if (op == "document-default-theme") {
    actor_document_menu_snapshot state= ed->document_menu_state_snapshot ();
    if (!state.ready || state.document_theme_kind != "basic") return;
    for (const auto& theme: state.document_themes)
      if (theme.checked) {
        string name (
          theme.value.data (), static_cast<int> (theme.value.size ()));
        document_remove_style_package (name);
      }
  }
  else if (op == "popup-spell-replace")
    popup_spell_replace (
      ed, native_action_string (action.value ("replacement")));
  else if (op == "selection-clipboard") {
    const QString operation= action.value ("operation").toString ();
    const string key= native_action_string (action.value ("key"));
    const bool hasFormat= action.contains ("format");
    string previous;
    if (hasFormat) {
      const string format= native_action_string (action.value ("format"));
      if (operation == "paste") {
        previous= ed->selection_get_import ();
        ed->selection_set_import (format);
      }
      else {
        previous= ed->selection_get_export ();
        ed->selection_set_export (format);
      }
    }
    if (operation == "copy") ed->selection_copy (key);
    else if (operation == "cut") ed->selection_cut (key);
    else ed->selection_paste (key);
    if (hasFormat) {
      if (operation == "paste") ed->selection_set_import (previous);
      else ed->selection_set_export (previous);
    }
  }
  else if (op == "selection-format-default") {
    const string format= native_action_string (action.value ("format"));
    if (action.value ("direction").toString () == "import")
      ed->selection_set_import (format);
    else
      ed->selection_set_export (format);
  }
  else if (op == "redo-branch") {
    const int index= action.value ("index").toInt (-1);
    if (index < 0 || index >= ed->redo_possibilities ()) return;
    if (ed->editor_command_state_snapshot ().read_only ()) return;
    ed->redo (index);
  }
  else if (op == "focus-materials-style") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () || !state.referenced_materials_context) return;
    (void) call (
      "materials-set-reference-style",
      object (native_action_string (action.value ("style"))));
  }
  else if (op == "document-citation-style") {
    if (!ed->document_menu_state_snapshot ().ready) return;
    if (action.value ("default").toBool ())
      ed->init_default ("materials-csl-style");
    else
      ed->init_env (
        "materials-csl-style",
        tree (native_action_string (action.value ("style"))));
    (void) call ("materials-update-current-document");
  }
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
    else if (id == "poster-block-toggle-titled" &&
             state.poster_block_context)
      (void) call ("block-toggle-titled", object (target));
    else if (id == "poster-block-toggle-wide" &&
             state.poster_block_context)
      (void) call ("block-toggle-wide", object (target));
    else if (id == "table-toggle-parwidth" &&
             state.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT))
      (void) call ("table-toggle-parwidth");
    else if (id == "sqrt-toggle" && state.sqrt_context)
      (void) call ("sqrt-toggle", object (target));
    else if (id == "dueto-add" && state.dueto_available)
      (void) call ("dueto-add", object (target));
    else if (id == "document-insert-title" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
             state.document_insert_title_available &&
             !state.poster_insert_title_available &&
             !state.tmdoc_insert_title_available)
      (void) call ("make-doc-data");
    else if (id == "document-insert-abstract" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
             state.document_insert_abstract_available)
      (void) call ("make-abstract-data");
    else if (id == "poster-insert-title" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
             state.poster_insert_title_available)
      (void) call ("make-poster-title");
    else if (id == "tmdoc-insert-title" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
             state.tmdoc_insert_title_available)
      (void) call ("tmdoc-insert-title");
    else if (id == "tmdoc-insert-copyright" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
             state.tmdoc_insert_copyright_available)
      (void) call ("tmdoc-insert-copyright-and-license");
    else if (id == "poster-insert-up" && state.poster_block_context)
      generic_structured_insert_up ();
    else if (id == "poster-insert-down" && state.poster_block_context)
      generic_structured_insert_down ();
    else if (id == "script-insert-up" && state.script_insert_up)
      generic_structured_insert_up ();
    else if (id == "script-insert-down" && state.script_insert_down)
      generic_structured_insert_down ();
    else if (id == "document-insert-screens" &&
             state.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
             state.document_insert_screens_available)
      (void) call ("make-screens");
    else if (id == "edit-focus-macro-source" &&
             state.tag_extension && state.tag_macro_source_available)
      (void) call ("edit-focus-macro-source");
    else if (id == "table-make-subtable" &&
             state.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT) &&
             state.table_subtable_available)
      ed->make_subtable ();
    else if (id == "table-join-selected-cells" &&
             state.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT) &&
             state.table_join_cells_available) {
      array<int> cells= ed->table_which_cells ();
      if (N(cells) != 4 || cells[1] < cells[0] || cells[3] < cells[2])
        return;
      ed->table_go_to (cells[0], cells[2]);
      ed->selection_cancel ();
      ed->cell_set_format (
        "cell-row-span", tree (as_string (cells[1] + 1 - cells[0])));
      ed->cell_set_format (
        "cell-col-span", tree (as_string (cells[3] + 1 - cells[2])));
    }
    else if (id == "table-reset-cell-span" &&
             state.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT) &&
             state.table_reset_span_available) {
      ed->cell_set_format ("cell-row-span", tree ("1"));
      ed->cell_set_format ("cell-col-span", tree ("1"));
    }
  }
  else if (op == "focus-hidden-field") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !ed->test_subtree (focus) ||
        state.pure_alternate_context ||
        state.overlays_context || state.overlay_context)
      return;
    const int index= action.value ("index").toInt (-1);
    bool allowed= false;
    for (const auto& field: state.hidden_fields)
      if (field.index == index) {
        allowed= true;
        break;
      }
    if (!allowed) return;
    (void) generic_focus_set_hidden_child (
      ed->the_subtree (focus), index,
      native_action_string (action.value ("value")));
  }
  else if (op == "focus-automatic-section-rename") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () || !state.automatic_section_context) return;
    (void) call (
      "automatic-section-rename",
      object (native_action_string (action.value ("value"))));
  }
  else if (op == "focus-set-cell-mode") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () ||
        !state.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT))
      return;
    ed->set_cell_mode (
      native_action_string (action.value ("mode")));
  }
  else if (op == "focus-set-effect-pen") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !state.pen_effect_context ||
        !ed->test_subtree (focus))
      return;
    format_set_effect_pen (
      object (ed->the_subtree (focus)),
      native_action_string (action.value ("pen")));
  }
  else if (op == "focus-overlay-switch") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !ed->test_subtree (focus) ||
        (!state.overlays_context && !state.overlay_context))
      return;
    const int index= action.value ("index").toInt (-1);
    if (index < 1 || index > state.overlay_count) return;
    tree target= ed->the_subtree (focus);
    (void) call (
      "native-overlays-switch-parent", object (target), object (index));
  }
  else if (op == "focus-overlay-reference") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !state.overlay_context ||
        !ed->test_subtree (focus))
      return;
    const int index= action.value ("index").toInt (-1);
    if (index < 1 || index > state.overlay_count) return;
    tree target= ed->the_subtree (focus);
    if (N(target) == 0) return;
    (void) tree_set (target, 0, tree (as_string (index)));
  }
  else if (op == "focus-text-data") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid ()) return;
    const QString kind= action.value ("kind").toString ();
    const string value=
      has_string (action, "value") ?
        native_action_string (action.value ("value")) : string ("");
    if (kind == "title-hidden-toggle") {
      if ((state.has (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT) ||
           state.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT)) &&
          state.title_hidden_available)
        (void) call ("doc-data-activate-toggle");
      return;
    }
    if (kind == "title-element") {
      if (!state.has (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT) &&
          !state.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT))
        return;
      if (value == "today") {
        (void) call ("make-doc-data-element", symbol_object ("doc-date"));
        (void) call ("make", symbol_object ("date"), object (0));
      }
      else
        (void) call ("make-doc-data-element", symbol_object (value));
      return;
    }
    if (kind == "author-element") {
      if (!state.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT)) return;
      (void) call ("make-author-data-element", symbol_object (value));
      return;
    }
    if (kind == "abstract-element") {
      if (!state.has (ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT)) return;
      (void) call ("make-abstract-data-element", symbol_object (value));
      return;
    }
    if (kind == "title-clustering") {
      if (!state.has (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT) &&
          !state.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT))
        return;
      if (value == "none")
        (void) call ("set-doc-title-clustering", object (false));
      else if (value == "affiliation")
        (void) call (
          "set-doc-title-clustering",
          object (string ("cluster-by-affiliation")));
      else if (value == "all")
        (void) call (
          "set-doc-title-clustering", object (string ("cluster-all")));
    }
  }
  else if (op == "focus-section-switch") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    if (!state.valid () || !state.section_navigation_available) return;
    const int index= action.value ("index").toInt (-1);
    if (index < 0 ||
        index >= static_cast<int> (state.section_names.size ()))
      return;
    (void) call ("native-section-switch-to", object (index));
  }
  else if (op == "focus-embedded-image") {
    actor_focus_toolbar_snapshot state= ed->focus_toolbar_state_snapshot ();
    path focus= ed->focus_get ();
    if (!state.valid () || !ed->test_subtree (focus)) return;
    tree target= ed->the_subtree (focus);
    const QString kind= action.value ("kind").toString ();
    if (kind == "save-as" && state.embedded_image_context)
      generic_save_embedded_image (
        target, url_system (native_action_string (action.value ("path"))));
    else if (kind == "link-as" && state.embedded_image_context)
      generic_link_embedded_image (
        target, url_system (native_action_string (action.value ("path"))));
    else if (kind == "link-copies-as" && state.embedded_image_context)
      generic_link_embedded_image_copies (
        target, url_system (native_action_string (action.value ("path"))));
    else if (kind == "save-all" && state.embedded_image_context)
      generic_save_all_embedded_images ();
    else if (kind == "link-all" && state.embedded_image_context)
      generic_link_all_embedded_images ();
    else if (kind == "embed-this" && state.linked_image_context)
      generic_embed_image (target);
    else if (kind == "embed-all" && state.linked_image_context)
      generic_embed_all_images ();
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
    else if (kind == "toggle" && available)
      document_toggle_style_package (name);
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
    execute_business_id (ed, action.value ("id").toString ());
  else
    FAILED ("unhandled validated native editor action");
}
