// Copyright (C) 2020 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "reference_markup.hpp"
#include "editor.hpp"
#include "native_text_edit.hpp"
#include "generic_editor_commands.hpp"
#include "native_interfaces.hpp"
#include "Data/NodeContracts/node_groups.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "analyze.hpp"
#include "dictionary.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "tree_traverse.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <functional>
#include <vector>

namespace {
std::string bytes (string s) { return std::string (s.data (), N(s)); }
string text (const std::string& s) { return string (s.data (), s.size ()); }
const nlohmann::json& reference_types () {
  static const auto data= [] {
    string source;
    ASSERT (!load_string (url ("$ATHENA_PATH/misc/input/reference-types.json"), source, false),
            "cannot read reference-types.json");
    auto result= nlohmann::json::parse (bytes (source));
    ASSERT (result.at ("version") == 1, "unsupported reference type schema");
    return result;
  } ();
  return data;
}
bool tie_context (tree t) {
  return text_node_in_group (t, "label-tag") ||
         text_node_in_group (t, "reference-tag") ||
         text_node_in_group (t, "citation-tag");
}
bool child_named (tree t, tree name) {
  for (int i=0; i<N(t); ++i) if (t[i] == name) return true;
  return false;
}
void visit (tree t, path p, const std::function<void(tree, path)>& action) {
  action (t, p);
  for (int i=0; i<N(t); ++i) visit (t[i], p * i, action);
}
bool parent (tree t, tree& out) {
  const path ip= obtain_ip (t);
  if (!ip_attached (ip) || admits_edit_observer (t) || is_nil (ip)) return false;
  out= get_current_editor ()->the_subtree (reverse (ip->next));
  return true;
}
string plural (string s) {
  if (s == "") return s;
  return ends (s, "y") ? s(0, N(s)-1) * "ies" : s * "s";
}
string previous_word (tree t) {
  tree p;
  while (parent (t, p)) {
    if (is_func (p, CONCAT)) {
      for (int i=obtain_ip (t)->item-1; i>=0; --i) if (is_atomic (p[i])) {
        string s=trim_spaces_right (p[i]->label);
        int end=N(s);
        while (end>0 && s[end-1]!=' ') --end;
        return s(end, N(s));
      }
      return "";
    }
    if (!is_atomic (t) && !text_node_in_group (t, "reference-tag") &&
        !is_func (t, INACTIVE)) break;
    t=p;
  }
  return "";
}
string word_type (string word) {
  if (word == "") return "";
  word=locase_all (word);
  string language=get_current_editor ()->get_init_string ("language");
  std::vector<std::string> types;
  const auto& groups=athena::contracts::standard_node_groups ();
  for (const char* group: {"enunciation-tag", "section-tag", "equation-tag", "algorithm-tag"})
    for (const auto& type: groups.members (group)) types.push_back (type);
  for (const auto& entry: reference_types ().at ("aliases").items ())
    types.push_back (entry.key ());
  for (const auto& type: types) {
    string t=text (type), p=plural (t);
    if (word == t || word == p || word == translate (t, "english", language) ||
        word == translate (p, "english", language)) return t;
  }
  return word == "(" ? string ("equation") : word;
}
bool matches_type (tree t, const std::vector<std::string>& types) {
  std::string tag=bytes (as_string (L(t)));
  const std::string kind=athena::enunciation::standard_registry ().kind_name (t);
  if (!kind.empty ()) tag=kind;
  for (const auto& type: types) if (tag == type) return true;
  if (is_func (t, CONCAT) &&
      (std::find (types.begin (), types.end (), "subsubsection")!=types.end () ||
       std::find (types.begin (), types.end (), "subparagraph")!=types.end ()))
    for (int i=0; i<N(t); ++i) if (matches_type (t[i], types)) return true;
  return false;
}
}

