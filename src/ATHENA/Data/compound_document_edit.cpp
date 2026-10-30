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
#include "Edit/Interface/edit_interface.hpp"
#include "node_metadata.hpp"
#include "patch.hpp"
#include "tree_cursor.hpp"
#include "convert.hpp"
#include "Data/Convert/Xml/clipboard_xml.hpp"
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
  const path first= range.first.empty () ? editor.command_tree_start (body, path ()) :
                                          position (range.first);
  const path last= range.last.empty () ? editor.command_tree_end (body, path ()) :
                                        position (range.last);
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

void require_clipboard_grid_context (editor_rep& editor, tree content, path first, path last) {
  if (!is_func (content, MATH_GRID) && !is_func (content, ROW) &&
      !is_func (content, CELL)) return;
  const path root= editor.the_buffer_path ();
  for (path p= common (first, last); root <= p; p= path_up (p)) {
    if (editor.test_subtree (p)) {
      const tree ancestor= editor.the_subtree (p);
      if (is_func (ancestor, MATH_GRID)) {
        if (!is_func (content, MATH_GRID))
          throw std::runtime_error ("Copy the native grid or MathBlock to preserve row numbering and cell context");
        if (p != root) {
          const tree parent= editor.the_subtree (path_up (p));
          if (is_func (parent, MATH_BLOCK, 1))
            throw std::runtime_error ("Native display grid selection must include its MathBlock context");
        }
        return;
      }
    }
    if (p == root || is_nil (p)) break;
  }
}

void report_history_result (const std::string& error) {
  if (!error.empty ()) std_warning << "Compound document history: " << string (error.c_str ()) << LF;
}
} // namespace

tree merge_clipboard_pieces (const std::vector<std::string>& pieces) {
  using namespace athena::document;
  if (pieces.empty ()) throw std::runtime_error ("Compound clipboard has no source pieces");
  std::vector<tree> selections;
  auto version= xml_format_version::v1;
  for (const auto& bytes: pieces) {
    tree selection= read_clipboard_xml (bytes);
    const auto origin= *clipboard_version (selection);
    if (origin == xml_format_version::v3 ||
        (origin == xml_format_version::v2 && version == xml_format_version::v1))
      version= origin;
    selections.push_back (selection);
  }
  if (selections.size () == 1) return selections.front ();

  tree body (DOCUMENT);
  for (const auto& selection: selections) {
    validate_clipboard_paste (selection, version);
    if (N(selection) != 4)
      throw std::runtime_error ("Cannot combine clipboard pieces with source-move provenance");
    const string mode= selection[2]->label;
    if (mode != "text" && mode != "math" && mode != "prog")
      throw std::runtime_error ("Cannot combine clipboard pieces from this source mode");
    const tree content= selection[1];
    const auto append= [&] (tree paragraph) {
      if (mode == "math" && !is_func (paragraph, MATH_BLOCK) &&
          !is_compound (paragraph, "math", 1))
        paragraph= compound ("math", paragraph);
      else if (mode == "prog")
        paragraph= tree (WITH, "mode", "prog", paragraph);
      // The merged selection is text, but each piece keeps its source language.
      if (mode != "text" || selection[3] != "english")
        paragraph= tree (WITH, MODE_LANGUAGE (mode), selection[3], paragraph);
      body << paragraph;
    };
    if (is_document (content) && !athena::node::get (content))
      for (int i=0; i<N(content); ++i) append (content[i]);
    else append (content);
  }
  const tree merged= clipboard_with_version (tuple ("texmacs", body, "text", "english"), version);
  // Strict encoding also catches cross-piece duplicate UUIDs before any cut.
  return read_clipboard_xml (write_clipboard_xml (merged, version));
}

void erase_ranges (std::vector<source_range> ranges,
                   std::function<void(std::string)> completion) {
  replace_ranges (std::move (ranges), {}, std::move (completion));
}

