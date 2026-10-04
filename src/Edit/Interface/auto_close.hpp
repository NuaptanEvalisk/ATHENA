/******************************************************************************
* MODULE     : auto_close.hpp
* DESCRIPTION: Native smart quotes, apostrophes and paired delimiters
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "editor.hpp"
#include "scheme.hpp"

namespace athena::auto_close {

struct quote_style {
  string open;
  string close;
  string open_suffix;
  string close_prefix;
  array<string> open_after;
  bool apostrophe_opens= true;
  bool paired= true;
};

// Pure helpers are exposed for regression tests of byte-position behavior.
quote_style quote_style_for_language (string language);
bool quote_should_close (tree root, path cursor, string language);
bool has_opening_single_quote (tree root, path cursor);

void insert_quote (editor_rep* ed);
void insert_apostrophe (editor_rep* ed, bool alternate);
void make_bracket_open (editor_rep* ed, string left, string right,
                        bool large=false);
void make_separator (editor_rep* ed, string separator, bool large=false);
void make_bracket_close (editor_rep* ed, string right, string left,
                         bool large=false);
bool test_matching_brackets ();
void toggle_matching_brackets ();
void make_big_operator (editor_rep* ed, string op);

// The JSON keymap evaluator asks this before falling back to Scheme.
bool keyboard_call (string name, const array<object>& args, object& result);

} // namespace athena::auto_close

// Stable native glue names for Scheme callers that still use these commands.
void native_insert_quote ();
void native_insert_apostrophe (bool alternate);
void native_make_bracket_open (string left, string right, object options);
void native_make_separator (string separator, object options);
void native_make_bracket_close (string right, string left, object options);
bool native_test_matching_brackets ();
void native_toggle_matching_brackets ();
void native_make_big_operator (string op);
