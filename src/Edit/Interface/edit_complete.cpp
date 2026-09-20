
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
#include "wencoding.hpp"
#include <QJsonArray>
#include <QJsonDocument>

/******************************************************************************
* Finding completions in text
******************************************************************************/

static void
find_completions (
  drd_info drd, tree t, hashset<string>& h, hashmap<string,int>& frequencies,
  string prefix= "")
{
  if (is_atomic (t)) {
    string s= t->label;
    int i= 0, n= N(s);
    while (i<n) {
      if (is_iso_alpha (s[i])) {
        int start= i;
        while ((i<n) && (is_iso_alpha (s[i]))) i++;
        string r= s (start, i);
        if (starts (r, prefix) && (r != prefix)) {
          h->insert (r);
          frequencies (r)= frequencies[r] + 1;
        }
      }
      else skip_symbol (s, i);
    }
  }
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (drd->is_accessible_child (t, i))
        find_completions (drd, t[i], h, frequencies, prefix);
  }
}

static array<string>
find_completions (drd_info drd, tree t, string prefix= "") {
  hashset<string> h;
  hashmap<string,int> frequencies (0);
  find_completions (drd, t, h, frequencies, prefix);
  array<string> words= as_completions (h);
  if (get_preference ("text autocompletion sorting", "alphabetical") ==
      "frequency") {
    // words is initially alphabetical. Stable insertion by descending count
    // therefore keeps alphabetical order for equal-frequency candidates.
    for (int i= 1; i < N(words); ++i) {
      string word= words[i];
      int count= frequencies[word];
      int j= i;
      while (j > 0 && frequencies[words[j - 1]] < count) {
        words[j]= words[j - 1];
        --j;
      }
      words[j]= word;
    }
  }
  for (int i= 0; i < N(words); ++i)
    words[i]= words[i] (N(prefix), N(words[i]));
  return words;
}

/******************************************************************************
* Completion mode
******************************************************************************/

bool
edit_interface_rep::complete_try () {
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
    if ((end==0) || (!is_iso_alpha (s[end-1])) ||
        ((end!=N(s)) && is_iso_alpha (s[end]))) return false;
    int start= end-1;
    while ((start>0) && is_iso_alpha (s[start-1])) start--;
    ss= s (start, end);
    a= find_completions (drd, et, ss);
  }
  if (N(a) == 0) return false;
  complete_start (ss, a);
  return true;
}

bool
edit_interface_rep::complete_try_realtime () {
  if (get_preference ("realtime text autocompletion", "on") != "on" ||
      get_init_string (MODE) != "text" || inside (HYBRID))
    return false;

  tree st= subtree (et, path_up (tp));
  if (is_compound (st)) return false;
  string s= st->label;
  int end= last_item (tp);
  if (end < 2 || end > N(s) || !is_iso_alpha (s[end - 1])) return false;
  int start= end - 1;
  while (start > 0 && is_iso_alpha (s[start - 1])) start--;
  if (end - start < 2) return false;
  return complete_try ();
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
  set_input_normal ();
  completion_prefix= copy (prefix);
  completions= copy (compls);
  completion_pos= 0;
  completion_cursor= copy (tp);
  completion_original= copy (s);
  ++completion_session;
  set_input_mode (INPUT_COMPLETE);
  QJsonArray choices;
  for (int i=0; i<N(completions); ++i) {
    string utf8= cork_to_utf8 (completion_prefix * completions[i]);
    choices.append (QString::fromUtf8 (as_charp (utf8), N(utf8)));
  }
  QByteArray encoded= QJsonDocument (choices).toJson (QJsonDocument::Compact);
  cursor cu= get_cursor ();
  if (!publish_ui_text (actor_command_kind::ui_show_completion,
        string (encoded.constData (), encoded.size ()), completion_session,
        static_cast<std::uint64_t> ((SI) (cu->ox * magf)),
        static_cast<std::uint64_t> ((SI) ((cu->oy + cu->y1) * magf)), 0))
    set_input_normal ();
}

void
edit_interface_rep::complete_choose (std::uint64_t session, int index) {
  if (input_mode != INPUT_COMPLETE || session != completion_session) return;
  bool valid= index >= 0 && index < N(completions) && tp == completion_cursor;
  if (valid) {
    tree st= subtree (et, path_up (tp));
    valid= is_atomic (st) && st->label == completion_original;
  }
  string suffix= valid ? copy (completions[index]) : string ("");
  set_input_normal ();
  if (!valid) return;
  start_editing ();
  try {
    archive_state ();
    if (ends (suffix, "()")) insert_tree (suffix, path (N(suffix)-1));
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
  if (accept != "enter" && accept != "tab" && accept != "both") accept= "both";
  bool accept_tab= key == "tab" && (accept == "tab" || accept == "both");
  bool accept_enter= (key == "return" || key == "enter") &&
                     (accept == "enter" || accept == "both");
  if (accept_tab || accept_enter) {
    complete_choose (completion_session, completion_pos);
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
