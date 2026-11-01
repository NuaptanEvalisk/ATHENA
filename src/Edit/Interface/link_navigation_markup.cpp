// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link_markup.hpp"
#include "link.hpp"
#include "hashmap.hpp"
#include "server.hpp"
#include "QTMNativeDialogs.hpp"
#include "boot.hpp"
#include "editor.hpp"
#include "command.hpp"
#include "analyze.hpp"
#include "generic_editor_commands.hpp"
#include "Data/Convert/Generic/conversion_executor.hpp"
#include <QUrlQuery>
#include "buffer_actor.hpp"
#include "scheme_execution_context.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "qt_utilities.hpp"
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
struct navigation_item {
  string type;
  tree attributes;
  int position;
  string source;
  tree target;
};
using navigation_items= std::vector<navigation_item>;

tree read_attributes (object attributes) {
  tree result (ATTR);
  for (; !is_null (attributes); attributes= cdr (attributes)) {
    const object pair= car (attributes);
    result << content_to_tree (car (pair)) << content_to_tree (cdr (pair));
  }
  return result;
}

object write_attributes (tree attributes) {
  object result= null_object ();
  for (int i= N(attributes)-2; i >= 0; i-= 2)
    result= cons (cons (tree_to_stree (attributes[i]), tree_to_stree (attributes[i+1])), result);
  return result;
}

object write_target (tree target) {
  // A script's extra arguments are live trees, not serialized source forms.
  return native_link_target (native_link_flatten (tree (LINK, "", tree (ATTR), "", target)));
}

navigation_items read_items (object items) {
  navigation_items result;
  for (; !is_null (items); items= cdr (items)) {
    const array<object> item= as_array_object (car (items));
    if (N(item) != 5) throw std::invalid_argument ("A navigation item must have five fields");
    result.push_back ({as_string (item[0]), read_attributes (item[1]), as_int (item[2]),
                       as_string (item[3]), content_to_tree (item[4])});
  }
  return result;
}

object write_item (const navigation_item& item) {
  array<object> fields;
  fields << object (item.type) << write_attributes (item.attributes)
         << object (item.position) << object (item.source) << write_target (item.target);
  return as_list_object (fields);
}

object write_items (const navigation_items& items) {
  array<object> result;
  for (const auto& item: items) result << write_item (item);
  return as_list_object (result);
}

navigation_items simplify (const navigation_items& items) {
  hashmap<tree, int> positions (-1);
  navigation_items result;
  for (const auto& item: items) {
    const tree key (TUPLE, item.target, item.type, item.attributes, as_string (item.position));
    const int found= positions[key];
    if (found < 0) {
      positions (key)= int (result.size ());
      result.push_back (item);
    }
    else result[found]= item;
  }
  return result;
}

bool jumpable (tree target) {
  return is_func (target, ID, 1) || is_compound (target, "url", 1) ||
    (is_compound (target, "script") && N(target) > 0);
}

navigation_items filter (const navigation_items& items, std::optional<string> type,
                         std::optional<int> position, bool only_jumpable) {
  navigation_items result;
  for (const auto& item: items)
    if ((!type || item.type == *type) && (!position || item.position == *position) &&
        (!only_jumpable || jumpable (item.target))) result.push_back (item);
  return result;
}

list<string> types (const navigation_items& items, string suffix= "") {
  hashmap<string, bool> seen (false);
  list<string> result;
  for (const auto& item: items) {
    const string name= item.type * suffix;
    if (!seen[name]) {
      result= list<string> (name, result);
      seen (name)= true;
    }
  }
  return reverse (result);
}

object automatic_link (tree target, object broken) {
  object text= object (false);
  if (is_func (target, ID, 1)) {
    const list<tree> nodes= get_trees (target[0]->label);
    if (!is_nil (nodes)) text= tree_to_stree (nodes->item);
  }
  else if (is_compound (target, "url", 1) ||
           (is_compound (target, "script") && N(target) > 0))
    text= tree_to_stree (compound ("verbatim", target[0]));
  if (is_bool (text) && !as_bool (text))
    return tree_to_stree (tree (WITH, "color", "red", content_to_tree (broken)));
  const tree id (ID, native_link_unique_id ());
  const object relation= cons (symbol_object ("link"),
    cons (object ("automatic"), list_object (tree_to_stree (id), write_target (target))));
  return cons (symbol_object ("locus"), list_object (tree_to_stree (id), relation, text));
}

