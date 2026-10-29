
/******************************************************************************
* MODULE     : edit_complete.cpp
* DESCRIPTION: Tab completion
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_interface.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "Bridge/impl_typesetter.hpp"
#include "hashmap.hpp"
#include "hashset.hpp"
#include "analyze.hpp"
#include "scheme.hpp"
#include "vars.hpp"
#include "unicode_text.hpp"
#include "native_latex_commands.hpp"
#include "fuzzy_rank.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <chrono>

/******************************************************************************
* Finding completions in text
******************************************************************************/

static bool
source_authoring (const evaluation_environment_checkpoint& context) {
  return context.effective_value (MODE) == "src" &&
         context.effective_value (PREAMBLE) != "true";
}

static void
collect_source_signatures (const tree& source,
    std::map<std::string, athena::contracts::definition_signature>& signatures) {
  if (is_func (source, ASSIGN, 2) && is_atomic (source[0])) {
    const string name= source[0]->label;
    signatures[std::string (name.data (), N(name))]=
      athena::contracts::signature_of (source[1]);
  }
  else if (is_func (source, DOCUMENT) || is_func (source, CONCAT))
    for (int i=0; i<N(source); ++i)
      collect_source_signatures (source[i], signatures);
}

void
editor_rep::prepare_source_signatures (const evaluation_environment_checkpoint& context) {
  if (!source_authoring (context)) return;
  const tree body= subtree (et, rp);
  if (!source_signatures_valid) {
    source_signature_chunks.clear ();
    source_signature_origins.clear ();
    source_signatures.clear ();
    source_signature_dirty_chunks.clear ();
    for (int i=0; i<N(body); ++i) source_signature_dirty_chunks.insert (i);
    source_signatures_valid= true;
  }
  for (int index: source_signature_dirty_chunks) {
    auto& chunk= source_signature_chunks[index];
    std::set<std::string> changed;
    for (const auto& entry: chunk) {
      changed.insert (entry.first);
      source_signature_origins[entry.first].erase (index);
    }
    chunk.clear ();
    if (index < N(body)) collect_source_signatures (body[index], chunk);
    for (const auto& entry: chunk) {
      changed.insert (entry.first);
      source_signature_origins[entry.first][index]= entry.second;
    }
    for (const auto& name: changed) {
      auto& origins= source_signature_origins[name];
      if (origins.empty ()) {
        source_signature_origins.erase (name);
        source_signatures.erase (name);
      }
      else source_signatures[name]= origins.rbegin ()->second;
    }
  }
  source_signature_dirty_chunks.clear ();
}

std::optional<athena::contracts::definition_signature>
editor_rep::source_signature (string name, const evaluation_environment_checkpoint& context) const {
  if (!source_authoring (context)) return {};
  const auto found= source_signatures.find (std::string (name.data (), N(name)));
  if (found == source_signatures.end ()) return {};
  return found->second;
}

void
edit_interface_rep::source_indexes_changed (path p, bool text_only) {
  path relative= p / rp;
  if (is_nil (relative)) completion_index_valid= false;
  else if (completion_index_valid) {
    auto& chunk= completion_chunks[relative->item];
    chunk.complete= false;
    chunk.stack.clear ();
    chunk.scanning.clear ();
    completion_dirty_chunks.insert (relative->item);
  }
  if (is_nil (relative)) source_signatures_valid= false;
  else if (source_signatures_valid) source_signature_dirty_chunks.insert (relative->item);

  // Text edits do not move graphics paths. Graphics properties may still change
  // the set of active pen regions, so invalidate those explicitly.
  while (text_only && rp <= p) {
    tree t= subtree (et, p);
    if (is_func (t, GRAPHICS)) text_only= false;
    if (is_func (t, WITH))
      for (int i=0; i+1<N(t); i+=2)
        if (t[i] == GR_MODE) text_only= false;
    if (p == rp) break;
    p= path_up (p);
  }
  if (!text_only) mark_native_ink_interaction_dirty ();
}