void copy_ranges (std::vector<source_range> ranges,
                  std::function<void(std::string, clipboard_copy)> completion) {
  auto pieces= std::make_shared<std::vector<std::string>> (ranges.size ());
  auto text= std::make_shared<std::vector<std::string>> (ranges.size ());
  std::vector<edit_participant> participants;
  for (std::size_t i= 0; i < ranges.size (); ++i) {
    const auto range= ranges[i];
    edit_participant member;
    member.actor= range.actor; member.view= range.view;
    member.prepare= [range] (editor_rep& editor) {
      if (current_scheme_execution_context ()->actor->source_epoch () != range.epoch)
        throw std::runtime_error ("Source changed after compound selection");
      (void) range_positions (editor, range);
    };
    member.apply= [range, pieces, text, i] (editor_rep& editor) {
      const auto positions= range_positions (editor, range);
      editor.select (positions.first, positions.second);
      path first, last;
      editor.selection_get (first, last);
      const tree content= editor.selection_get ();
      tree mode= editor.selection_get_env_value ("mode");
      if (mode != "src") require_clipboard_grid_context (editor, content, first, last);
      if (mode != "src" && is_func (content, MATH_BLOCK)) mode= "text";
      const auto version= current_scheme_execution_context ()->actor->current_state ()->storage_version;
      (*pieces)[i]= athena::document::write_clipboard_xml (
        tuple ("texmacs", content, mode,
               editor.selection_get_env_value (MODE_LANGUAGE (as_string (mode)))), version);
      // Expansion belongs to the source owner, before separate physical
      // documents lose their local definitions in the merged clipboard tree.
      const tree expanded= editor.exec_verbatim (content, editor.selection_get_cursor_path ());
      const string plain= tree_to_verbatim (expanded, false, "utf-8");
      (*text)[i].assign (plain.data (), N(plain));
    };
    member.rollback= member.commit= [] (editor_rep&) {};
    participants.push_back (std::move (member));
  }
  submit_edit_batch (std::move (participants), [pieces, text, completion= std::move (completion)] (std::string error) {
    clipboard_copy result;
    if (error.empty ()) {
      result.pieces= std::move (*pieces);
      for (std::size_t i=0; i<text->size (); ++i) {
        if (i != 0) result.plain_text += '\n';
        result.plain_text += (*text)[i];
      }
    }
    completion (std::move (error), std::move (result));
  });
}

void replace_ranges (std::vector<source_range> ranges, std::string clipboard_xml,
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
    const bool insert= range.actor == ranges.front ().actor && !clipboard_xml.empty ();
    auto replacement= std::make_shared<std::shared_ptr<const native_paste_plan>> ();
    auto effective= std::make_shared<std::pair<path, path>> ();
    member.prepare= [range, insert, clipboard_xml, replacement, effective] (editor_rep& editor) {
      require_writable ();
      if (editor.inside_active_graphics ())
        throw std::runtime_error ("Compound range editing does not support an active graphics selection");
      if (current_scheme_execution_context ()->actor->source_epoch () != range.epoch)
        throw std::runtime_error ("A source changed after the compound selection was made");
      const auto positions= range_positions (editor, range);
      editor.select (positions.first, positions.second);
      editor.selection_get (effective->first, effective->second);
      if (insert) {
        const tree selection= athena::document::read_clipboard_xml (clipboard_xml);
        const path cursor= editor.the_path ();
        editor.go_to (effective->first);
        try {
          if (editor.inside_active_graphics ())
            throw std::runtime_error ("Compound replacement does not support a graphics destination");
          *replacement= editor.selection_prepare_paste (selection);
        }
        catch (...) { editor.go_to (cursor); throw; }
        editor.go_to (cursor);
      }
    };
    member.apply= [marker, insert, replacement, effective] (editor_rep& editor) {
      editor.start_editing ();
      editor.archive_state ();
      editor.start_slave (marker);
      editor.select (effective->first, effective->second);
      editor.selection_cut ("none");
      if (insert) editor.selection_apply_paste (**replacement);
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
