
/******************************************************************************
* MODULE     : edit_modify.cpp
* DESCRIPTION: base routines for modifying the edit tree + notification
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "modification.hpp"
#include "edit_modify.hpp"
#include "ATHENA/Data/compound_document_edit.hpp"
#include "tm_window.hpp"
#include "scheme.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/Data/node_reference.hpp"
#include "ATHENA/buffer_actor.hpp"
#ifdef EXPERIMENTAL
#include "../../Style/Memorizer/clean_copy.hpp"
#endif

/******************************************************************************
* Constructors and destructors
******************************************************************************/

edit_modify_rep::edit_modify_rep ():
  editor_rep (), // NOTE: ignored by the compiler, but suppresses warning
  author (new_author ()),
  arch (author, rp),
  editing_depth (0) {}
edit_modify_rep::~edit_modify_rep () {}

/******************************************************************************
* Notification of changes in document
******************************************************************************/

void
edit_modify_rep::notify_assign (path p, tree u) {
  (void) u;
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_assign (get_typesetter (), p / rp, u);
}

void
edit_modify_rep::notify_insert (path p, tree u) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_insert (get_typesetter (), p / rp, u);
}

void
edit_modify_rep::notify_remove (path p, int nr) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_remove (get_typesetter (), p / rp, nr);
}

void
edit_modify_rep::notify_split (path p) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_split (get_typesetter (), p / rp);
}

void
edit_modify_rep::notify_join (path p) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_join (get_typesetter (), p / rp);
}

void
edit_modify_rep::notify_assign_node (path p, tree_label op) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_assign_node (get_typesetter (), p / rp, op);
}

void
edit_modify_rep::notify_insert_node (path p, tree t) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_insert_node (get_typesetter (), p / rp, t);
}

void
edit_modify_rep::notify_remove_node (path p) {
  if (!(rp <= p)) return;
  cur_pos= position_new (tp);
  ::notify_remove_node (get_typesetter (), p / rp);
}

void
edit_modify_rep::notify_set_cursor (path p, tree data) {
  if (!(rp <= p)) return;
  if (data[0] == as_string (author)) {
    if (is_compound (data, "cursor", 1) ||
        is_compound (data, "cursor-clear", 1)) {
      if (tp != p) {
        tp= p;
        go_to_correct (tp);
      }
      if (is_compound (data, "cursor-clear", 1)) {
        //cout << "Clear selection\n";
        select (tp, tp);
      }
    }
    else if (is_compound (data, "start", 1)) {
      if (selection_get_start () != p) {
        //cout << "Set start selection: " << p << "\n";
        select (p, p);
      }
    }
    else if (is_compound (data, "end", 1)) {
      if (selection_get_end () != p) {
        //cout << "Set end selection: " << p << "\n";
        selection_set_end (p);
      }
    }
  }
}

void
edit_modify_rep::post_notify (path p) {
  // cout << "Post notify\n";
  if (!(rp <= p)) return;
  if (buf && buf->actor) buf->actor->source_changed ();
  athena::node_reference::source_changed ();
  selection_cancel ();
  cancel_alt_selections ();
  notify_change (THE_TREE);
  tp= position_get (cur_pos);
  position_delete (cur_pos);
  cur_pos= nil_observer;
  go_to_correct (tp);
  if (get_preference ("heading word counts", "off") == "on")
    exec_delayed (scheme_cmd ("(heading-word-count-schedule-refresh)"));
  /*
  cout << "et= " << et << "\n";
  cout << "tp= " << tp << "\n\n";
  */
}

/******************************************************************************
* Hooks / notify changes to editor
******************************************************************************/

// FIXME: the notification might be slow when we have many
// open buffers. In the future, we might obtain the relevant editors
// from all possible prefixes of p using a hashtable

// FIXME: the undo system is not safe when a change is made inside
// a buffer which has no editor attached to it

void
edit_assign (editor_rep* ed, path pp, tree u) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_assign (p, u);
}

void
edit_insert (editor_rep* ed, path pp, tree u) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_insert (p, u);
}