void
edit_interface_rep::update_completion_index () {
  if (!completion_requested) return;
  const auto layout= get_typesetter ();
  if (!layout->execution_base) return;
  tree body= subtree (et, rp);
  if (!completion_index_valid) {
    completion_chunks.clear ();
    completion_words.clear ();
    completion_dirty_chunks.clear ();
    for (int i=0; i<N(body); ++i) completion_dirty_chunks.insert (i);
    completion_index_valid= true;
    completion_layout.reset ();
  }
  if (completion_layout != layout->execution_base) {
    completion_layout= layout->execution_base;
    ++completion_generation;
    completion_review_remaining= N(body);
    completion_waiting= false;
  }
  const auto occurrence_at= [&] (path p) {
    return find_source_occurrence (layout->br->source_occurrences,
      rp * p, subtree (body, p));
  };
  const auto context_at= [&] (const std::shared_ptr<const source_occurrence>& occurrence)
      -> std::shared_ptr<const evaluation_environment_checkpoint> {
    if (!occurrence) return {};
    const auto* context= layout->execution_contexts.find (occurrence->boundary);
    return context ? *context : nullptr;
  };
  const auto prepare= [&] (int i) {
    auto& chunk= completion_chunks[i];
    if (chunk.checked == completion_generation) return;
    chunk.checked= completion_generation;
    const auto occurrence= occurrence_at (path (i));
    const auto context= context_at (occurrence);
    if (!context || chunk.occurrence != occurrence || !chunk.context ||
        !context->contract_context ().same_state (chunk.context->contract_context ()) ||
        context->effective_value (MODE) != chunk.context->effective_value (MODE) ||
        context->effective_value (PREAMBLE) != chunk.context->effective_value (PREAMBLE) ||
        chunk.pending) {
      chunk.complete= false;
      chunk.stack.clear ();
      chunk.scanning.clear ();
    }
    chunk.context= context;
    chunk.occurrence= occurrence;
    if (!context) completion_waiting= true;
    if (context && !chunk.complete) completion_dirty_chunks.insert (i);
    else completion_dirty_chunks.erase (i);
  };
  const auto deadline= std::chrono::steady_clock::now () + std::chrono::milliseconds (4);
  const path focus= tp / rp;
  const int pivot= is_nil (focus)? 0: max (0, min (N(body)-1, focus->item));
  if (N(body) > 0) prepare (pivot);
  for (int n=0; completion_review_remaining > 0 && n<64 &&
       std::chrono::steady_clock::now () < deadline; ++n) {
    if (completion_review >= N(body)) completion_review= 0;
    prepare (completion_review++);
    --completion_review_remaining;
  }
  int budget= 256;
  while (!completion_dirty_chunks.empty () && budget > 0 &&
         std::chrono::steady_clock::now () < deadline) {
    auto next= completion_dirty_chunks.lower_bound (pivot);
    if (next == completion_dirty_chunks.end ()) next= completion_dirty_chunks.begin ();
    const int i= *next;
    prepare (i);
    auto& chunk= completion_chunks[i];
    if (!chunk.context) continue;
    if (chunk.stack.empty ()) {
      chunk.pending= false;
      chunk.stack.push_back ({path (i), -1, chunk.context, {}});
    }
    while (!chunk.stack.empty () && budget-- > 0 &&
           std::chrono::steady_clock::now () < deadline) {
      auto& frame= chunk.stack.back ();
      const tree t= subtree (body, frame.position);
      if (is_atomic (t)) {
        const std::string_view text (t->label.data (), N(t->label));
        if (frame.next == -1) {
          frame.words= athena::text::word_segments (text);
          frame.next= 0;
        }
        if (frame.next < int (frame.words.size ())) {
          const auto& word= frame.words[frame.next++];
          if (word.lexical)
            ++chunk.scanning[std::string (text.substr (word.begin, word.end-word.begin))];
        }
        else chunk.stack.pop_back ();
        continue;
      }
      if (frame.next == -1) {
        if (!frame.context) frame.context= context_at (occurrence_at (frame.position));
        if (!frame.context) {
          chunk.pending= true;
          completion_waiting= true;
          chunk.stack.pop_back ();
          continue;
        }
        frame.next= 0;
      }
      if (frame.next == N(t)) { chunk.stack.pop_back (); continue; }
      const int child= frame.next++;
      const bool source= frame.context->effective_value (MODE) == "src" ||
                         frame.context->effective_value (PREAMBLE) == "true";
      const athena::contracts::editing_query query {frame.context->contract_context (),
        source ? athena::contracts::access_policy::source : athena::contracts::access_policy::normal};
      if (query.accessible (t, child))
        chunk.stack.push_back ({frame.position * child, -1, {}, {}});
      else if (query.mapped_accessible (t, child)) {
        const path target= frame.position * child;
        const auto mapped= context_at (occurrence_at (target));
        if (mapped) chunk.stack.push_back ({target, -1, mapped, {}});
        else {
          const auto& contract= query.describe (t);
          const int field= contract.layout.index (child, N(t));
          if (field < 0 || field >= int (contract.fields.size ()) ||
              contract.fields[field].analysis.access != athena::contracts::inference_state::resolved) {
            chunk.pending= true;
            completion_waiting= true;
          }
        }
      }
    }
    if (!chunk.stack.empty ()) continue;
    for (const auto& word: chunk.words) {
      auto found= completion_words.find (word.first);
      found->second.erase (i);
      if (found->second.empty ()) completion_words.erase (found);
    }
    chunk.words= std::move (chunk.scanning);
    chunk.scanning.clear ();
    for (const auto& word: chunk.words) completion_words[word.first][i]= word.second;
    chunk.complete= true;
    completion_dirty_chunks.erase (i);
  }
}

