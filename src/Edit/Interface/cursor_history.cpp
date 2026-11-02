// Copyright (C) 2002 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "cursor_history.hpp"
#include "editor.hpp"
#include "server.hpp"
#include <map>
#include <memory>
#include <vector>

namespace {
struct saved_position {
  observer position;
  explicit saved_position (path p): position (get_current_editor ()->position_new (p)) {}
  ~saved_position () {
    tree t;
    int offset;
    if (position->get_position (t, offset)) detach_observer (t, position);
  }
  bool attached () {
    tree t;
    int offset;
    return position->get_position (t, offset) && ip_attached (obtain_ip (t)) && !is_func (t, UNINIT);
  }
};
struct history {
  std::vector<std::unique_ptr<saved_position>> past, future;
};
history& current_history () {
  // Position observers and their trees remain on the owning BufferActor.
  static thread_local std::map<int, history> histories;
  return histories[get_server ()->get_window_serial ()];
}
bool same (const std::vector<std::unique_ptr<saved_position>>& entries, path p) {
  return !entries.empty () && entries.back ()->attached () &&
    get_current_editor ()->position_get (entries.back ()->position) == p;
}
void move_history (bool forward) {
  editor ed= get_current_editor ();
  history& h= current_history ();
  auto& source= forward ? h.future : h.past;
  auto& destination= forward ? h.past : h.future;
  while (!source.empty ()) {
    auto entry= std::move (source.back ());
    source.pop_back ();
    if (!entry->attached ()) continue;
    path target= ed->position_get (entry->position);
    destination.push_back (std::move (entry));
    if (target == ed->the_path ()) continue;
    ed->go_to (target);
    ed->show_cursor_if_hidden ();
    return;
  }
}
}

void cursor_history_add (path p) {
  history& h= current_history ();
  if (same (h.future, p)) {
    h.past.push_back (std::move (h.future.back ()));
    h.future.pop_back ();
  }
  else if (!same (h.past, p)) h.past.push_back (std::make_unique<saved_position> (p));
}
void cursor_history_backward () { move_history (false); }
void cursor_history_forward () { move_history (true); }