void
edit_remove (editor_rep* ed, path pp, int nr) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  if (nr <= 0) return;
  ed->notify_remove (p, nr);
}

void
edit_split (editor_rep* ed, path pp) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_split (p);
}

void
edit_join (editor_rep* ed, path pp) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  if (N(p)<1) FAILED ("path too short in join");
  ed->notify_join (p);
}

void
edit_assign_node (editor_rep* ed, path pp, tree_label op) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_assign_node (p, op);
}

void
edit_insert_node (editor_rep* ed, path pp, tree t) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_insert_node (p, t);
}

void
edit_remove_node (editor_rep* ed, path pp) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_remove_node (p);
}

void
edit_set_cursor (editor_rep* ed, path pp, tree data) {
  path p= copy (pp);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  ed->notify_set_cursor (p, data);
}

void
edit_announce (editor_rep* ed, modification mod) {
  // Bridges reconstruct structural edits independently. Use an exact preview
  // when those reconstructions would discard source metadata or replay labels.
  path p= root (mod);
  tree source= subtree (ed->et, p);
  bool preview= mod->k == MOD_SET_METADATA;
  // Plain insert/remove notifications can remain incremental: the bridge
  // reconstruction helpers preserve node metadata.  Promoting every keystroke
  // in an identified source node to a whole-node assign corrupts the paragraph
  // bridge and needlessly restarts progressive typesetting below the edit.
  if (mod->k == MOD_ASSIGN_NODE || mod->k == MOD_INSERT_NODE)
    preview= athena::node::get (source) != nullptr ||
      (mod->k == MOD_INSERT_NODE &&
       athena::node::get (inserted_node_template (mod)) != nullptr);
  if (restores_child_header (mod)) preview= true;
  if (mod->k == MOD_SPLIT) {
    preview= athena::node::get (source) != nullptr;
    if (has_node_headers (mod)) {
      tree child= source[index (mod)];
      preview= preview || athena::node::get (child) != nullptr ||
        athena::node::get (mod->t[0]) != nullptr ||
        athena::node::get (mod->t[1]) != nullptr ||
        L(mod->t[0]) != L(child) || L(mod->t[1]) != L(child);
    }
  }
  if (mod->k == MOD_JOIN) {
    preview= athena::node::get (source) != nullptr;
    if (has_node_headers (mod)) {
      tree header= single_node_header (mod);
      preview= preview || athena::node::get (source[index (mod)]) != nullptr ||
        athena::node::get (source[index (mod)+1]) != nullptr ||
        athena::node::get (header) != nullptr ||
        L(header) != L(source[index (mod)]);
    }
  }
  if (preview) {
    edit_assign (ed, p, clean_apply (source, mod / p));
    return;
  }
  switch (mod->k) {
  case MOD_ASSIGN:
    edit_assign (ed, mod->p, mod->t);
    break;
  case MOD_INSERT:
    edit_insert (ed, mod->p, mod->t);
    break;
  case MOD_REMOVE:
    edit_remove (ed, path_up (mod->p), last_item (mod->p));
    break;
  case MOD_SPLIT:
    edit_split (ed, mod->p);
    break;
  case MOD_JOIN:
    edit_join (ed, mod->p);
    break;
  case MOD_ASSIGN_NODE:
    edit_assign_node (ed, mod->p, L(mod));
    break;
  case MOD_INSERT_NODE:
    edit_insert_node (ed, mod->p, mod->t);
    break;
  case MOD_REMOVE_NODE:
    edit_remove_node (ed, mod->p);
    break;
  case MOD_SET_CURSOR:
    edit_set_cursor (ed, mod->p, mod->t);
    break;
  default: FAILED ("invalid modification type");
  }
}

void
edit_done (editor_rep* ed, modification mod) {
  path p= copy (mod->p);
  ASSERT (ed->the_buffer_path() <= p, "invalid modification");
  if (ed->buf != nullptr && ed->buf->node_identities)
    ed->buf->node_identities->observe (mod / ed->rp);
  if (mod->k != MOD_SET_CURSOR)
    ed->post_notify (p);
#ifdef EXPERIMENTAL
  copy_announce (subtree (ed->et, ed->rp), ed->cct, mod / ed->rp);
#endif
}

