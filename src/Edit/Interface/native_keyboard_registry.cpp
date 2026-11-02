/******************************************************************************
* MODULE     : native_keyboard_registry.cpp
* DESCRIPTION: Native keyboard registration and context-sensitive lookup
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#include "native_keyboard_registry.hpp"
#include "native_keyboard_loader.hpp"
#include "user_shortcuts.hpp"
#include "object.hpp"
#include "server.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

std::string bytes (string value) {
  return std::string (value.data (), N(value));
}

string text (const std::string& value) {
  return string (value.data (), value.size ());
}

// Share roots, not object wrappers: object uses a non-atomic reference count.
struct rooted_value {
  tmscm_root_handle* root;
  explicit rooted_value (object value):
    root (tmscm_root_acquire (object_to_tmscm (value))) {}
  ~rooted_value () { tmscm_root_release (root); }
  rooted_value (const rooted_value&)= delete;
  object get () const { return tmscm_to_object (tmscm_root_value (root)); }
};

struct binding {
  rooted_value conditions, action;
  std::string key, source, help;
  binding (object c, object a, string k, string s, string h):
    conditions (c), action (a), key (bytes (k)), source (bytes (s)), help (bytes (h)) {}
};

using binding_ptr= std::shared_ptr<const binding>;
using bindings= std::vector<binding_ptr>;
using binding_index= std::unordered_map<std::string, bindings>;

struct registry {
  std::mutex mutex;
  binding_index forward, inverse;

  void erase (object conditions, const std::string& key) {
    auto found= forward.find (key);
    if (found == forward.end ()) return;
    auto& entries= found->second;
    for (auto it= entries.begin (); it != entries.end ();) {
      if ((*it)->conditions.get () != conditions) { ++it; continue; }
      const auto old= *it;
      auto inv= inverse.find (old->source);
      auto& reverse= inv->second;
      reverse.erase (std::remove (reverse.begin (), reverse.end (), old), reverse.end ());
      if (reverse.empty ()) inverse.erase (inv);
      it= entries.erase (it);
    }
    if (entries.empty ()) forward.erase (found);
  }

  void insert (object conditions, string key, object action, string help) {
    object source= is_procedure (action) ? call ("promise-source", action) : action;
    auto entry= std::make_shared<binding> (conditions, action, key,
                                         object_to_string (source), help);
    std::lock_guard<std::mutex> lock (mutex);
    erase (conditions, entry->key);
    auto& f= forward[entry->key];
    f.insert (f.begin (), entry);
    auto& r= inverse[entry->source];
    r.insert (r.begin (), entry);
  }

  bindings snapshot (const binding_index& table, string key) {
    std::lock_guard<std::mutex> lock (mutex);
    auto found= table.find (bytes (key));
    return found == table.end () ? bindings () : found->second;
  }
};

registry& keyboard () {
  static registry value;
  return value;
}

binding_ptr exact (object conditions, string key) {
  auto& r= keyboard ();
  const auto entries= r.snapshot (r.forward, key);
  for (const auto& entry: entries)
    if (entry->conditions.get () == conditions) return entry;
  for (const auto& entry: entries)
    if (is_null (entry->conditions.get ())) return entry;
  return {};
}

binding_ptr resolve (const bindings& entries) {
  for (const auto& entry: entries) {
    bool matches= true;
    for (object c= entry->conditions.get (); !is_null (c); c= cdr (c)) {
      object result= call (car (c));
      if (is_bool (result) && !as_bool (result)) { matches= false; break; }
    }
    if (matches) return entry;
  }
  return {};
}

string partial_text (string previous, string suffix) {
  for (int i= 0; i < N(suffix); ++i) {
    if (suffix[i] == '<') previous << "<less>";
    else if (suffix[i] == '>') previous << "<gtr>";
    else if (suffix[i] != ' ') previous << suffix[i];
  }
  return previous;
}

} // namespace

void native_keyboard_bind (object conditions, string key, object action, string help) {
  key= get_server ()->kbd_pre_rewrite (key);
  int previous= 0;
  for (int end= 0; end < N(key); ++end) {
    if (key[end] != ' ') continue;
    const string prefix= key (0, end);
    if (!exact (conditions, prefix)) {
      const string before= key (0, previous);
      const auto prior= exact (conditions, before);
      object value= prior ? prior->action.get () : object (false);
      string expansion= partial_text (is_string (value) ? as_string (value) : before,
                                      key (previous, end));
      keyboard ().insert (conditions, prefix, object (expansion), "");
    }
    previous= end;
  }
  keyboard ().insert (conditions, key, action, help);
}

void native_keyboard_unbind (object conditions, string key) {
  key= get_server ()->kbd_pre_rewrite (key);
  auto& r= keyboard ();
  std::lock_guard<std::mutex> lock (r.mutex);
  r.erase (conditions, bytes (key));
}

void native_keyboard_bind_source (string key, object source) {
  object action= is_string (source) ? source :
    eval (list_object (symbol_object ("lambda"), null_object (), source));
  native_keyboard_bind (null_object (), key, action, "");
}

object native_keyboard_find (string key) {
  object user= user_shortcuts_find (key);
  if (!(is_bool (user) && !as_bool (user))) return user;
  native_keyboard_force (false);
  auto& r= keyboard ();
  const auto entry= resolve (r.snapshot (r.forward, key));
  return entry ? list_object (entry->action.get (), object (text (entry->help))) : object (false);
}

string native_keyboard_cached_inverse (object source) {
  string user= user_shortcuts_inverse (source);
  if (user != "") return user;
  auto& r= keyboard ();
  const auto entry= resolve (r.snapshot (r.inverse, object_to_string (source)));
  return entry ? text (entry->key) : string ("");
}

string native_keyboard_inverse (object source) {
  native_keyboard_force (false);
  return native_keyboard_cached_inverse (source);
}

object native_keyboard_system_inverse (object source) {
  tree label= get_server ()->kbd_system_rewrite (native_keyboard_inverse (source));
  if (is_compound (label, "render-key", 1)) label= label[0];
  if (is_func (label, WITH) && N(label) > 0) label= label[N(label)-1];
  return is_atomic (label) ? object (label->label) : object (false);
}

object native_keyboard_reverse (object source) {
  native_keyboard_force (false);
  if (is_tree (source)) source= tree_to_stree (as_tree (source));
  if (!is_string (source)) return object (false);
  auto& r= keyboard ();
  const auto entries= r.snapshot (r.inverse, object_to_string (string_to_object (as_string (source))));
  return entries.empty () ? object (false) : object (text (entries.front ()->key));
}

object native_keyboard_catalog () {
  auto& r= keyboard ();
  binding_index snapshot;
  {
    std::lock_guard<std::mutex> lock (r.mutex);
    snapshot= r.forward;
  }
  object result= null_object ();
  for (const auto& item: snapshot) {
    object entries= null_object ();
    for (auto it= item.second.rbegin (); it != item.second.rend (); ++it) {
      const auto& entry= *it;
      entries= cons (cons (entry->conditions.get (),
                          list_object (entry->action.get (), object (text (entry->help)))), entries);
    }
    result= cons (cons (object (text (item.first)), entries), result);
  }
  return result;
}

void native_keyboard_list_shortcuts () {
  auto& r= keyboard ();
  std::vector<std::pair<std::string, bindings>> entries;
  {
    std::lock_guard<std::mutex> lock (r.mutex);
    for (const auto& entry: r.forward) entries.push_back (entry);
  }
  std::sort (entries.begin (), entries.end (),
    [] (const auto& a, const auto& b) { return a.first < b.first; });
  tree rows (TABLE);
  rows << tree (ROW, tree (CELL, compound ("strong", "Shortcut")),
                    tree (CELL, compound ("strong", "Command")),
                    tree (CELL, compound ("strong", "Context")));
  for (const auto& entry: entries) {
    tree key= get_server ()->kbd_system_rewrite (text (entry.first));
    for (const auto& binding: entry.second) {
      object conditions= binding->conditions.get ();
      rows << tree (ROW, tree (CELL, key),
        tree (CELL, compound ("verbatim", object_to_string (binding->action.get ()))),
        tree (CELL, is_null (conditions) ? tree ("Global") :
          compound ("verbatim", object_to_string (conditions))));
    }
  }
  tree table (TFORMAT,
    tree (CWITH, "1", "-1", "1", "-1", "cell-background", "light grey"),
    tree (CWITH, "1", "1", "1", "-1", "cell-background", "grey"), rows);
  tree doc (DOCUMENT, compound ("section", "ATHENA Keyboard Shortcuts"),
            compound ("tabular", table));
  url name= create_buffer ();
  set_buffer_body (name, doc);
}