object reference_infer_label (tree t) {
  if (!is_atomic (t)) return object (false);
  string value=t->label, type;
  int colon=search_forwards (":", 0, value);
  if (colon>=0) {
    type=value(0, colon);
    for (const auto& entry: reference_types ().at ("aliases").items ())
      for (const auto& alias: entry.value ())
        if (alias == bytes (type)) { type=text (entry.key ()); break; }
    value=value(colon+1, N(value));
  }
  else type=word_type (previous_word (t));
  editor ed=get_current_editor ();
  array<string> labels;
  const array<string> references=ed->find_refs (value);
  for (int i=0; i<N(references); ++i)
    if (!starts (references[i], "auto-") && !starts (references[i], "bib-"))
      labels << references[i];
  if (N(labels)==0) return object (false);
  if (N(labels)==1) return object (labels[0]);
  const auto& all_types=reference_types ().at ("types");
  std::vector<std::string> types=all_types.contains (bytes (type)) ?
    all_types.at (bytes (type)).get<std::vector<std::string>>() :
    std::vector<std::string> {bytes (type)};
  string result;
  visit (ed->the_buffer (), ed->the_buffer_path (), [&] (tree label, path) {
    if (result!="" || !text_node_in_group (label, "label-tag")) return;
    bool candidate=false;
    for (int i=0; i<N(labels); ++i)
      candidate=candidate || child_named (label, tree (labels[i]));
    if (!candidate) return;
    tree ancestor=label;
    do {
      if (matches_type (ancestor, types)) {
        if (N(label)>0 && is_atomic (label[0])) result=label[0]->label;
        break;
      }
    } while (parent (ancestor, ancestor));
  });
  return object (result=="" ? labels[0] : result);
}

bool reference_inactive_at_cursor () {
  editor ed=get_current_editor ();
  path p=path_up (ed->the_path ());
  return N(p)>=N(ed->the_buffer_path ())+2 && is_atomic (ed->the_subtree (p)) &&
    text_node_in_group (ed->the_subtree (path_up (p)), "reference-tag") &&
    is_func (ed->the_subtree (path_up (p, 2)), INACTIVE) &&
    ed->defined_at_init ("preview-ref-package");
}

void reference_resolve_at_cursor () {
  if (!reference_inactive_at_cursor ()) return;
  editor ed=get_current_editor ();
  tree ref=ed->the_subtree (path_up (ed->the_path (), 2));
  for (int i=0; i<N(ref); ++i) if (is_atomic (ref[i])) {
    bool exists=false;
    visit (ed->the_buffer (), ed->the_buffer_path (), [&] (tree node, path) {
      exists=exists || (text_node_in_group (node, "label-tag") && child_named (node, ref[i]));
    });
    if (!exists) {
      object label=reference_infer_label (ref[i]);
      if (is_string (label)) tree_assign (ref[i], tree (as_string (label)));
    }
  }
}

bool reference_navigate (tree t, bool forwards, bool extremal) {
  editor ed=get_current_editor ();
  if (!tie_context (t)) {
    const auto label=generic_focus_label (t);
    if (!label) return false;
    t=*label;
    ed->go_to_end (reverse (obtain_ip (t)));
  }
  if (N(t)==0) return true;
  tree id=ed->reference_navigation_id;
  if (!child_named (t, id)) {
    if (!is_atomic (t[0])) return true;
    string key=t[0]->label;
    id=starts (key, "bib-") ? tree (key(4, N(key))) : t[0];
  }
  ed->reference_navigation_id=id;
  string key=as_string (id);
  if (starts (key, "bib-")) key=key(4, N(key));
  path cursor=path_up (ed->the_path ()), selected;
  bool found=false;
  visit (ed->the_buffer (), ed->the_buffer_path (), [&] (tree node, path p) {
    if (!tie_context (node) ||
        (!child_named (node, tree (key)) && !child_named (node, tree ("bib-" * key)))) return;
    if (extremal ? (forwards || !found) :
        (forwards ? (!found && path_inf (cursor, p)) : path_inf (p, cursor))) {
      selected=p;
      found=true;
    }
  });
  if (found) ed->go_to_end (selected);
  return true;
}
