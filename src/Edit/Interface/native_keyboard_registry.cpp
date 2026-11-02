/******************************************************************************
* MODULE     : native_keyboard_registry.cpp
* DESCRIPTION: Native keyboard registration and context-sensitive lookup
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#include "native_keyboard_registry.hpp"
#include "analyze.hpp"
#include "native_keyboard_loader.hpp"
#include "user_shortcuts.hpp"
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

struct binding {
  native_keyboard_context context;
  native_key_binding value;
  std::string key, source;
  binding (native_keyboard_context c, native_key_binding a, string k, string s):
    context (std::move (c)), value (std::move (a)), key (bytes (k)), source (bytes (s)) {}
};

using binding_ptr= std::shared_ptr<const binding>;
using bindings= std::vector<binding_ptr>;
using binding_index= std::unordered_map<std::string, bindings>;

struct registry {
  std::mutex mutex;
  binding_index forward, inverse;

  void erase (const std::string& context, const std::string& key) {
    auto found= forward.find (key);
    if (found == forward.end ()) return;
    auto& entries= found->second;
    for (auto it= entries.begin (); it != entries.end ();) {
      if ((*it)->context.identity != context) { ++it; continue; }
      const auto old= *it;
      auto inv= inverse.find (old->source);
      auto& reverse= inv->second;
      reverse.erase (std::remove (reverse.begin (), reverse.end (), old), reverse.end ());
      if (reverse.empty ()) inverse.erase (inv);
      it= entries.erase (it);
    }
    if (entries.empty ()) forward.erase (found);
  }

  void insert (native_keyboard_context context, string key,
               native_key_binding action, string source) {
    auto entry= std::make_shared<binding> (std::move (context), std::move (action), key, source);
    std::lock_guard<std::mutex> lock (mutex);
    erase (entry->context.identity, entry->key);
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

binding_ptr exact (const std::string& context, string key) {
  auto& r= keyboard ();
  const auto entries= r.snapshot (r.forward, key);
  for (const auto& entry: entries)
    if (entry->context.identity == context) return entry;
  for (const auto& entry: entries)
    if (entry->context.identity.empty ()) return entry;
  return {};
}

binding_ptr resolve (const bindings& entries) {
  for (const auto& entry: entries)
    if (!entry->context.matches || entry->context.matches ()) return entry;
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

void native_keyboard_bind (native_keyboard_context context, string key,
                           native_key_binding action, string source) {
  key= get_server ()->kbd_pre_rewrite (key);
  int previous= 0;
  for (int end= 0; end < N(key); ++end) {
    if (key[end] != ' ') continue;
    const string prefix= key (0, end);
    if (!exact (context.identity, prefix)) {
      const string before= key (0, previous);
      const auto prior= exact (context.identity, before);
      string expansion= partial_text (prior && !prior->value.action ? text (prior->value.text) : before,
                                      key (previous, end));
      keyboard ().insert (context, prefix, {{}, bytes (expansion), ""}, scm_quote (expansion));
    }
    previous= end;
  }
  keyboard ().insert (std::move (context), key, std::move (action), source);
}

void native_keyboard_unbind (const std::string& context, string key) {
  key= get_server ()->kbd_pre_rewrite (key);
  auto& r= keyboard ();
  std::lock_guard<std::mutex> lock (r.mutex);
  r.erase (context, bytes (key));
}

std::optional<native_key_binding> native_keyboard_find (string key) {
  auto user= user_shortcuts_find (key);
  if (user) return user;
  native_keyboard_force (false);
  auto& r= keyboard ();
  const auto entry= resolve (r.snapshot (r.forward, key));
  return entry ? std::optional<native_key_binding> (entry->value) : std::nullopt;
}

bool native_keyboard_has_binding (string key) {
  return native_keyboard_find (key).has_value ();
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
      rows << tree (ROW, tree (CELL, key),
        tree (CELL, compound ("verbatim", text (binding->source))),
        tree (CELL, binding->context.identity.empty () ? tree ("Global") :
          compound ("verbatim", text (binding->context.identity))));
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