array<string>
edit_interface_rep::indexed_completions (string prefix) {
  // Candidate lookup never prepares a source or scans a document. A word from
  // an unreviewed layout generation must not leak through its old contribution.
  const auto layout= get_typesetter ();
  if (!completion_index_valid || !layout->execution_base ||
      completion_layout != layout->execution_base) return {};
  std::string key (prefix.data (), N(prefix));
  std::vector<std::pair<std::string, int>> matches;
  for (auto it= completion_words.lower_bound (key);
       it != completion_words.end () && it->first.compare (0, key.size (), key) == 0;
       ++it)
    if (it->first != key) {
      int frequency= 0;
      for (const auto& source: it->second) {
        const auto& chunk= completion_chunks.at (source.first);
        if (chunk.complete && chunk.checked == completion_generation)
          frequency+= source.second;
      }
      if (frequency > 0) matches.emplace_back (it->first, frequency);
    }
  if (get_preference ("text autocompletion sorting", "alphabetical") == "frequency")
    std::stable_sort (matches.begin (), matches.end (),
      [] (const auto& a, const auto& b) { return a.second > b.second; });
  array<string> result;
  for (const auto& word: matches) {
    std::string suffix= word.first.substr (key.size ());
    result << string (suffix.data (), suffix.size ());
  }
  return result;
}

static string
completion_word_prefix (string s, int end) {
  std::string_view text (s.data (), N(s));
  for (const auto& span: athena::text::word_segments (text))
    if (span.lexical && span.end == static_cast<std::size_t> (end))
      return s (span.begin, span.end);
  return "";
}

/******************************************************************************
* Completion mode
******************************************************************************/

static bool
reference_completion_context (editor_rep& ed) {
  return ed.inside (LABEL) || ed.inside (REFERENCE) || ed.inside (PAGEREF) ||
    ed.inside (as_tree_label ("eqref")) || ed.inside (as_tree_label ("smart-ref"));
}

bool
edit_interface_rep::complete_try () {
  if (hybrid_name_context ()) return complete_hybrid ();
  tree st= subtree (et, path_up (tp));
  if (is_compound (st)) return false;
  string s= st->label, ss;
  int end= last_item (tp);
  array<string> a;
  if (reference_completion_context (*this)) {
    if (end != N(s)) return false;
    ss= copy (s);
    tree t= get_labels ();
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (is_atomic (t[i]) && starts (t[i]->label, s))
        a << string (t[i]->label (N(s), N(t[i]->label)));
  }
  else {
    ss= completion_word_prefix (s, end);
    if (ss == "") return false;
    return complete_text (ss, false);
  }
  if (N(a) == 0) return false;
  complete_start (ss, a);
  return true;
}

bool
edit_interface_rep::complete_try_realtime () {
  if (hybrid_name_context ()) return complete_hybrid ();
  if (get_preference ("realtime text autocompletion", "on") != "on" ||
      inside (HYBRID))
    return false;

  tree st= subtree (et, path_up (tp));
  if (is_compound (st)) return false;
  string s= st->label;
  int end= last_item (tp);
  if (end == 0 || end > N(s)) return false;
  string prefix= completion_word_prefix (s, end);
  std::string_view text (prefix.data (), N(prefix));
  if (prefix == "" || athena::text::next_scalar (text, 0) == text.size ())
    return false;
  return complete_text (prefix, true);
}