object enumeration (object items) {
  if (is_null (items) || is_null (cdr (items))) return items;
  array<object> rows;
  for (; !is_null (items); items= cdr (items))
    rows << cons (symbol_object ("surround"),
                  list_object (list_object (symbol_object ("item")), object (""), car (items)));
  const object body= cons (symbol_object ("document"), as_list_object (rows));
  return list_object (list_object (symbol_object ("enumerate"), body));
}

void append_sections (array<object>& body, const navigation_items& items, string prefix) {
  for (list<string> names= types (items); !is_nil (names); names= names->next) {
    body << tree_to_stree (compound ("strong", prefix * names->item));
    array<object> contents;
    for (const auto& item: items)
      if (item.type == names->item) contents << automatic_link (item.target, object ("Broken"));
    for (object rows= enumeration (as_list_object (contents)); !is_null (rows); rows= cdr (rows))
      body << car (rows);
  }
}
}

object native_navigation_simplify (object items) {
  return write_items (simplify (read_items (items)));
}

object native_link_list_to_navigation (object links) {
  navigation_items result;
  for (; !is_null (links); links= cdr (links)) {
    const object link= car (links);
    const string source= as_string (car (link));
    const string type= as_string (cadr (link));
    const tree attributes= read_attributes (caddr (link));
    int position= 0;
    for (object vertices= cdr (cddr (link)); !is_null (vertices); vertices= cdr (vertices), ++position) {
      const tree vertex= content_to_tree (car (vertices));
      if (!is_func (vertex, ID, 1) || vertex[0] != source)
        result.push_back ({type, attributes, position, source, vertex});
    }
  }
  return write_items (simplify (result));
}

object native_upward_navigation (object t) {
  return native_link_list_to_navigation (native_upward_link_list (t, true));
}

bool native_link_may_follow (object t) { return !is_null (native_upward_navigation (t)); }

object native_navigation_filter (object items, object type, object position, bool only_jumpable) {
  const bool any_type= is_bool (type) && as_bool (type);
  const bool any_position= is_bool (position) && as_bool (position);
  if ((!any_type && !is_string (type)) || (!any_position && !is_int (position)))
    return null_object ();
  return write_items (filter (read_items (items), any_type ? std::nullopt : std::optional<string> (as_string (type)),
                             any_position ? std::nullopt : std::optional<int> (as_int (position)), only_jumpable));
}

list<string> native_navigation_types (object items) { return types (read_items (items)); }

list<string> native_navigation_xtypes (object items) {
  const auto parsed= read_items (items);
  const auto direct= types (filter (parsed, std::nullopt, 1, true));
  const auto inverse= types (filter (parsed, std::nullopt, 0, true), "*");
  list<string> result;
  hashmap<string, bool> seen (false);
  for (list<string> names= direct * inverse; !is_nil (names); names= names->next)
    if (!seen[names->item]) {
      seen (names->item)= true;
      result= list<string> (names->item, result);
    }
  return reverse (result);
}

object native_navigation_first_xtype (object items, string type) {
  const bool inverse= N(type) > 0 && type[N(type)-1] == '*';
  if (inverse) type= type (0, N(type)-1);
  const auto matches= filter (read_items (items), type, inverse ? 0 : 1, true);
  return matches.empty () ? object (false) : write_item (matches.front ());
}

object native_automatic_link (object target, object options) {
  return automatic_link (content_to_tree (target), is_null (options) ? object ("Broken") : car (options));
}

object native_link_enumeration (object items) { return enumeration (items); }

object native_navigation_document (tree style, object items) {
  const auto parsed= read_items (items);
  if (parsed.empty ()) throw std::invalid_argument ("A link page requires navigation candidates");
  array<object> body;
  body << tree_to_stree (compound ("strong", "Source"))
       << automatic_link (tree (ID, parsed.front ().source), object ("Unaccessible"));
  append_sections (body, filter (parsed, std::nullopt, 1, true), "Direct ");
  append_sections (body, filter (parsed, std::nullopt, 0, true), "Inverse ");
  return cons (symbol_object ("document"),
    list_object (tree_to_stree (compound ("style", style)),
      list_object (symbol_object ("body"), cons (symbol_object ("document"), as_list_object (body)))));
}