void
edit_touch (editor_rep* ed, path p) {
  //cout << "Touch " << p << "\n";
  ASSERT (ed->the_buffer_path() <= p, "invalid touch");
  ed -> typeset_invalidate (p);
}

/******************************************************************************
* undo and redo handling
******************************************************************************/

void
edit_modify_rep::clear_undo_history () {
  global_clear_history ();
}

double
edit_modify_rep::this_author () {
  return author;
}

void
edit_modify_rep::archive_state () {
  path sp1= selection_get_start ();
  path sp2= selection_get_end ();
  if (path_less (sp1, sp2)) {
    //cout << "Selection: " << sp1 << "--" << sp2 << "\n";
    set_cursor (sp2, compound ("end", as_string (author)));
    set_cursor (sp1, compound ("start", as_string (author)));
    set_cursor (tp, compound ("cursor", as_string (author)));
  }
  else set_cursor (tp, compound ("cursor-clear", as_string (author)));
}

void
edit_modify_rep::start_editing () {
  //cout << "Start editing" << LF << INDENT;
  if (editing_depth++ == 0) set_author (this_author ());
}

void
edit_modify_rep::end_editing () {
  //cout << UNINDENT << "End editing" << LF;
  if (editing_depth > 1) {
    --editing_depth;
    return;
  }
  editing_depth= 0;
  if (buf != nullptr && buf->read_only && arch->has_content_changes ()) {
    if (pending_source_move_token != "") {
      athena::document_node::release_source_move (
        std::string (as_charp (pending_source_move_token),
                     (std::size_t) N(pending_source_move_token)));
      pending_source_move_token= "";
      pending_source_move_marker= 0.0;
    }
    global_cancel ();
    if (buf->node_identities) buf->node_identities->cancelled (subtree (et, rp));
    set_message ("This view is read-only", "edit");
    return;
  }
  if (!finish_node_identities ()) {
    if (pending_source_move_token != "") {
      athena::document_node::release_source_move (
        std::string (as_charp (pending_source_move_token),
                     (std::size_t) N(pending_source_move_token)));
      pending_source_move_token= "";
      pending_source_move_marker= 0.0;
    }
    return;
  }
  if (pending_source_move_token != "") {
    const std::string token (
      as_charp (pending_source_move_token),
      (std::size_t) N(pending_source_move_token));
    if (!athena::document_node::activate_source_move (token)) {
      global_cancel ();
      if (buf != nullptr && buf->node_identities)
        buf->node_identities->cancelled (subtree (et, rp));
      pending_source_move_token= "";
      pending_source_move_marker= 0.0;
      set_message ("Move paste rejected", "cut/paste");
      return;
    }
    pending_source_move_token= "";
    pending_source_move_marker= 0.0;
  }
  global_confirm ();
}

bool
edit_modify_rep::node_identities_active () {
  return buf != nullptr && bool (buf->node_identities);
}

bool
edit_modify_rep::adopt_node_identities () {
  if (buf == nullptr) return false;
  if (buf->node_identities) return finish_node_identities ();
  // This explicit staged loader hook adopts a validated baseline, not an
  // undoable switch. Do not leave pre-activation anonymous history behind it.
  if (arch->has_content_changes () || arch->undo_possibilities () != 0 ||
      arch->redo_possibilities () != 0) {
    std_warning << "Source identity activation rejected: history is not a fresh baseline" << LF;
    set_message ("Source identities require a fresh history baseline", "node model");
    return false;
  }
  // A just-loaded editor need not have reached its first GUI idle/typesetting
  // update. Resolve the source style/preamble DRD at this cold boundary rather
  // than treating uninitialized macro child descriptors as an invalid source.
  drd_update ();
  auto state= std::make_unique<athena::document_node::source_identity_state> ();
  const auto errors= state->initialize_complete (
    subtree (et, rp), drd, athena::document_node::standard_source_role);
  if (!errors.empty ()) {
    std_warning << "Source identity activation rejected: "
                << string (errors.front ().detail.c_str ()) << LF;
    set_message ("Source identity activation rejected",
                 tree (errors.front ().detail.c_str ()), true);
    return false;
  }
  buf->node_identities= std::move (state);
  return true;
}

