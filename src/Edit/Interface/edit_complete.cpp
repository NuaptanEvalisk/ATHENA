
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

/******************************************************************************
* Finding completions in text
******************************************************************************/

static void
find_completions (
  drd_info drd, tree t, std::map<std::string, int>& words)
{
  if (is_atomic (t)) {
    string s= t->label;
    std::string_view text (s.data (), N(s));
    for (const auto& span: athena::text::word_segments (text))
      if (span.lexical)
        ++words[std::string (text.substr (span.begin, span.end - span.begin))];
  }
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (drd->is_accessible_child (t, i))
        find_completions (drd, t[i], words);
  }
}

void
edit_interface_rep::source_indexes_changed (path p, bool text_only) {
  path relative= p / rp;
  if (is_nil (relative)) completion_index_valid= false;
  else if (completion_index_valid) completion_dirty_chunks.insert (relative->item);

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

array<string>
edit_interface_rep::indexed_completions (string prefix) {
  tree body= subtree (et, rp);
  if (!completion_index_valid) {
    completion_chunks.clear ();
    completion_words.clear ();
    completion_dirty_chunks.clear ();
    for (int i=0; i<N(body); ++i) completion_dirty_chunks.insert (i);
    completion_index_valid= true;
  }
  for (int i: completion_dirty_chunks) {
    auto& chunk= completion_chunks[i];
    for (const auto& word: chunk) {
      auto found= completion_words.find (word.first);
      found->second-= word.second;
      if (found->second == 0) completion_words.erase (found);
    }
    chunk.clear ();
    if (i < N(body) && drd->is_accessible_child (body, i))
      find_completions (drd, body[i], chunk);
    for (const auto& word: chunk) completion_words[word.first]+= word.second;
  }
  completion_dirty_chunks.clear ();
  std::string key (prefix.data (), N(prefix));
  std::vector<std::pair<std::string, int>> matches;
  for (auto it= completion_words.lower_bound (key);
       it != completion_words.end () && it->first.compare (0, key.size (), key) == 0;
       ++it)
    if (it->first != key) matches.push_back (*it);
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

bool
edit_interface_rep::complete_try () {
  if (hybrid_name_context ()) return complete_hybrid ();
  tree st= subtree (et, path_up (tp));
  if (is_compound (st)) return false;
  string s= st->label, ss;
  int end= last_item (tp);
  array<string> a;
  if (inside (LABEL) || inside (REFERENCE) || inside (PAGEREF) ||
      inside (as_tree_label ("eqref")) ||
      inside (as_tree_label ("smart-ref"))) {
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
    a= indexed_completions (ss);
  }
  if (N(a) == 0) return false;
  complete_start (ss, a);
  return true;
}

bool
edit_interface_rep::complete_try_realtime () {
  if (hybrid_name_context ()) return complete_hybrid ();
  if (get_preference ("realtime text autocompletion", "on") != "on" ||
      get_init_string (MODE) != "text" || inside (HYBRID))
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
  return complete_try ();
}

bool
edit_interface_rep::complete_hybrid () {
  tree text= subtree (et, path_up (tp));
  if (!is_atomic (text) || last_item (tp) != N(text->label) || text == "") return false;
  string prefix= text->label;
  std::map<std::string, string> catalog;
  tree environment= get_full_env ();
  for (int i=0; i<N(environment); ++i) {
    tree entry= environment[i];
    if (N(entry) != 2 || !is_atomic (entry[0]) ||
        !(is_func (entry[1], MACRO) || is_func (entry[1], XMACRO))) continue;
    string name= entry[0]->label, signature= name;
    if (is_func (entry[1], MACRO)) {
      for (int j=0; j<N(entry[1])-1; ++j)
        if (is_atomic (entry[1][j])) signature= signature * "  [" * entry[1][j]->label * "]";
    }
    else signature= signature * "  [...]";
    catalog[std::string (name.data (), N(name))]= signature;
  }
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
      path name= path_up (tp);
      assign (name, suffix);
      go_to (end (et, name));
      if (activate) activate_hybrid (false);
      else (void) prepare_hybrid ();
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