namespace {
void update_link_id (string id) {
  for (list<tree> nodes= get_trees (id) * get_trees ("&" * id);
       !is_nil (nodes); nodes= nodes->next) {
    path ip= obtain_ip (nodes->item);
    if (ip_attached (ip)) get_server ()->typeset_update (reverse (ip));
  }
}

void visit_vertex (tree vertex) {
  const bool id_vertex= is_func (vertex, ID, 1);
  const string value= vertex[0]->label;
  if (!id_vertex || N(value) == 0 || value[0] != '%')
    declare_visited ((id_vertex ? "id:" : "url:") * value);
  hashmap<string, bool> updated (false);
  if (id_vertex) { update_link_id (value); updated (value)= true; }
  for (list<tree> links= get_links (vertex); !is_nil (links); links= links->next)
    for (int i=2; i<N(links->item); ++i) {
      const tree target= links->item[i];
      if (target != vertex && is_func (target, ID, 1) && !updated[target[0]->label]) {
        updated (target[0]->label)= true;
        update_link_id (target[0]->label);
      }
    }
}

void follow_item (const navigation_item& item) {
  visit_vertex (tree (ID, item.source));
  if (is_func (item.target, ID, 1) || is_compound (item.target, "url", 1))
    visit_vertex (item.target);
  native_link_go_to_vertex (write_target (item.target), write_attributes (item.attributes));
}
}

void native_navigation_follow (object items) {
  auto candidates= read_items (items);
  if (N(types (candidates)) >= 2) {
    navigation_items explicit_items;
    for (const auto& item: candidates)
      if (item.type != "automatic") explicit_items.push_back (item);
    candidates= std::move (explicit_items);
  }
  const object encoded= write_items (candidates);
  const list<string> choices= native_navigation_xtypes (encoded);
  if (is_nil (choices)) return;
  if (native_navigation_link_pages () && candidates.size () >= 2) {
    visit_vertex (tree (ID, candidates.front ().source));
    call ("build-navigation-page", encoded);
  }
  else if (is_nil (choices->next)) follow_item (candidates.front ());
  else if (!headless_mode) {
    const auto* owner= current_scheme_execution_context ();
    if (!owner || !owner->editor)
      throw std::runtime_error ("Link navigation requires an owning editor");
    tree snapshot (TUPLE);
    for (const auto& item: candidates) {
      tree row (TUPLE);
      row << item.type << item.attributes << as_string (item.position) << item.source << item.target;
      snapshot << row;
    }
    struct payload {
      athena_blob_id id;
      explicit payload (tree t): id (actor_tree_registry::instance ().store (std::move (t))) {}
      ~payload () { actor_tree_registry::instance ().discard (id); }
    };
    auto pending= std::make_shared<payload> (std::move (snapshot));
    std::vector<std::string> proposals;
    for (auto names= choices; !is_nil (names); names= names->next)
      proposals.emplace_back (names->item.data (), N(names->item));
    qt_post_to_main_thread ([pending, proposals, actor= owner->actor_id, view= owner->view_id] {
      QTMInteractiveField field {"Link type", "string", {}};
      for (const auto& value: proposals) field.proposals << string (value.c_str ());
      const auto selected= qtm_interactive_dialog ("Follow link", {field});
      if (N(selected) == 0) return;
      std::string choice (selected[0].data (), N(selected[0]));
      athena::avd::submit_source_task (actor, view, [pending, choice] (editor_rep&) {
        tree snapshot= actor_tree_registry::instance ().take (pending->id);
        string type (choice.c_str ());
        const bool inverse= N(type) > 0 && type[N(type)-1] == '*';
        if (inverse) type= type (0, N(type)-1);
        for (int i=0; i<N(snapshot); ++i) {
          tree row= snapshot[i];
          if (row[0] == type && as_int (row[2]->label) == (inverse ? 0 : 1) && jumpable (row[4])) {
            follow_item ({row[0]->label, row[1], as_int (row[2]->label), row[3]->label, row[4]});
            break;
          }
        }
      }, [] (std::string error) {
        if (!error.empty ()) std_warning << "Link navigation: " << string (error.c_str ()) << LF;
      });
    });
  }
}

void native_link_follow_ids (list<string> ids, string event) {
  native_navigation_follow (native_link_list_to_navigation (
    native_filter_link_list (native_ids_link_list (ids), event)));
}