bool
edit_modify_rep::finish_node_identities () {
  if (buf == nullptr || !buf->node_identities || !buf->node_identities->pending ()) return true;
  auto& state= *buf->node_identities;
  tree& body= subtree (et, rp);
  const auto plan= state.prepare (body, drd, athena::document_node::standard_source_role,
    [] (const athena::document_node::identity_request&) { return athena::node::new_id (); });
  if (!plan.ok ()) {
    global_cancel ();
    state.cancelled (body);
    set_message ("Edit rejected", tree (plan.diagnostics.front ().detail.c_str ()), true);
    return false;
  }
  try { state.apply (body, plan); }
  catch (...) {
    global_cancel ();
    // A failed apply may have partially updated the disposable identity index.
    // Rebuild it only on this exceptional rollback path, never on keystrokes.
    state.cancelled (body);
    throw;
  }
  try {
    buf->artifacts.update (*buf, body, plan.scope);
    // Artifact binding is metadata in the same undo transaction. Finalize any
    // newly identified bold source nodes before confirming that transaction.
    if (state.pending ()) {
      const auto bound= state.prepare (body, drd,
        athena::document_node::standard_source_role,
        [] (const athena::document_node::identity_request&) { return athena::node::new_id (); });
      if (!bound.ok ()) throw std::runtime_error (bound.diagnostics.front ().detail);
      state.apply (body, bound);
    }
  }
  catch (...) {
    buf->artifacts.reset ();
    global_cancel (); state.cancelled (body);
    athena::artifact::close (buf->actor->id ());
    throw;
  }
  return true;
}

void
edit_modify_rep::source_move_paste_pending (string token, double marker) {
  if (pending_source_move_token != "") {
    athena::document_node::release_source_move (
      std::string (as_charp (pending_source_move_token),
                   (std::size_t) N(pending_source_move_token)));
  }
  pending_source_move_token= std::move (token);
  pending_source_move_marker= marker;
}

bool
edit_modify_rep::source_move_cut_marker_present (double marker) {
  return arch->has_undo_move_marker (marker);
}

double
edit_modify_rep::source_move_undo_marker () {
  return arch->undo_move_marker ();
}

double
edit_modify_rep::source_move_redo_marker (int branch) {
  return arch->redo_move_marker (branch);
}

bool
edit_modify_rep::source_move_redo_available (double marker) {
  return arch->redo_move_branch (marker) >= 0;
}

bool
edit_modify_rep::source_move_undo_local (double marker) {
  if (buf != nullptr && buf->read_only) return false;
  if (arch->undo_move_marker () != marker) return false;
  arch->forget_cursor ();
  path p= arch->undo ();
  if (!is_nil (p)) go_to (p);
  return true;
}

bool
edit_modify_rep::source_move_redo_local (double marker) {
  if (buf != nullptr && buf->read_only) return false;
  const int branch= arch->redo_move_branch (marker);
  if (branch < 0) return false;
  arch->forget_cursor ();
  path p= arch->redo (branch);
  if (!is_nil (p)) go_to (p);
  return true;
}

void
edit_modify_rep::cancel_editing () {
  //cout << UNINDENT << "Cancel editing" << LF;
  editing_depth= 0;
  if (pending_source_move_token != "") {
    athena::document_node::release_source_move (
      std::string (as_charp (pending_source_move_token),
                   (std::size_t) N(pending_source_move_token)));
    pending_source_move_token= "";
    pending_source_move_marker= 0.0;
  }
  global_cancel ();
  if (buf != nullptr && buf->node_identities)
    buf->node_identities->cancelled (subtree (et, rp));
}

void
edit_modify_rep::start_slave (double a) {
  arch->start_slave (a);
}

