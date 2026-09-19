/******************************************************************************
* MODULE     : format_commands.cpp
* DESCRIPTION: Observer-preserving formatting wrappers and editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "format_commands.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "native_interfaces.hpp"
#include "tree_analyze.hpp"
#include "tree_cursor.hpp"
#include "tree_modify.hpp"
#include "file.hpp"
#include "buffer_actor.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>
#include <array>

namespace {

QString json_key (string value) {
  c_string bytes (value);
  return QString::fromUtf8 (bytes, N (value));
}

string native_string (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

void require_strings (QJsonValue value) {
  ASSERT (value.isArray (), "format parameter list must be an array");
  for (QJsonValue item: value.toArray ())
    ASSERT (item.isString (), "format parameter list must contain strings");
}

struct format_parameter_data {
  QJsonObject definitions;
  QJsonObject customizable;
  QJsonObject effect_families;
  QJsonObject effect_pens;

  format_parameter_data () {
    string source;
    bool failed= load_string (url ("$ATHENA_PATH/misc/input/format-parameters.json"),
                              source, false);
    ASSERT (!failed, "cannot read format-parameters.json");
    c_string bytes (source);
    QJsonParseError error;
    QJsonDocument document= QJsonDocument::fromJson (QByteArray (bytes, N (source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid format-parameters.json");
    definitions= document.object ();
    ASSERT (definitions.value ("version").toInt () == 1,
            "unsupported format parameter schema");
    require_strings (definitions.value ("focus_preferences"));
    ASSERT (definitions.value ("standard").isObject () &&
            definitions.value ("customizable").isArray () &&
            definitions.value ("choices").isArray () &&
            definitions.value ("pen_effects").isObject (),
            "missing format parameter definitions");
    QJsonObject standard= definitions.value ("standard").toObject ();
    for (auto i= standard.begin (); i != standard.end (); ++i) require_strings (i.value ());
    for (QJsonValue item: definitions.value ("customizable").toArray ()) {
      ASSERT (item.isObject (), "invalid customizable parameter group");
      QJsonObject group= item.toObject ();
      require_strings (group.value ("tags"));
      ASSERT (group.value ("parameters").isArray (), "missing customizable parameters");
      for (QJsonValue parameter: group.value ("parameters").toArray ()) {
        require_strings (parameter);
        ASSERT (parameter.toArray ().size () == 2, "parameter requires name and label");
      }
      for (QJsonValue tag: group.value ("tags").toArray ()) {
        ASSERT (!customizable.contains (tag.toString ()), "duplicate customizable tag");
        customizable.insert (tag.toString (), group.value ("parameters"));
      }
    }
    for (QJsonValue item: definitions.value ("choices").toArray ()) {
      ASSERT (item.isObject (), "invalid parameter choice rule");
      QJsonObject rule= item.toObject ();
      ASSERT (rule.contains ("names") || rule.contains ("suffixes"), "choice rule needs a match");
      if (rule.contains ("names")) require_strings (rule.value ("names"));
      if (rule.contains ("suffixes")) require_strings (rule.value ("suffixes"));
      require_strings (rule.value ("values"));
      ASSERT (!rule.contains ("other") || rule.value ("other").isBool (), "invalid other choice");
    }
    require_strings (definitions.value ("pens"));
    QJsonArray pens= definitions.value ("pens").toArray ();
    QJsonObject families= definitions.value ("pen_effects").toObject ();
    for (auto i= families.begin (); i != families.end (); ++i) {
      ASSERT (i.value ().isString () && pens.contains (i.value ()), "unknown default effect pen");
      effect_families.insert (i.key (), i.key ());
      effect_pens.insert (i.key (), i.value ());
      for (QJsonValue pen: pens) {
        QString tag= pen.toString () + "-" + i.key ();
        effect_families.insert (tag, i.key ());
        effect_pens.insert (tag, pen);
      }
    }
  }
};

const format_parameter_data& parameter_data () {
  // Publish immutable Qt values, never actor-owned trees or rooted Scheme lists.
  static const format_parameter_data data;
  return data;
}

object json_strings (const QJsonArray& values, bool other= false) {
  object result= other ? list_object (keyword_object ("other")) : null_object ();
  for (int i= values.size () - 1; i >= 0; --i)
    result= cons (object (native_string (values[i].toString ())), result);
  return result;
}

bool parent_tree (tree t, tree& parent) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  parent= subtree (root, reverse (ip->next));
  return true;
}

int property_index (tree t, tree variable, bool last= false) {
  int found= -1;
  if (is_func (t, WITH))
    for (int i= 0; i + 2 < N (t); i+= 2)
      if (t[i] == variable) {
        found= i;
        if (!last) break;
      }
  return found;
}

struct tracked_tree {
  observer pointer;
  explicit tracked_tree (tree t): pointer (tree_pointer_new (t)) {}
  ~tracked_tree () { tree_pointer_delete (pointer); }
  tree get () { return obtain_tree (pointer); }
};

void simplify_outer (tree t, tree variable) {
  if (admits_edit_observer (t)) return;
  tree parent;
  if (is_func (t, DOCUMENT, 1)) {
    if (parent_tree (t, parent)) simplify_outer (parent, variable);
  }
  else if (is_func (t, WITH) && N (t) > 0) {
    for (int i= (N (t) - 3) / 2 * 2; i >= 0 && i + 2 < N (t); i-= 2)
      if (t[i] == variable) t= tree_remove (t, i, 2);
    tree body= t[N (t) - 1];
    if (is_func (body, DOCUMENT, 1) && is_func (body[0], WITH))
      tree_remove_node (body, 0);
    if (N (t) == 1) t= tree_remove_node (t, 0);
    if (parent_tree (t, parent)) simplify_outer (parent, variable);
  }
}

bool enclosing_property_scope (tree t, tree& parent) {
  tree grandparent;
  return parent_tree (t, parent) && parent_tree (parent, grandparent);
}

bool with_like_search (tree t, tree& result) {
  while (true) {
    if (is_with_like (t)) { result= t; return true; }
    if (!is_atomic (t) && !is_func (t, CONCAT) && !is_func (t, DOCUMENT))
      return false;
    if (admits_edit_observer (t) || !parent_tree (t, t)) return false;
  }
}

tree cursor_tree (bool shifted= false) {
  editor ed= get_current_editor ();
  path p= shifted ? ed->the_shifted_path () : ed->the_path ();
  tree root= ed->the_root ();
  return subtree (root, path_up (p));
}

void go_body (tree t, bool at_end) {
  path p= reverse (obtain_ip (t)) * (N (t) - 1);
  tree body= t[N (t) - 1];
  get_current_editor ()->go_to (p * (at_end ? end (body) : start (body)));
}

bool at_body_border (tree t, bool at_end) {
  path p= reverse (obtain_ip (t));
  path cursor= get_current_editor ()->the_path ();
  if (cursor == p * (at_end ? end (t) : start (t))) return true;
  int i= at_end ? 1 : 0;
  return is_compound (t) && N (t) > i &&
    cursor == p * i * (at_end ? end (t[i]) : start (t[i]));
}

tree property_wrapper (object properties) {
  tree wrapper (WITH);
  ASSERT (is_list (properties), "format properties must be a list");
  for (object p= properties; !is_null (p); p= cdr (p))
    wrapper << content_to_tree (car (p));
  ASSERT (N (wrapper) % 2 == 0, "format properties must be variable/value pairs");
  return wrapper;
}

bool innermost_with (tree& t) {
  t= cursor_tree ();
  while (true) {
    if (is_func (t, WITH)) return true;
    if (admits_edit_observer (t) || !parent_tree (t, t)) return false;
  }
}

void select_tree (tree t) {
  path p= reverse (obtain_ip (t));
  get_current_editor ()->selection_set_paths (p * 0, p * right_index (t));
}

tree add_with (tree properties, tree body, path& destination) {
  if (is_func (body, WITH) && N (body) > 0) {
    int last= N (body) - 1;
    tree wrapper (WITH, N (body));
    for (int i= 0; i < last; ++i) wrapper[i]= body[i];
    wrapper[last]= add_with (properties, body[last], destination);
    destination= path (last) * destination;
    return wrapper;
  }
  destination= path (N (properties)) * end (body);
  properties << body;
  return properties;
}

void restore_cells (const std::array<int, 4>& cells) {
  editor ed= get_current_editor ();
  path first= ed->table_search_cell (cells[0], cells[2]);
  path last= ed->table_search_cell (cells[1], cells[3]);
  if (!is_nil (first) && !is_nil (last))
    ed->selection_set_paths (path_up (first) * 0, path_up (last) * 1);
}

void restore_cells_after_command (array<int> cells) {
  if (N (cells) != 4 || (cells[0] == cells[1] && cells[2] == cells[3])) return;
  const std::array<int, 4> selected {cells[0], cells[1], cells[2], cells[3]};
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr || context->actor_id == ATHENA_NO_ACTOR ||
      QCoreApplication::instance () == nullptr) {
    restore_cells (selected);
    return;
  }
  athena_actor_id actor= context->actor_id;
  athena_view_id view= context->view_id;
  // Match keep-table-selection's post-command restoration, without moving
  // Scheme closures or document objects through the GUI timer queue.
  QTimer::singleShot (10, QCoreApplication::instance (), [actor, view, selected] {
    auto id= actor_continuation_registry::instance ().store ([selected] { restore_cells (selected); });
    if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
          view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, id))
      actor_continuation_registry::instance ().discard (id);
  });
}

} // namespace

object format_with_ref (tree t, tree variable) {
  int i= property_index (t, variable);
  return i < 0 ? object (false) : object (t[i + 1]);
}

tree format_with_set (tree t, tree variable, tree value) {
  int i= property_index (t, variable);
  if (i >= 0) {
    // The shared diff helper preserves cursors inside an edited property value.
    if (tree_active (t[i + 1]))
      call ("tree-set-diff", object (t[i + 1]), object (value));
    else tree_set (t, i + 1, copy (value));
  }
  else if (is_func (t, WITH) && N (t) > 0)
    t= tree_insert (t, N (t) - 1, tree (TUPLE, variable, value));
  else t= tree_insert_node (t, 2, tree (WITH, variable, value));
  return t;
}

void format_with_simplify (tree t) {
  if (admits_edit_observer (t) || !tree_active (t)) return;
  tree parent;
  if (!parent_tree (t, parent)) return;
  tracked_tree current (t);
  format_with_simplify (parent);
  t= current.get ();
  if (!is_func (t, WITH)) return;
  for (int i= 0; i + 2 < N (t); i+= 2)
    if (parent_tree (t, parent)) simplify_outer (parent, t[i]);
}

void format_with_merge (tree t) {
  tree parent;
  if (!is_func (t, WITH) || N (t) == 0 ||
      !parent_tree (t, parent) || !is_func (parent, WITH)) return;
  tree properties (TUPLE, N (t) - 1);
  for (int i= 0; i < N (properties); ++i) properties[i]= copy (t[i]);
  tree_remove_node (t, N (t) - 1);
  tree_insert (parent, N (parent) - 1, properties);
}

bool format_test_env (string variable, string value) {
  return get_current_editor ()->get_env_string (variable) == value;
}

void format_tree_with_set (tree t, object properties) {
  tree wrapper= property_wrapper (properties);
  call ("focus-tree-modified", object (t));
  t= tree_insert_node (t, N (wrapper), wrapper);
  tracked_tree current (t);
  format_with_simplify (t);
  format_with_merge (current.get ());
}

object format_tree_with_get (object value, tree variable) {
  if (!is_tree (value)) return object (false);
  tree t= as_tree (value);
  do {
    int i= property_index (t, variable, true);
    if (i >= 0) return object (t[i + 1]);
  } while (enclosing_property_scope (t, t));
  return object (false);
}

void format_tree_with_reset (object value, tree variable) {
  if (!is_tree (value)) return;
  tree t= as_tree (value);
  if (!tree_active (t)) return;
  call ("focus-tree-modified", value);
  do {
    int i= property_index (t, variable, true);
    if (i >= 0) {
      t= tree_remove (t, i, 2);
      if (N (t) == 1) tree_remove_node (t, 0);
      return;
    }
  } while (enclosing_property_scope (t, t));
}

bool format_with_like_check_insert (tree t) {
  tree u= cursor_tree ();
  if (is_with_like (u) && with_same_type (t, u)) {
    go_body (u, last_item (get_current_editor ()->the_path ()) != 0);
    return true;
  }
  u= cursor_tree (true);
  if (is_with_like (u) && with_same_type (t, u)) {
    go_body (u, false);
    return true;
  }
  if (with_like_search (cursor_tree (), u) && with_same_type (t, u)) {
    string tag= as_string (L (t));
    get_current_editor ()->set_message (
      tree (CONCAT, "Warning: already inside '", tag, "'"),
      tree (CONCAT, "make '", tag, "'"));
    return true;
  }
  return false;
}

void format_make_with_like (tree t) {
  editor ed= get_current_editor ();
  if (is_func (t, WITH, 3)) {
    if (ed->selection_active_table ()) ed->cell_set_format (as_string (t[0]), t[1]);
    else if (is_atomic (t[1])) ed->make_with (as_string (t[0]), t[1]->label);
    else ed->var_insert_tree (t, path (2, 0));
  }
  else if (is_compound (t) && N (t) == 1)
    // The public constructor includes mode-dependent inline/block wrapping.
    call ("make", symbol_object (as_string (L (t))));
  else if (is_compound (t) && N (t) > 0) {
    t= copy (t);
    path destination (N (t) - 1, 0);
    if (ed->selection_active_any ()) {
      tree selected= copy (ed->selection_get ());
      destination= path (N (t) - 1) * end (selected);
      ed->selection_cut ("nowhere");
      t[N (t) - 1]= selected;
    }
    ed->var_insert_tree (t, destination);
  }
}

void format_toggle_with_like (tree wrapper, object back) {
  editor ed= get_current_editor ();
  tree t, root= ed->the_root ();
  bool found= false;
  if (ed->selection_active_any ()) {
    t= subtree (root, ed->selection_get_path ());
    found= ed->selection_get () == t;
  }
  if (!found) {
    tree parent;
    found= parent_tree (cursor_tree (), parent) && with_like_search (parent, t);
  }
  if (!found || !is_with_like (t) || !with_same_type (t, wrapper))
    format_make_with_like (wrapper);
  else if ((is_bool (back) && !as_bool (back)) || is_empty (t[N (t) - 1])) {
    t= tree_remove_node (t, N (t) - 1);
    tree parent;
    if (parent_tree (t, parent)) correct_node (parent);
  }
  else if (at_body_border (t[N (t) - 1], false))
    ed->go_to (reverse (obtain_ip (t)) * 0);
  else if (at_body_border (t[N (t) - 1], true))
    ed->go_to (reverse (obtain_ip (t)) * 1);
  else format_make_with_like (content_to_tree (back));
}

void format_toggle_bold () {
  format_toggle_with_like (tree (WITH, "font-series", "bold", ""),
    object (tree (WITH, "font-series", "medium", "")));
}

void format_toggle_italic () {
  format_toggle_with_like (tree (WITH, "font-shape", "italic", ""),
    object (tree (WITH, "font-shape", "right", "")));
}

void format_toggle_small_caps () {
  format_toggle_with_like (tree (WITH, "font-shape", "small-caps", ""),
    object (tree (WITH, "font-shape", "right", "")));
}

void format_toggle_underlined () {
  format_toggle_with_like (tree (as_tree_label ("underline"), ""), object (false));
}

bool format_focus_has_preferences (tree t) {
  return is_extension (t) || parameter_data ().definitions.value ("focus_preferences")
    .toArray ().contains (json_key (as_string (L (t))));
}

object format_standard_parameters (string tag) {
  QJsonValue values= parameter_data ().definitions.value ("standard").toObject ().value (json_key (tag));
  return values.isArray () ? json_strings (values.toArray ()) : object (false);
}

object format_parameter_choices (string variable) {
  QString key= json_key (variable);
  for (QJsonValue item: parameter_data ().definitions.value ("choices").toArray ()) {
    QJsonObject rule= item.toObject ();
    bool matches= rule.value ("names").toArray ().contains (key);
    for (QJsonValue suffix: rule.value ("suffixes").toArray ())
      matches= matches || key.endsWith (suffix.toString ());
    if (matches) return json_strings (rule.value ("values").toArray (), rule.value ("other").toBool ());
  }
  return object (false);
}

object format_customizable_parameters (tree t) {
  QJsonArray values= parameter_data ().customizable.value (json_key (as_string (L (t)))).toArray ();
  object result= null_object ();
  for (int i= values.size () - 1; i >= 0; --i)
    result= cons (json_strings (values[i].toArray ()), result);
  return result;
}

object format_customizable_parameters_memo (tree t) {
  // Native definitions are already indexed. Resolve extensions without caching
  // mutable Scheme objects across actors or hiding subsequently loaded rules.
  return call ("customizable-parameters", object (t));
}

bool format_customizable_context (tree t) {
  return !is_null (format_customizable_parameters_memo (t));
}

bool format_pen_effect_context (tree t) {
  return parameter_data ().effect_families.contains (json_key (as_string (L (t))));
}

static bool effect_tree (object value, tree& t) {
  if (is_tree (value)) { t= as_tree (value); return true; }
  if (!parent_tree (cursor_tree (), t)) return false;
  while (true) {
    if (format_pen_effect_context (t)) return true;
    if (admits_edit_observer (t) || !parent_tree (t, t)) return false;
  }
}

object format_get_effect_pen (object value) {
  tree t;
  if (!effect_tree (value, t)) return object (false);
  QJsonValue pen= parameter_data ().effect_pens.value (json_key (as_string (L (t))));
  return pen.isString () ? object (native_string (pen.toString ())) : object (false);
}

void format_set_effect_pen (object value, string pen) {
  tree t;
  if (!effect_tree (value, t)) return;
  const format_parameter_data& data= parameter_data ();
  QJsonValue family= data.effect_families.value (json_key (as_string (L (t))));
  if (!family.isString () || !data.definitions.value ("pens").toArray ().contains (json_key (pen))) return;
  call ("variant-set", object (t), symbol_object (pen * "-" * native_string (family.toString ())));
}

bool format_test_effect_pen (object value, string pen) {
  return format_get_effect_pen (value) == object (pen);
}

void format_make_multi_with (object properties) {
  tree wrapper= property_wrapper (properties);
  if (N (wrapper) == 0) return;
  editor ed= get_current_editor ();
  if (ed->selection_active_table ()) {
    array<int> cells= ed->table_which_cells ();
    bool multiple= N (cells) == 4 &&
      (cells[0] != cells[1] || cells[2] != cells[3]);
    std::array<int, 4> selected {};
    if (multiple)
      for (int i= 0; i < 4; ++i) selected[i]= cells[i];
    for (int i= 0; i < N (wrapper); i+= 2) {
      // cell_set_format corrects table structure and collapses a multi-cell
      // selection.  Re-establish the original range before every later
      // property so one make-multi-with applies all pairs to the same cells.
      if (i > 0 && multiple) restore_cells (selected);
      ed->cell_set_format (as_string (wrapper[i]), wrapper[i + 1]);
    }
    restore_cells_after_command (cells);
  }
  else if (ed->selection_active_any ()) {
    tree selected= ed->selection_get ();
    path destination;
    wrapper= add_with (wrapper, selected, destination);
    ed->selection_cut ("null");
    ed->var_insert_tree (wrapper, destination);
    format_with_simplify (cursor_tree ());
    tree t;
    if (innermost_with (t)) select_tree (t);
  }
  else {
    path destination (N (wrapper), 0);
    wrapper << tree ("");
    ed->var_insert_tree (wrapper, destination);
    format_with_simplify (cursor_tree ());
  }
}

void format_make_line_with (string variable, tree value) {
  editor ed= get_current_editor ();
  if (!ed->selection_active_normal ()) ed->select_line ();
  format_make_with_like (tree (WITH, variable, value, ""));
  call ("insert-return");
  ed->remove_text (false);
}

void format_make_multi_line_with (object properties) {
  if (is_null (properties)) return;
  editor ed= get_current_editor ();
  if (ed->selection_active_table ()) { format_make_multi_with (properties); return; }
  if (!ed->selection_active_normal ()) ed->select_line ();
  format_make_multi_with (properties);
  call ("insert-return");
  ed->remove_text (false);
  tree t;
  if (innermost_with (t)) {
    format_with_simplify (t);
    if (innermost_with (t)) {
      format_with_merge (t);
      if (innermost_with (t)) select_tree (t);
    }
  }
}

static void page_break (const char* tag) {
  call ("make", symbol_object (tag));
  call ("insert-return");
}

void format_make_page_break () { page_break ("page-break"); }
void format_make_new_page () { page_break ("new-page"); }
void format_make_new_dpage () { page_break ("new-dpage"); }