void native_link_execute_script (string source, bool trusted, object arguments) {
  if (starts (source, "(") && ends (source, ")") && !starts (source, "(lambda ")) {
    get_current_editor ()->set_message ("Legacy script links are no longer supported", "Evaluate script");
    return;
  }
  array<object> quoted;
  for (; !is_null (arguments); arguments= cdr (arguments))
    quoted << list_object (symbol_object ("quote"), car (arguments));
  object expression= cons (eval ("'" * source), as_list_object (quoted));
  const bool allowed= trusted || as_bool (call ("secure?", expression));
  const string policy= get_preference ("security");
  object closure= eval (list_object (symbol_object ("lambda"), null_object (), expression));
  if (allowed || policy == "accept all scripts") exec_delayed (closure);
  else if (policy == "prompt on scripts") {
    if (headless_mode) return;
    command execute= as_actor_command (closure);
    std::string text (source.data (), N(source));
    qt_post_to_main_thread ([execute, text] () mutable {
      QTMInteractiveField field {"Execute " * string (text.c_str ()) * "?", "question", {}};
      field.proposals << string ("no") << string ("yes");
      const auto answer= qtm_interactive_dialog ("Evaluate script", {field});
      if (N(answer) == 1 && answer[0] == "yes") execute->apply ();
    });
  }
  else get_current_editor ()->set_message ("Unsecure script refused", "Evaluate script");
}

void native_link_go_to_vertex (object vertex, object attributes) {
  if (!is_list (vertex) || is_null (vertex) || !is_symbol (car (vertex))) return;
  const string kind= as_symbol (car (vertex));
  const auto fields= as_array_object (cdr (vertex));
  if (kind == "id" && N(fields) == 1)
    call ("go-to-id", fields[0], object (get_current_editor ()->the_path ()));
  else if (kind == "url" && N(fields) == 1) {
    string target= as_string (fields[0]);
    for (int i=0; i<N(target); ++i)
      if (target[i] == '#' || target[i] == '?') {
        target= target (0, i) * replace (target (i, N(target)), ":", "%3A");
        break;
      }
    call ("go-to-url", object (target), object (get_current_editor ()->the_path ()));
  }
  else if (kind == "script" && N(fields) > 0) {
    bool trusted= false;
    for (; !is_null (attributes); attributes= cdr (attributes)) {
      object entry= car (attributes);
      if (is_string (car (entry)) && as_string (car (entry)) == "secure") {
        trusted= is_string (cdr (entry)) && as_string (cdr (entry)) == "true";
        break;
      }
    }
    native_link_execute_script (as_string (fields[0]), trusted, cddr (vertex));
  }
}

void native_locus_link_follow () {
  editor ed= get_current_editor ();
  list<string> ids;
  for (auto nodes= native_link_active_upwards (ed->the_subtree (path_up (ed->the_path ())));
       !is_nil (nodes); nodes= nodes->next)
    ids= ids * get_ids (nodes->item);
  native_link_follow_ids (ids, "click");
}

namespace {
std::pair<url, string> split_link_url (url target) {
  const string text= as_string (target);
  for (int i=0; i<N(text); ++i)
    if (text[i] == '#' || text[i] == '?')
      return {url_system (text (0, i)), replace (text (i+1, N(text)), "%3A", ":")};
  return {target, ""};
}
}

object native_link_process_url (url target) {
  const auto parts= split_link_url (target);
  return list_object (object (parts.first), object (parts.second));
}

void native_link_post_load (url target) {
  const auto parts= split_link_url (target);
  const string query= parts.second;
  if (query == "") return;
  const string format= conversion_file_format (parts.first);
  editor ed= get_current_editor ();
  if (format == "texmacs-file" || format == "generic-file" || format == "html-file") {
    const string label= replace (query, "%3A", ":");
    ed->go_to_label (label);
    ed->set_message ("At " * label * ".", "");
  }
  else if (format == "cpp-file" || format == "java-file" || format == "scala-file" ||
           format == "python-file" || format == "julia-file") {
    const QUrlQuery parameters (QString::fromUtf8 (query.data (), N(query)));
    const QString line= parameters.queryItemValue ("line", QUrl::FullyDecoded);
    const QString column= parameters.queryItemValue ("column", QUrl::FullyDecoded);
    const QByteArray selected= parameters.queryItemValue ("select", QUrl::FullyDecoded).toUtf8 ();
    const string word (selected.constData (), selected.size ());
    generic_go_to_line (line.toInt (), null_object ());
    generic_go_to_column (column.toInt (), null_object ());
    const object position= generic_select_word (word, ed->the_subtree (path_up (ed->the_path ())), column.toInt ());
    const QByteArray line_text= line.toUtf8 ();
    ed->set_message (word, string (line_text.constData (), line_text.size ()) * ":" *
      as_string (is_int (position) ? as_int (position) : 0));
  }
  else cout << "Unhandled format for default queries: " << format << LF;
}