void
edit_modify_rep::mark_start (double a) {
  //cout << "Mark start " << a << LF << INDENT;
  arch->mark_start (a);
}

bool
edit_modify_rep::mark_cancel (double a) {
  //cout << UNINDENT << "Mark cancel " << a << LF;
  return arch->mark_cancel (a);
}

void
edit_modify_rep::mark_end (double a) {
  //cout << UNINDENT << "Mark end " << a << LF;
  arch->mark_end (a);
}

void
edit_modify_rep::add_undo_mark () {
  //cout << "Add undo mark" << LF;
  if (finish_node_identities ()) arch->confirm ();
}

void
edit_modify_rep::remove_undo_mark () {
  //cout << "Remove undo mark" << LF;
  arch->retract ();
}

int
edit_modify_rep::undo_possibilities () {
  return arch->undo_possibilities ();
}

bool
edit_modify_rep::coordinate_source_move_history (bool redo) {
  if (buf == nullptr || buf->actor == nullptr) return false;
  const double marker= redo ? source_move_redo_marker () :
                              source_move_undo_marker ();
  if (marker == 0.0) return false;

  using namespace athena::document_node;
  const source_move_endpoint current {buf->actor->id (), runtime_view_id};
  auto move= source_move_for_history (marker, current);
  if (!move) return false;
  const source_move_state expected=
    redo ? source_move_state::undone : source_move_state::active;
  const source_move_state next=
    redo ? source_move_state::active : source_move_state::undone;
  if (move->state != expected) {
    set_message ("Move history is not in the expected state", "undo");
    return true;
  }

  enum class history_action { query_undo, query_redo, undo, redo };
  auto run= [&] (source_move_endpoint endpoint,
                 history_action action) -> bool {
    auto execute= [marker, action] (editor_rep* editor) {
      if (editor == nullptr) return false;
      switch (action) {
      case history_action::query_undo:
        return editor->source_move_undo_marker () == marker;
      case history_action::query_redo:
        return editor->source_move_redo_available (marker);
      case history_action::undo:
        return editor->source_move_undo_local (marker);
      case history_action::redo:
        return editor->source_move_redo_local (marker);
      }
      return false;
    };

    if (endpoint.actor == current.actor)
      return execute (buf->actor->current_editor (endpoint.view));

    struct response { bool value= false; };
    auto answer= std::make_shared<response> ();
    auto continuation= actor_continuation_registry::instance ().store (
      [answer, marker, action] {
        const auto* context= current_scheme_execution_context ();
        editor_rep* editor= context ? context->editor : nullptr;
        if (editor == nullptr) return;
        switch (action) {
        case history_action::query_undo:
          answer->value= editor->source_move_undo_marker () == marker;
          break;
        case history_action::query_redo:
          answer->value= editor->source_move_redo_available (marker);
          break;
        case history_action::undo:
          answer->value= editor->source_move_undo_local (marker);
          break;
        case history_action::redo:
          answer->value= editor->source_move_redo_local (marker);
          break;
        }
      });
    if (!buffer_actor::invoke_on (
          endpoint.actor, actor_command_kind::run_native_continuation,
          endpoint.view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
          SCHEME_CAPABILITY_BUFFER, continuation)) {
      actor_continuation_registry::instance ().discard (continuation);
      return false;
    }
    return answer->value;
  };

  const source_move_endpoint first= redo ? move->source : move->target;
  const source_move_endpoint second= redo ? move->target : move->source;
  const history_action query= redo ? history_action::query_redo :
                                     history_action::query_undo;
  const history_action perform= redo ? history_action::redo :
                                       history_action::undo;
  const history_action rollback= redo ? history_action::undo :
                                        history_action::redo;

  // Both halves must still be the next history item before either side moves.
  // This is the conflict boundary for cross-document undo: the user first
  // undoes any later independent edits in the peer buffer.
  if (!run (first, query) || !run (second, query)) {
    set_message (
      redo ? "Redo later edits in the other document before redoing this move"
           : "Undo later edits in the other document before undoing this move",
      redo ? "redo" : "undo");
    return true;
  }
  if (!run (first, perform)) {
    set_message ("Could not update the first half of the document move",
                 redo ? "redo" : "undo");
    return true;
  }
  if (!run (second, perform)) {
    (void) run (first, rollback);
    set_message ("Could not update both halves of the document move",
                 redo ? "redo" : "undo");
    return true;
  }
  if (!set_source_move_history_state (marker, expected, next)) {
    // The process-local move registry is the authority for coordinating future
    // history. Restore the document state rather than leave an untracked half.
    (void) run (second, rollback);
    (void) run (first, rollback);
    set_message ("Move history coordination was lost", redo ? "redo" : "undo");
    return true;
  }
  return true;
}

