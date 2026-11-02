// Copyright (C) 1999 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "editing_utilities.hpp"
#include "native_math_edit.hpp"
#include "presentation_commands.hpp"
#include "editor.hpp"
#include "tree_correct.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "format_geometry.hpp"
#include "native_node_groups.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "Data/Convert/Xml/math_source_normalization.hpp"
#include "Scheme/Scheme/native_interfaces.hpp"

namespace {
string variant_text (QString value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

tree variant_tree (QJsonValue value) {
  if (value.isString ()) return variant_text (value.toString ());
  QJsonArray parts= value.toArray ();
  ASSERT (!parts.isEmpty () && parts[0].isString (), "invalid mathematical variant");
  tree result (make_tree_label (variant_text (parts[0].toString ())));
  for (int i=1; i<parts.size (); ++i) result << variant_tree (parts[i]);
  return result;
}

const QJsonObject& math_variants () {
  static const QJsonObject data= [] {
    string source;
    ASSERT (!load_string (url ("$ATHENA_PATH/misc/input/math-variants.json"), source, false),
            "cannot read math-variants.json");
    QJsonParseError error;
    auto document= QJsonDocument::fromJson (QByteArray (source.data (), N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject () &&
            document.object ().value ("version").toInt () == 1,
            "invalid math-variants.json");
    return document.object ();
  } ();
  return data;
}

void cycle_variant (tree target, tree original, QJsonArray values, bool forward) {
  for (int i=0; i<values.size (); ++i)
    if (original == variant_tree (values[i])) {
      tree_assign (target, variant_tree (values[(i + (forward ? 1 : values.size ()-1)) % values.size ()]));
      return;
    }
}

bool around (tree t) { return is_func (t, AROUND) || is_func (t, VAR_AROUND); }
bool delimiter (tree t) {
  return is_func (t, LEFT) || is_func (t, MID) || is_func (t, RIGHT);
}
void cycle_bracket (tree t, bool forward, const char* side) {
  if (around (t) && N(t) == 3) {
    cycle_bracket (t[0], forward, "left");
    cycle_bracket (t[2], forward, "right");
  }
  else if (delimiter (t) && N(t) > 1)
    cycle_bracket (t[0], forward, is_func (t, LEFT) ? "left" :
                   is_func (t, RIGHT) ? "right" : "middle");
  else if (is_atomic (t)) cycle_variant (t, t, math_variants ().value (side).toArray (), forward);
}

void resize_bracket (tree t, int by) {
  if (delimiter (t) && N(t) >= 2) resize_bracket (t[1], by);
  else if (around (t) && N(t) == 3) {
    for (int index: {0, 2}) {
      if (is_atomic (t[index]))
        tree_assign (t[index], tree (index == 0 ? LEFT : RIGHT, copy (t[index]), "0"));
      resize_bracket (t[index], by);
      if (is_compound (t[index]) && N(t[index]) >= 2 && t[index][1] == "0")
        tree_assign (t[index], copy (t[index][0]));
    }
  }
  else if (is_int (t)) tree_assign (t, as_string (as_int (t)+by));
  else if (geometry_rich_length (t)) geometry_length_increase (t, by);
}
}

bool math_bracket_geometry (tree t, bool down, bool reset) {
  if (reset) {
    if (!around (t)) return false;
    if (N(t) == 3)
      for (int index: {0, 2})
        if (delimiter (t[index]) && N(t[index]) > 0)
          tree_assign (t[index], copy (t[index][0]));
  }
  else {
    if (!around (t) && !delimiter (t)) return false;
    resize_bracket (t, down ? -1 : 1);
  }
  return true;
}

void native_variant_circulate (tree t, bool forward) {
  if (presentation_variant (t, forward)) return;
  if (math_script_context (t)) {
    if (math_script_only (t)) {
      tree_label label= is_func (t, LSUB) ? LSUP : is_func (t, LSUP) ? LSUB :
                        is_func (t, RSUB) ? RSUP : RSUB;
      editing_variant_set (t, label);
    }
    return;
  }
  if ((is_func (t, WIDE) || is_func (t, VAR_WIDE)) && N(t) == 2) {
    tree accent= t[1];
    if (is_func (accent, WITH, 3) && accent[0] == "math-accent-stretch" && accent[1] == "true")
      accent= accent[2];
    if (is_atomic (accent)) {
      tree original= copy (accent);
      for (QJsonValue group: math_variants ().value ("accents").toArray ())
        cycle_variant (accent, original, group.toArray (), forward);
    }
    return;
  }
  if (around (t) || delimiter (t)) { cycle_bracket (t, forward, "middle"); return; }
  if (is_func (t, BIG_AROUND, 2)) {
    cycle_variant (t[0], t[0], math_variants ().value ("big").toArray (), forward);
    return;
  }
  if (editing_variant_context (t)) {
    array<object> choices= as_array_object (native_node_variants (symbol_object (as_string (L(t)))));
    for (int i=0; i<N(choices); ++i)
      if (as_symbol (choices[i]) == as_string (L(t))) {
        editing_variant_set (t, as_tree_label (as_symbol (choices[(i + (forward ? 1 : N(choices)-1)) % N(choices)])));
        return;
      }
    return;
  }
  object parent= call ("tree-outer", object (t));
  if (is_tree (parent)) call ("variant-circulate", parent, object (forward));
}

bool math_script_context (tree t) {
  return is_func (t, LSUB) || is_func (t, LSUP) ||
         is_func (t, RSUB) || is_func (t, RSUP);
}

bool math_script_only (tree t) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  tree parent= subtree (root, reverse (ip->next));
  int index= ip->item;
  return is_func (parent, CONCAT) &&
    (index == 0 || !math_script_context (parent[index-1])) &&
    (index+1 == N(parent) || !math_script_context (parent[index+1]));
}

void math_sqrt_toggle (tree t) {
  if (!is_func (t, SQRT)) return;
  if (N(t) == 1) {
    t= tree_insert (t, 1, tree (TUPLE, ""));
    get_current_editor ()->go_to (reverse (obtain_ip (t)) * path (1, 0));
  }
  else if (N(t) == 2) tree_remove (t, 1, 1);
}

void math_correct_all () {
  editor ed= get_current_editor ();
  tree root= ed->the_root ();
  path p= ed->the_buffer_path ();
  if (ed->selection_active_any ()) {
    p= ed->selection_get_path ();
    if (ed->selection_get () != subtree (root, p)) {
      ed->set_message ("Only implemented for complete subtrees", "correct formula");
      return;
    }
  }
  tree source= subtree (root, p);
  tree corrected= manual_correct (source);
  if (corrected != source) tree_assign (source, corrected);
}

namespace {
bool bracket_wrapper (tree t) {
  return is_func (t, AROUND) || is_func (t, VAR_AROUND) ||
    is_func (t, BIG_AROUND) || is_compound (t, "temp-slot");
}

bool restore_bracket_cursor (editor ed, tree t, path p) {
  if (is_compound (t, "temp-slot")) {
    ed->go_to (p * path (0, 0));
    ed->remove_structure_upwards ();
    return true;
  }
  if (is_compound (t))
    for (int i=0; i<N(t); ++i)
      if (restore_bracket_cursor (ed, t[i], p * i)) return true;
  return false;
}
}

void math_brackets_refresh () {
  editor ed= get_current_editor ();
  ed->insert_tree (compound ("temp-slot", ""), path (0, 0));
  tree root= ed->the_root ();
  path p= path_up (ed->the_path ());
  const path buffer= ed->the_buffer_path ();
  while (p != buffer) {
    path parent= path_up (p);
    tree outer= subtree (root, parent);
    if (!bracket_wrapper (subtree (root, p)) &&
        !bracket_wrapper (outer) && !is_func (outer, CONCAT)) break;
    p= parent;
  }
  tree source= subtree (root, p);
  tree replacement= downgrade_big (
    upgrade_brackets (downgrade_brackets (source, true, false), "math"));
  tree_assign (source, replacement);
  restore_bracket_cursor (ed, subtree (root, p), p);
}

void math_open_latex_dialog () {
  const array<string> values= athena_native_latex_formula_dialog ();
  if (N(values) < 2) return;
  const string mode= values[0];
  string input= values[1];
  if (mode == "inline") input= "$" * input * "$";
  else if (mode == "display") input= "\\[" * input * "\\]";
  tree converted= latex_to_tree (parse_latex (input));
  editor ed= get_current_editor ();
  if (mode == "inline") {
    if (is_func (converted, DOCUMENT) && N(converted) > 0) converted= converted[0];
    if (is_func (converted, WITH) && N(converted) >= 3 &&
        converted[0] == "mode" && converted[1] == "math")
      converted= converted[N(converted)-1];
    ed->insert_tree (athena::document::normalize_imported_math (
      compound ("math", converted)));
  }
  else {
    converted= athena::document::normalize_imported_math (converted);
    if (is_func (converted, DOCUMENT))
      for (int i=0; i<N(converted); ++i) ed->insert_tree (converted[i]);
    else ed->insert_tree (converted);
  }
}
