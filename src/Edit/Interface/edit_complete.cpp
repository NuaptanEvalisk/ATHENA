
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
#include "hashset.hpp"
#include "analyze.hpp"
#include "wencoding.hpp"
#include <QJsonArray>
#include <QJsonDocument>

/******************************************************************************
* Finding completions in text
******************************************************************************/

static void
find_completions (
  drd_info drd, tree t, hashset<string>& h, string prefix= "")
{
  if (is_atomic (t)) {
    string s= t->label;
    int i= 0, n= N(s);
    while (i<n) {
      if (is_iso_alpha (s[i])) {
        int start= i;
        while ((i<n) && (is_iso_alpha (s[i]))) i++;
        string r= s (start, i);
        if (starts (r, prefix) && (r != prefix))
          h->insert (r (N(prefix), N(r)));
      }
      else skip_symbol (s, i);
    }
  }
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (drd->is_accessible_child (t, i))
        find_completions (drd, t[i], h, prefix);
  }
}

static array<string>
find_completions (drd_info drd, tree t, string prefix= "") {
  hashset<string> h;
  find_completions (drd, t, h, prefix);
  return as_completions (h);
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
  if (key == "tab" || key == "return" || key == "enter") {
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