void
edit_modify_rep::undo (bool redoable) {
  interrupt_shortcut ();
  if (buf != nullptr && buf->read_only) {
    set_message ("This view is read-only", "undo");
    return;
  }
  arch->forget_cursor ();
  if (redoable && athena::avd::coordinate_compound_history (false)) return;
  bool in_graphics= inside_graphics ();
  bool native_graphics_history=
    in_graphics && native_graphics_owns_history ();
  if (arch->undo_possibilities () == 0) {
    set_message ("No more undo information available", "undo"); return; }
  if (redoable && coordinate_source_move_history (false)) return;
  if (redoable) {
    path p= arch->undo ();
    if (!is_nil (p)) go_to (p);
  }
  else arch->forget ();
  if (arch->conform_save ()) {
    set_message ("Your document is back in its original state", "undo");
    beep (); }
  if (native_graphics_history) native_graphics_history_reset ();
}

void
edit_modify_rep::unredoable_undo () {
  undo (false);
}

void
edit_modify_rep::undo (int i) {
  ASSERT (i == 0, "invalid undo");
  undo (true);
}

int
edit_modify_rep::redo_possibilities () {
  return arch->redo_possibilities ();
}

void
edit_modify_rep::redo (int i) {
  interrupt_shortcut ();
  if (buf != nullptr && buf->read_only) {
    set_message ("This view is read-only", "redo");
    return;
  }
  arch->forget_cursor ();
  if (athena::avd::coordinate_compound_history (true, i)) return;
  bool in_graphics= inside_graphics ();
  bool native_graphics_history=
    in_graphics && native_graphics_owns_history ();
  if (arch->redo_possibilities () == 0) {
    set_message ("No more redo information available", "redo"); return; }
  if (coordinate_source_move_history (true)) return;
  path p= arch->redo (i);
  if (!is_nil (p)) go_to (p);
  if (arch->conform_save ()) {
    set_message ("Your document is back in its original state", "undo");
    beep (); }
  if (native_graphics_history) native_graphics_history_reset ();
}

void
edit_modify_rep::require_save () {
  if (buf && buf->actor) buf->actor->source_changed ();
  arch->require_autosave ();
  arch->require_save ();
}

void
edit_modify_rep::notify_save (bool real_save) {
  if (!finish_node_identities ()) return;
  arch->confirm ();
  arch->notify_autosave ();
  if (real_save) arch->notify_save ();
}

bool
edit_modify_rep::need_save (bool real_save) {
  if (arch->conform_save ()) return false;
  if (real_save) return true;
  return !arch->conform_autosave ();
}

/******************************************************************************
* handling multiple cursor positions
******************************************************************************/

observer
edit_modify_rep::position_new (path p) {
  tree st= subtree (et, path_up (p));
  int index= last_item (p);
  observer o= tree_position (st, index);
  attach_observer (st, o);
  return o;
}

void
edit_modify_rep::position_delete (observer o) {
  tree st;
  int  index;
  if (o->get_position (st, index))
    detach_observer (st, o);
}

void
edit_modify_rep::position_set (observer o, path p) {
  tree st= subtree (et, path_up (p));
  int index= last_item (p);
  o->set_position (st, index);
}

path
edit_modify_rep::position_get (observer o) {
  //return super_correct (et, obtain_position (o));
  return correct_cursor (et, obtain_position (o));
}
