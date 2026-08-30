
/******************************************************************************
* MODULE     : gui_text.cpp
* DESCRIPTION: Direct English text formatting for the user interface
* COPYRIGHT  : (C) 2026  The ATHENA developers
*******************************************************************************
*/

#include "gui_text.hpp"
#include "analyze.hpp"
#include "basic.hpp"

string
ui_text (const string& s) {
  int pos= search_forwards ("::", s);
  return pos < 0 ? s : s (0, pos);
}

string
ui_text (const char* s) {
  return ui_text (string (s));
}

static string
ui_replace_text (const tree& t) {
  if (N(t) == 0) return "";
  string result= ui_text (t[0]);
  for (int i=1; i<N(t); i++)
    result= replace (result, "%" * as_string (i), ui_text (t[i]));
  return result;
}

string
ui_text (const tree& t) {
  if (is_atomic (t)) return ui_text (t->label);
  if (is_compound (t, "replace")) return ui_replace_text (t);
  if (is_concat (t) || is_document (t)) {
    string result;
    for (int i=0; i<N(t); i++) {
      string part= ui_text (t[i]);
      if (i > 0 && is_compound (t[i], "render-key") &&
          !ends (result, " "))
        result << (use_macos_fonts () || gui_is_qt () ? "  " : " ");
      result << part;
    }
    return result;
  }
  if (is_compound (t, "verbatim", 1) ||
      is_compound (t, "localize", 1) ||
      is_compound (t, "render-key", 1) ||
      is_compound (t, "math", 1))
    return ui_text (t[0]);
  if (is_func (t, WITH) && N(t) > 0) return ui_text (t[N(t)-1]);
  if (is_compound (t, "op", 1)) {
    string op= ui_text (t[0]);
    if (op == "<leftarrow>") return "Left";
    if (op == "<rightarrow>") return "Right";
    if (op == "<uparrow>") return "Up";
    if (op == "<downarrow>") return "Down";
    return op;
  }

  string result;
  for (int i=0; i<N(t); i++) result << ui_text (t[i]);
  return result;
}