bool
edit_interface_rep::complete_text (string prefix, bool realtime) {
  const bool reference= reference_completion_context (*this);
  if (reference) {
    const tree text= subtree (et, path_up (tp));
    if (last_item (tp) != N(text->label)) return false;
    prefix= text->label;
  }
  completion_requested= true;
  pending_text_completion= copy (tp);
  pending_text_original= copy (subtree (et, path_up (tp))->label);
  pending_text_prefix= prefix;
  pending_text_realtime= realtime;
  const auto context= editor_rep::cursor_context_at (tp);
  if (!context) return !realtime;
  if (realtime && context->effective_value (MODE) != "text") {
    pending_text_completion= path ();
    return false;
  }
  array<string> choices;
  if (reference) {
    const tree labels= get_labels ();
    for (int i=0; i<N(labels); ++i)
      if (is_atomic (labels[i]) && starts (labels[i]->label, prefix))
        choices << labels[i]->label (N(prefix), N(labels[i]->label));
  }
  else choices= indexed_completions (prefix);
  if (N(choices) == 0) {
    // A manual Tab awaiting the index must not report a failed completion.
    const auto layout= get_typesetter ();
    return !realtime && (!completion_index_valid || !layout->execution_base ||
      completion_layout != layout->execution_base || completion_review_remaining > 0 ||
      !completion_dirty_chunks.empty () || completion_waiting);
  }
  complete_start (prefix, choices);
  return true;
}

void
edit_interface_rep::complete_text_after_layout () {
  update_completion_index ();
  if (is_nil (pending_text_completion)) return;
  const tree text= subtree (et, path_up (tp));
  if (tp != pending_text_completion || !is_atomic (text) ||
      text->label != pending_text_original || input_mode != INPUT_NORMAL ||
      (pending_text_realtime && get_preference ("realtime text autocompletion", "on") != "on")) {
    pending_text_completion= path ();
    return;
  }
  (void) complete_text (pending_text_prefix, pending_text_realtime);
}

bool
edit_interface_rep::complete_hybrid () {
  pending_hybrid_completion= path ();
  tree text= subtree (et, path_up (tp));
  if (!is_atomic (text) || last_item (tp) != N(text->label) || text == "") return false;
  path input= search_upwards (HYBRID);
  if (is_func (subtree (et, path_up (input)), INACTIVE)) input= path_up (input);
  const auto context= editor_rep::cursor_context_at (input * 0);
  if (!context) {
    pending_hybrid_completion= copy (tp);
    pending_hybrid_prefix= copy (text->label);
    return false;
  }
  string prefix= text->label;
  std::map<std::string, string> catalog;
  const auto contracts= context->contract_context ().snapshot ();
  const auto add_signature= [&] (const std::string& key,
      const athena::contracts::definition_signature& parameters) {
    if (parameters.kind == athena::contracts::definition_kind::value) {
      catalog.erase (key);
      return;
    }
    string name (key.data (), key.size ()), signature= name;
    if (parameters.kind == athena::contracts::definition_kind::macro) {
      for (const auto& parameter: parameters.parameters)
        signature= signature * "  [" * string (parameter.data (), parameter.size ()) * "]";
    }
    else signature= signature * "  [...]";
    catalog[key]= signature;
  };
  for (auto label: contracts.definition_labels ()) {
    const auto binding= contracts.definition (label);
    if (!binding) continue;
    add_signature (binding->name, binding->value->signature);
  }
  if (source_authoring (*context))
    for (const auto& entry: source_signatures) add_signature (entry.first, entry.second);
  tree commands= native_latex_command_catalog ();
  for (int i=0; i<N(commands); ++i) {
    string name= commands[i][0]->label;
    catalog.emplace (std::string (name.data (), N(name)), name * "  " * commands[i][1]->label);
  }
  struct candidate { string name, label; int rank; };
  std::vector<candidate> matches;
  for (const auto& entry: catalog) {
    string name (entry.first.data (), entry.first.size ());
    array<fuzzy_rank_field> fields;
    fields << fuzzy_rank_field (name, 1);
    auto score= fuzzy_rank (prefix, fields);
    if (!score.matched) continue;
    int rank= name == prefix? 2000000: starts (name, prefix)? 1000000: 0;
    matches.push_back ({name, entry.second, rank + score.score});
  }
  std::stable_sort (matches.begin (), matches.end (),
    [] (const candidate& a, const candidate& b) { return a.rank > b.rank; });
  array<string> choices;
  hybrid_completion_labels= array<string> ();
  for (std::size_t i=0; i<matches.size () && i<40; ++i) {
    choices << matches[i].name;
    hybrid_completion_labels << matches[i].label;
  }
  if (N(choices) == 0) return false;
  complete_start (prefix, choices);
  return true;
}

