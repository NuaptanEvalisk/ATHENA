/******************************************************************************
* MODULE     : reference_markup.cpp
* DESCRIPTION: Native typed and grouped reference markup
* COPYRIGHT  : (C) 2020 Joris van der Hoeven, 2026 The ATHENA developers
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "reference_markup.hpp"
#include "analyze.hpp"
#include "editor.hpp"
#include "native_text_edit.hpp"
#include "tree_traverse.hpp"
#include "scheme.hpp"
#include "vault.hpp"
#include "QTMVaultChooser.hpp"
#include <QUrl>

namespace {
path focused_transclusion (editor ed) {
  path p= ed->focus_get ();
  if (ed->test_subtree (p) && is_compound (ed->the_subtree (p), "transclude")) return p;
  for (p= path_up (ed->the_path ()); ed->the_buffer_path () <= p; p= path_up (p)) {
    if (is_compound (ed->the_subtree (p), "transclude")) return p;
    if (p == ed->the_buffer_path ()) break;
  }
  return path ();
}

void leave_transclusion (bool forward) {
  editor ed= get_current_editor ();
  path p= focused_transclusion (ed);
  if (is_nil (p)) return;
  ed->selection_cancel ();
  while (ed->the_buffer_path () < p) {
    const int sibling= last_item (p) + (forward ? 1 : -1);
    p= path_up (p);
    tree parent= ed->the_subtree (p);
    if (sibling >= 0 && sibling < N(parent)) {
      if (forward) ed->go_to_start (p * sibling);
      else ed->go_to_end (p * sibling);
      return;
    }
  }
}

void insert_vault_reference (bool transclude) {
  editor ed= get_current_editor ();
  if (!vault_active ()) {
    ed->set_message ("No active vault. Please load a vault first.", "Error");
    return;
  }
  vault_choose_link_native (transclude, [transclude] (tree result) {
    if (!is_tuple (result) || N(result) != 4) return;
    editor target= get_current_editor ();
    if (transclude) {
      if (!is_tuple (result[3]) || N(result[3]) == 0) {
        target->set_message ("Selected transclusion has no persistent source identities", "Insert transclusion");
        return;
      }
      target->insert_tree (compound ("transclude", result[3]));
    }
    else {
      string identity= as_string (result[3]);
      if (identity == "") {
        target->set_message ("Selected target has no persistent source identity", "Insert wikilink");
        return;
      }
      QByteArray encoded= QUrl::toPercentEncoding (QString::fromUtf8 (identity.data (), N(identity)));
      target->insert_tree (tree (HLINK, result[2], "tmfs://wikilink/" * string (encoded.constData ())));
    }
  });
}

tree reference_list (const array<tree>& items) {
  if (N(items) == 0) return tree ("");
  if (N(items) == 1) return items[0];
  tree result (CONCAT);
  for (int i=0; i<N(items); ++i) {
    if (i == N(items)-1) {
      if (N(items) == 2) result << tree (" ");
      result << compound ("localize", "and") << compound ("nbsp");
    }
    result << items[i];
    if (i < N(items)-2) result << tree (", ");
  }
  return result;
}

string reference_type (string name) {
  name= locase_all (name);
  int first= find_non_alpha (name, 0, true);
  int last= find_non_alpha (name, N(name), false);
  editor ed= get_current_editor ();
  if (first >= 0 && ed->defined_at_init (name(0, first) * "-ref"))
    return name(0, first);
  if (last >= 0 && ed->defined_at_init (name(last+1, N(name)) * "-ref"))
    return name(last+1, N(name));
  return "unknown";
}
}

void reference_insert_wikilink () { insert_vault_reference (false); }
void reference_insert_transclusion () { insert_vault_reference (true); }
void reference_before_transclusion () { leave_transclusion (false); }
void reference_after_transclusion () { leave_transclusion (true); }
void reference_select_transclusion () {
  editor ed= get_current_editor ();
  path p= focused_transclusion (ed);
  if (!is_nil (p)) ed->select (p);
}

tree reference_typed_markup (tree name, tree wrapper, tree arguments) {
  string title= is_atomic (name) ? name->label : string ("");
  string macro= is_atomic (wrapper) ? wrapper->label : string ("identity");
  array<tree> items;
  for (int i=0; i<N(arguments); ++i) {
    tree item (REFERENCE, arguments[i]);
    if (macro != "identity") item= compound (macro, item);
    items << item;
  }
  if (N(items) == 0) return tree ("");
  if (N(items) > 1 && title != "")
    title= ends (title, "y") ? title(0, N(title)-1) * "ies" : title * "s";
  tree body= reference_list (items);
  if (title == "") return body;
  tree result (CONCAT, compound ("localize", title), compound ("nbsp"));
  if (is_func (body, CONCAT)) result << A(body);
  else result << body;
  return result;
}

tree reference_typed_markup (tree name, tree arguments) {
  return reference_typed_markup (name, tree ("identity"), arguments);
}

tree reference_smart_markup (tree arguments) {
  array<string> types;
  array<tree> groups;
  for (int i=0; i<N(arguments); ++i) {
    string type= reference_type (as_string (arguments[i]));
    int group=0;
    while (group < N(types) && types[group] != type) ++group;
    if (group == N(types)) {
      types << type;
      groups << compound (type * "-ref");
    }
    groups[group] << arguments[i];
  }
  return reference_list (groups);
}

namespace {
tree clean_reference_preview (tree t) {
  if (text_node_in_group (t, "section-tag")) {
    tree result (as_tree_label (as_string (L(t)) * "*"));
    result << A(t);
    return result;
  }
  const string tag= as_string (L(t));
  if (tag == "label" || tag == "item" || tag == "item*" ||
      tag == "bibitem" || tag == "bibitem*" || tag == "eq-number")
    return tree ("");
  if (is_func (t, DOCUMENT) || is_func (t, CONCAT) || is_func (t, TFORMAT) ||
      is_func (t, TABLE) || is_func (t, ROW) || is_func (t, CELL)) {
    tree result (L(t));
    for (int i=0; i<N(t); ++i) result << clean_reference_preview (t[i]);
    return is_func (t, CONCAT) ? simplify_concat (result) : result;
  }
  return t;
}

bool expand_reference_preview (tree t) {
  string tag= as_string (L(t));
  if (ends (tag, "*")) tag= tag(0, N(tag)-1);
  return tag == "theorem" || tag == "proposition" || tag == "lemma" ||
         tag == "corollary" || tag == "conjecture" || tag == "definition" ||
         tag == "axiom";
}
}

object reference_preview (string id) {
  editor ed= get_current_editor ();
  path label= ed->search_label (id);
  if (is_nil (label)) return object (false);
  label= path_up (label);
  tree target= ed->the_subtree (label);
  const string tag= as_string (L(target));
  if (!text_node_in_group (target, "label-tag") &&
      tag != "glossary" && tag != "glossary-explain" && tag != "glossary-dup" &&
      tag != "index" && tag != "subindex" && tag != "subsubindex" &&
      tag != "index-complex") return object (false);
  const path root= ed->the_buffer_path ();
  path context;
  bool found=false, math=false;
  for (path p=label; N(p)>=N(root); p=path_up (p)) {
    tree t= ed->the_subtree (p);
    math= math || is_compound (t, "math-block") || is_compound (t, "math-grid");
    if (!found && (is_func (t, ROW) ||
        (N(p)>N(root) && is_func (ed->the_subtree (path_up (p)), DOCUMENT)))) {
      context=p;
      found=true;
    }
    if (p == root) break;
  }
  if (!found) return object (false);
  tree doc= ed->the_subtree (context);
  if (N(context)>=N(root)+2 &&
      is_func (ed->the_subtree (path_up (context)), DOCUMENT)) {
    tree parent= ed->the_subtree (path_up (context));
    tree enclosing= ed->the_subtree (path_up (context, 2));
    if (expand_reference_preview (enclosing)) doc=parent;
    else if (text_node_in_group (enclosing, "algorithm-tag") &&
             last_item (path_up (context)) < N(enclosing)-1)
      doc=tree (WITH, "par-first", "0em", parent);
  }
  if (is_func (doc, ROW)) {
    tree row (CONCAT);
    for (int i=0; i<N(doc); ++i)
      row << (is_func (doc[i], CELL, 1) ? doc[i][0] : doc[i]);
    doc=simplify_concat (row);
  }
  doc=clean_reference_preview (doc);
  if (math) doc=tree (WITH, "math-display", "true", compound ("math", doc));
  return tree_to_stree (compound ("preview-balloon", doc));
}

void reference_show_preview (tree body, tree label) {
  const path ip= obtain_ip (body);
  if (!ip_attached (ip) || is_nil (ip) || !is_atomic (label)) return;
  tree ref= get_current_editor ()->the_subtree (reverse (ip->next));
  object tip= reference_preview (label->label);
  if (is_bool (tip) && !as_bool (tip)) return;
  array<object> args;
  args << object (label->label) << object (ref) << tip << object ("auto")
       << object ("auto") << object ("default") << object (0.6);
  call ("show-tooltip", args);
}
