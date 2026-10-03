/******************************************************************************
* MODULE     : compound_document_edit.cpp
* DESCRIPTION: Grouped source edits using native undo patches on their owners
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "compound_document_edit.hpp"
#include "compound_edit_batch.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "patch.hpp"
#include "tree_cursor.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace athena::avd {
namespace {
struct history_entry {
  double marker= 0;
  std::vector<source_range> sources;
  bool undone= false;
  bool busy= false;
};
std::mutex history_mutex;
std::map<double, std::shared_ptr<history_entry>> histories;

path position (const std::vector<int>& value) {
  path result;
  for (auto i= value.rbegin (); i != value.rend (); ++i) result= path (*i, result);
  return result;
}

std::pair<path, path> range_positions (editor_rep& editor, const source_range& range) {
  const tree body= editor.the_buffer ();
  const path first= range.first.empty () ? start (body) : position (range.first);
  const path last= range.last.empty () ? end (body) : position (range.last);
  if (!is_inside (body, first) || !is_inside (body, last) || path_less (last, first))
    throw std::runtime_error ("Compound selection no longer identifies a source range");
  const path root= editor.the_buffer_path ();
  return {root * first, root * last};
}

void require_writable () {
  const auto* context= current_scheme_execution_context ();
  if (context->actor->current_state ()->read_only)
    throw std::runtime_error ("A compound source is read-only");
}

void report_history_result (const std::string& error) {
  if (!error.empty ()) std_warning << "Compound document history: " << string (error.c_str ()) << LF;
}
} // namespace

void erase_ranges (std::vector<source_range> ranges,
                   std::function<void(std::string)> completion) {
  auto history= std::make_shared<history_entry> ();
  history->marker= new_marker ();
  history->sources= ranges;
  std::vector<edit_participant> participants;
  for (const auto& range: ranges) {
    const double marker= history->marker;
    edit_participant member;
    member.actor= range.actor;
    member.view= range.view;
    member.prepare= [range] (editor_rep& editor) {
      require_writable ();
      if (current_scheme_execution_context ()->actor->source_epoch () != range.epoch)
        throw std::runtime_error ("A source changed after the compound selection was made");
      (void) range_positions (editor, range);
    };
    member.apply= [range, marker] (editor_rep& editor) {
      const auto positions= range_positions (editor, range);
      editor.start_editing ();
      editor.archive_state ();
      editor.start_slave (marker);
      editor.select (positions.first, positions.second);
      editor.selection_cut ("none");
      if (!editor.finish_node_identities ())
        throw std::runtime_error ("Compound deletion could not preserve source identities");
    };
    member.rollback= [] (editor_rep& editor) { editor.cancel_editing (); };
    member.commit= [] (editor_rep& editor) {
      editor.end_editing ();
      editor.selection_cancel ();
    };
    participants.push_back (std::move (member));
  }
  submit_edit_batch (std::move (participants),
    [history, completion= std::move (completion)] (std::string error) {
      if (error.empty ()) {
        std::lock_guard<std::mutex> lock (history_mutex);
        histories.emplace (history->marker, history);
      }
      completion (std::move (error));
    });
}

bool coordinate_compound_history (bool redo, int branch) {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor || !context->editor) return false;
  const double requested= redo ? context->editor->source_move_redo_marker (branch) : 0;
  if (redo && branch != 0 && requested == 0) return false;
  std::vector<std::shared_ptr<history_entry>> candidates;
  {
    std::lock_guard<std::mutex> lock (history_mutex);
    for (auto it= histories.rbegin (); it != histories.rend (); ++it)
      for (const auto& source: it->second->sources)
        if (source.actor == context->actor_id) {
          candidates.push_back (it->second);
          break;
        }
  }
  std::shared_ptr<history_entry> selected;
  for (const auto& candidate: candidates) {
    if (requested != 0 && requested != candidate->marker) continue;
    for (const auto& source: candidate->sources) {
      if (source.actor != context->actor_id) continue;
      auto* owner= context->actor->current_editor (source.view);
      if (!owner) continue;
      const bool matches= redo && requested != 0 ?
        owner->source_move_redo_available (requested) :
        (redo ? owner->source_move_redo_marker () : owner->source_move_undo_marker ()) == candidate->marker;
      if (matches) selected= candidate;
    }
    if (selected) break;
  }
  if (!selected) return false;
  {
    std::lock_guard<std::mutex> lock (history_mutex);
    if (selected->busy || selected->undone != redo) {
      context->editor->set_message ("Compound history is already being updated", "undo");
      return true;
    }
    selected->busy= true;
  }
  const double marker= selected->marker;
  std::vector<edit_participant> participants;
  for (const auto& source: selected->sources) {
    auto performed= std::make_shared<bool> (false);
    edit_participant member;
    member.actor= source.actor;
    member.view= source.view;
    member.prepare= [marker, redo] (editor_rep& editor) {
      require_writable ();
      if (redo ? !editor.source_move_redo_available (marker) :
                 editor.source_move_undo_marker () != marker)
        throw std::runtime_error ("Resolve later edits in the other source documents before changing compound history");
    };
    member.apply= [marker, redo, performed] (editor_rep& editor) {
      editor.start_editing ();
      *performed= redo ? editor.source_move_redo_local (marker) :
                        editor.source_move_undo_local (marker);
      if (!*performed) throw std::runtime_error ("Compound history changed before replay");
    };
    member.rollback= [marker, redo, performed] (editor_rep& editor) {
      // Confirm observer bookkeeping before applying the inverse history step.
      editor.end_editing ();
      if (*performed) {
        editor.start_editing ();
        const bool restored= redo ? editor.source_move_undo_local (marker) :
                                    editor.source_move_redo_local (marker);
        if (!restored) throw std::runtime_error ("Could not restore compound history after a failed replay");
        editor.end_editing ();
      }
    };
    member.commit= [] (editor_rep& editor) {
      editor.end_editing ();
      // Source cursor history must not recreate untracked AVD selections.
      editor.selection_cancel ();
    };
    participants.push_back (std::move (member));
  }
  submit_edit_batch (std::move (participants), [selected, redo] (std::string error) {
    {
      std::lock_guard<std::mutex> lock (history_mutex);
      selected->busy= false;
      if (error.empty ()) selected->undone= !redo;
    }
    report_history_result (error);
  });
  return true;
}
} // namespace athena::avd