void
edit_interface_rep::complete_hybrid_after_layout () {
  if (is_nil (pending_hybrid_completion)) return;
  const path requested= pending_hybrid_completion;
  pending_hybrid_completion= path ();
  if (tp != requested || !hybrid_name_context ()) return;
  const tree text= subtree (et, path_up (tp));
  if (is_atomic (text) && text->label == pending_hybrid_prefix)
    (void) complete_hybrid ();
}

void
edit_interface_rep::complete_start (string prefix, array<string> compls) {
  // check consistency
  tree st= subtree (et, path_up (tp));
  if (is_compound (st)) return;
  string s= st->label;
  int end= last_item (tp);
  if ((end<N(prefix)) || (s (end-N(prefix), end) != prefix)) return;

  if (N(compls) == 0) return;
  bool hybrid= hybrid_name_context ();
  QJsonArray choices;
  for (int i=0; i<N(compls); ++i) {
    string utf8= hybrid? hybrid_completion_labels[i]: prefix * compls[i];
    athena::text::require_utf8 (
      std::string_view (utf8.data (), static_cast<std::size_t> (N(utf8))));
    choices.append (QString::fromUtf8 (as_charp (utf8), N(utf8)));
  }
  QByteArray encoded= QJsonDocument (choices).toJson (QJsonDocument::Compact);
  set_input_normal ();
  completion_prefix= copy (prefix);
  completions= copy (compls);
  completion_pos= 0;
  completion_cursor= copy (tp);
  completion_original= copy (s);
  completion_hybrid= hybrid;
  ++completion_session;
  set_input_mode (INPUT_COMPLETE);
  cursor cu= get_cursor ();
  if (!publish_ui_text (actor_command_kind::ui_show_completion,
        string (encoded.constData (), encoded.size ()), completion_session,
        static_cast<std::uint64_t> ((SI) (cu->ox * magf)),
        static_cast<std::uint64_t> ((SI) ((cu->oy + cu->y1) * magf)), hybrid))
    set_input_normal ();
}

void
edit_interface_rep::complete_choose (std::uint64_t session, int index, bool activate) {
  if (input_mode != INPUT_COMPLETE || session != completion_session) return;
  bool valid= index >= 0 && index < N(completions) && tp == completion_cursor;
  if (valid) {
    tree st= subtree (et, path_up (tp));
    valid= is_atomic (st) && st->label == completion_original;
  }
  string suffix= valid ? copy (completions[index]) : string ("");
  bool hybrid= completion_hybrid;
  set_input_normal ();
  if (!valid) return;
  start_editing ();
  try {
    archive_state ();
    if (hybrid) {
      choose_hybrid (suffix, activate);
    }
    else if (ends (suffix, "()")) insert_tree (suffix, path (N(suffix)-1));
    else insert_tree (suffix);
    end_editing ();
  }
  catch (...) {
    cancel_editing ();
    throw;
  }
}

bool
edit_interface_rep::complete_keypress (string key) {
  if (input_mode != INPUT_COMPLETE || N(completions) == 0) return false;
  string accept= get_preference ("text autocompletion accept key", "both");
  if (completion_hybrid) accept= "both";
  if (accept != "enter" && accept != "tab" && accept != "both") accept= "both";
  bool accept_tab= key == "tab" && (accept == "tab" || accept == "both");
  bool accept_enter= (key == "return" || key == "enter") &&
                     (accept == "enter" || accept == "both");
  if (accept_tab || accept_enter) {
    complete_choose (completion_session, completion_pos, accept_enter);
    return true;
  }
  if (key == "up" || key == "down" || key == "S-tab") {
    int delta= key == "down" ? 1 : -1;
    completion_pos= (completion_pos + delta + N(completions)) % N(completions);
    (void) publish_ui (actor_command_kind::ui_select_completion,
                      completion_session, completion_pos);
    return true;
  }
  set_input_normal ();
  return key == "escape";
}

/******************************************************************************
* Tab completion inside sessions
******************************************************************************/
