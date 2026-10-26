
/******************************************************************************
* MODULE     : edit_math.cpp
* DESCRIPTION: modify mathematical structures
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_math.hpp"
#include "analyze.hpp"
#include "utf8_edit.hpp"
#include "language.hpp"
#include "math_tree_v3.hpp"
#include "Data/NodeContracts/native_fields.hpp"
#include "tree_analyze.hpp"
#include "converter.hpp"
#include "node_metadata.hpp"
#include <array>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

/******************************************************************************
* Constructors and destructors
******************************************************************************/

edit_math_rep::edit_math_rep () {}
edit_math_rep::~edit_math_rep () {}

namespace {

bool
math_token_letter_or_digit (const string& text, int begin, int end,
                            bool& letter, bool& digit) {
  letter= digit= false;
  if (begin < 0 || end <= begin || end > N(text)) return false;
  const char* bytes= text.data ();
  int32_t i= begin;
  UChar32 cp= U_SENTINEL;
  U8_NEXT (bytes, i, end, cp);
  if (cp < 0) return false;
  letter= u_isalpha (cp);
  digit= u_isdigit (cp);
  return true;
}

int
math_previous_token_start (const string& text, int end) {
  if (end <= 0) return 0;
  int start= utf8_grapheme_previous (text, end);
  bool letter= false, digit= false;
  (void) math_token_letter_or_digit (text, start, end, letter, digit);
  while (start > 0 && (letter || digit)) {
    int previous= utf8_grapheme_previous (text, start);
    bool previous_letter= false, previous_digit= false;
    (void) math_token_letter_or_digit (
      text, previous, start, previous_letter, previous_digit);
    if ((letter && !previous_letter) || (digit && !previous_digit)) break;
    start= previous;
  }
  return start;
}

bool
is_script_or_around (const tree& value) {
  return is_func (value, RSUB) || is_func (value, RSUP) ||
         is_func (value, AROUND) || is_func (value, make_tree_label ("around*"));
}

bool
math_symbol_or_operator (const string& text) {
  if (N(text) == 0) return false;
  if (math_symbol_type (tree (text)) == "symbol") return true;
  // The old math-operator? predicate treats a multi-letter identifier as one
  // unit; byte length is intentional because the source remains UTF-8 bytes.
  if (utf8_grapheme_count (text) < 2) return false;
  for (int at=0; at<N(text); ) {
    int next= utf8_grapheme_next (text, at);
    bool letter= false, digit= false;
    if (!math_token_letter_or_digit (text, at, next, letter, digit) || !letter)
      return false;
    at= next;
  }
  return true;
}

void
collect_math_labels (const tree& value, std::vector<tree>& labels) {
  if (is_func (value, LABEL)) { labels.push_back (copy (value)); return; }
  if (is_atomic (value)) return;
  for (int i=0; i<N(value); ++i) collect_math_labels (value[i], labels);
}

tree
math_concat (const std::vector<tree>& items) {
  if (items.empty ()) return tree ("");
  tree result (CONCAT);
  for (const tree& item: items) {
    if (item == "") continue;
    if (is_atomic (item) && N(result) > 0 && is_atomic (result[N(result)-1]))
      result[N(result)-1]= result[N(result)-1]->label * item->label;
    else if (is_concat (item))
      for (int j=0; j<N(item); ++j) result << copy (item[j]);
    else result << copy (item);
  }
  if (N(result) == 0) return tree ("");
  if (N(result) == 1) return result[0];
  return result;
}

void
decompose_math_concat (const tree& value, std::vector<tree>& out) {
  if (is_func (value, LABEL) || is_func (value, make_tree_label ("eq-number")))
    return;
  if (is_atomic (value)) {
    array<string> graphemes= utf8_graphemes (value->label);
    for (int i=0; i<N(graphemes); ++i) out.push_back (tree (graphemes[i]));
    return;
  }
  if (is_concat (value)) {
    for (int i=0; i<N(value); ++i) decompose_math_concat (value[i], out);
    return;
  }
  out.push_back (copy (value));
}

bool
is_binary_math_relation (const tree& value) {
  const string group= math_symbol_group (value);
  return group == "Relation-nolim-symbol" || group == "Assign-symbol";
}

path
innermost_label_path (tree root, path cursor, tree_label label) {
  path p= cursor;
  if (!is_nil (p)) p= path_up (p);
  while (true) {
    if (has_subtree (root, p) && L(subtree (root, p)) == label) return p;
    if (is_nil (p)) break;
    p= path_up (p);
  }
  return path ();
}

void
collect_eqnarray_rows (const tree& value, std::vector<std::vector<tree>>& rows) {
  if (is_func (value, ROW)) {
    std::vector<tree> cells;
    for (int i=0; i<N(value); ++i)
      if (is_func (value[i], CELL) && N(value[i]) > 0)
        cells.push_back (copy (value[i][0]));
    if (!cells.empty ()) rows.push_back (std::move (cells));
    return;
  }
  if (is_atomic (value)) return;
  for (int i=0; i<N(value); ++i) collect_eqnarray_rows (value[i], rows);
}

bool
convertible_eqnarray_rows (const std::vector<std::vector<tree>>& rows) {
  bool previous_empty= true;
  for (const auto& cells: rows) {
    if (cells.empty ()) continue;
    const bool first_empty= cells.front () == "";
    if (!previous_empty && !first_empty) return false;
    previous_empty= first_empty;
  }
  return true;
}

bool
is_around_tree (const tree& value) {
  return is_func (value, AROUND, 3) ||
         is_func (value, make_tree_label ("around*"), 3);
}

bool
missing_bracket (const tree& value) {
  if (is_atomic (value)) return value == "." || value == "<nobracket>";
  return (is_func (value, LEFT, 1) || is_func (value, RIGHT, 1)) &&
         value[0] == ".";
}

bool
find_innermost_around (tree root, path cursor, path& result) {
  path p= cursor;
  if (!is_nil (p)) p= path_up (p);
  while (true) {
    if (has_subtree (root, p) && is_around_tree (subtree (root, p))) {
      result= p;
      return true;
    }
    if (is_nil (p)) break;
    p= path_up (p);
  }
  return false;
}

bool
find_adjacent_around (tree root, path cursor, path& result) {
  if (find_innermost_around (root, cursor, result)) return true;
  if (is_nil (cursor)) return false;
  path parent_path= path_up (cursor);
  if (!has_subtree (root, parent_path)) return false;
  tree parent= subtree (root, parent_path);
  if (is_atomic (parent)) return false;
  int i= last_item (cursor);
  if (i > 0 && i <= N(parent) && is_around_tree (parent[i-1])) {
    result= parent_path * (i-1);
    return true;
  }
  if (i >= 0 && i < N(parent) && is_around_tree (parent[i])) {
    result= parent_path * i;
    return true;
  }
  return false;
}

bool
native_v3_math_context (const buffer_document_state* buffer) {
  return buffer != nullptr && buffer->storage_version ==
    athena::document::xml_storage_version::v3;
}

bool
find_native_delimited (tree root, path cursor, path& result) {
  path p= cursor;
  while (true) {
    if (has_subtree (root, p) && is_func (subtree (root, p), DELIMITED, 6)) {
      result= p;
      return true;
    }
    if (is_nil (p)) break;
    p= path_up (p);
  }
  if (is_nil (cursor)) return false;
  const path parent_path= path_up (cursor);
  if (!has_subtree (root, parent_path)) return false;
  const tree parent= subtree (root, parent_path);
  if (is_atomic (parent)) return false;
  const int i= last_item (cursor);
  for (int index: {i-1, i})
    if (index >= 0 && index < N (parent) &&
        is_func (parent[index], DELIMITED, 6)) {
      result= parent_path * index;
      return true;
    }
  return false;
}

bool
find_native_scripts (tree root, path cursor, path& result) {
  path p= cursor;
  while (true) {
    if (has_subtree (root, p) &&
        is_func (subtree (root, p), MATH_SCRIPTS, 5)) {
      result= p;
      return true;
    }
    if (is_nil (p)) return false;
    p= path_up (p);
  }
}

bool
native_prime_run (tree t) {
  if (is_concat (t)) {
    for (int i=0; i<N(t); ++i)
      if (!native_prime_run (t[i])) return false;
    return N(t) > 0;
  }
  if (!is_atomic (t) || t == "") return false;
  int32_t offset= 0;
  while (offset < N(t->label)) {
    UChar32 cp= U_SENTINEL;
    U8_NEXT (t->label.data (), offset, N(t->label), cp);
    if (cp != '\'' && cp != '`' &&
        !(cp >= 0x2032 && cp <= 0x2037) && cp != 0x2057)
      return false;
  }
  return true;
}

bool
continue_native_primes (tree root, path cursor, int slot) {
  path scripts;
  return find_native_scripts (root, cursor, scripts) &&
    cursor == end (root, scripts * slot) &&
    native_prime_run (subtree (root, scripts * slot));
}

bool
native_script_base (tree root, path cursor, path& result, int& offset) {
  if (is_nil (cursor)) return false;
  result= path_up (cursor);
  if (!has_subtree (root, result)) return false;
  tree value= subtree (root, result);
  offset= last_item (cursor);
  if (is_atomic (value) && offset > 0) return true;
  if (is_compound (value) && cursor == end (root, result))
    return L(value) != MATH_BLOCK && L(value) != MATH_GRID &&
           L(value) != DOCUMENT && L(value) != ROW && L(value) != CELL;
  // A cursor in the empty text immediately after an object still belongs to
  // that object. Do not cross a content slot or an identity-bearing leaf.
  if (!is_atomic (value) || offset != 0 || is_nil (result)) return false;
  const path parent= path_up (result);
  if (!is_concat (subtree (root, parent))) return false;
  int index= last_item (result);
  while (index > 0) {
    value= subtree (root, parent * (--index));
    if (value == "" && athena::node::get (value) == nullptr) continue;
    result= parent * index;
    offset= is_atomic (value) ? N(value->label) : 1;
    return true;
  }
  return false;
}

tree
empty_native_scripts (int slot, tree value= tree ("")) {
  tree fields[5]= {"", "", "", "", ""};
  fields[slot]= value;
  return athena::math_v3::scripts (
    fields[0], fields[1], fields[2], fields[3], fields[4]);
}

bool
scriptable_math_token (const string& text) {
  if (N (text) == 0) return false;
  int32_t offset= 0;
  UChar32 cp= U_SENTINEL;
  U8_NEXT (text.data (), offset, N (text), cp);
  return cp >= 0 && (u_isalpha (cp) || u_isdigit (cp));
}

void
append_native_math_tokens (tree& result, const string& value) {
  if (value == "") return;
  // The v3 contract specifies one lexical math token per leaf, not a run of
  // text which will only be tokenized by the typesetter at render time.
  const array<tree> tokens= concat_tokenize (tree (value));
  for (int i=0; i<N (tokens); ++i)
    result << tokens[i];
}

string
native_delimiter_glyph (string glyph) {
  return glyph == "." ? string ("") : glyph;
}

bool
native_math_accent_glyph (string input, string& result) {
  // Keyboard actions may name a canonical accent by its input-command name.
  // Translate it once when constructing native mathematics: v3 stores only
  // the Unicode accent glyph and never a second symbolic spelling.
  static const std::pair<const char*,const char*> names[]= {
    {"^", "̂"}, {"<hat>", "̂"}, {"~", "̃"}, {"<tilde>", "̃"},
    {"<bar>", "̅"}, {"<wide-bar>", "̅"},
    {"<vect>", "⃗"}, {"<rightarrow>", "⃗"},
    {"<wide-varrightarrow>", "⃗"},
    {"<leftarrow>", "⃖"}, {"<wide-varleftarrow>", "⃖"},
    {"<breve>", "̆"}, {"<check>", "̌"},
    {"<acute>", "́"}, {"<grave>", "̀"},
    {"<dot>", "̇"}, {"<ddot>", "̈"},
    {"<dddot>", "⃛"}, {"<abovering>", "̊"},
    {"<overbrace>", "⏞"}, {"<underbrace>", "⏟"},
    {"<bind>", "͡"}
  };
  for (const auto& [key, glyph]: names)
    if (input == key) { result= glyph; return true; }
  if (athena::math_v3::glyph (input)) { result= input; return true; }
  return false;
}

bool
unpaired_bracket_glyph (string glyph) {
  return glyph == "(" || glyph == ")" || glyph == "[" || glyph == "]" ||
         glyph == "{" || glyph == "}" || glyph == "⟨" || glyph == "⟩" ||
         glyph == "⟪" || glyph == "⟫" || glyph == "⟦" || glyph == "⟧" ||
         glyph == "⌊" || glyph == "⌋" || glyph == "⌈" || glyph == "⌉";
}

} // namespace

/******************************************************************************
* Making mathematical objects
******************************************************************************/

bool
edit_math_rep::native_math_editing () const {
  return native_v3_math_context (buf);
}

void
edit_math_rep::make_native_big_operator (string op) {
  if (!native_math_editing ()) return;
  // Closing a historical operator scope has no meaning for a native atom.
  if (op == "." || op == "bignone") return;
  string glyph= op;
  bool upright= false, display_limits= false;
  if (!athena::math_v3::glyph (glyph)) {
    string name= op;
    if (N(name) > 2 && name[0] == '<' && name[N(name)-1] == '>')
      name= name (1, N(name)-1);
    // Decode the keyboard command with the shipped operator mappings, not
    // a migration of existing source or an intermediate BIG tree.
    glyph= strict_cork_to_utf8 ("<big-" * name * "-1>");
    if (!athena::math_v3::glyph (glyph)) {
      set_message ("Error: unsupported native big operator", op);
      return;
    }
    upright= starts (name, "up");
    display_limits= ends (name, "lim");
  }
  tree atom= athena::math_v3::big_operator (glyph, upright);
  if (display_limits)
    atom= athena::math_v3::scripts (atom, "", "", "", "", 'd');
  if (selection_active_normal ()) {
    const tree selected= selection_get_cut ();
    insert_tree (tree (CONCAT, atom, selected), path (1, end (selected)));
  }
  else insert_tree (atom, path (1));
}

void
edit_math_rep::make_rigid () {
  if (selection_active_small ())
    insert_tree (tree (RIGID, selection_get_cut ()));
  else {
    insert_tree (tree (RIGID, ""), path (0, 0));
    set_status_hint ("move to the right when finished", "group");
  }
}

void
edit_math_rep::make_lprime (string s) {
  if (native_v3_math_context (buf)) {
    if (selection_active_small () || !continue_native_primes (et, tp, 2))
      make_script (true, false, false);
    insert_tree (s == "'" ? tree ("′") : tree (s));
    return;
  }
  tree& st= subtree (et, path_up (tp));
  if (is_func (st, LPRIME, 1) && (last_item (tp) == 1)) {
    if (is_atomic (st[0]))
      insert (path_up (tp) * path (0, N (st[0]->label)), s);
  }
  else insert_tree (tree (LPRIME, s));
}

void
edit_math_rep::make_rprime (string s) {
  if (native_v3_math_context (buf)) {
    // Consecutive primes share a superscript; ordinary powers still nest.
    if (selection_active_small () || !continue_native_primes (et, tp, 4))
      make_script (true, true, false);
    insert_tree (s == "'" ? tree ("′") : tree (s));
    return;
  }
  tree& st= subtree (et, path_up (tp));
  if (is_func (st, RPRIME, 1) && (last_item (tp) == 1)) {
    if (is_atomic (st[0]))
      insert (path_up (tp) * path (0, N (st[0]->label)), s);
  }
  else insert_tree (tree (RPRIME, s));
}

void
edit_math_rep::make_below () {
  if (native_v3_math_context (buf)) {
    const tree body= selection_active_small () ? selection_get_cut () : tree ("");
    insert_tree (tree (MATH_ANNOTATION, body, tree (""), tree ("")),
                 body == "" ? path (0, 0) : path (1, 0));
    return;
  }
  if (selection_active_small ()) {
    insert_tree (tree (BELOW, selection_get_cut (), ""), path (1, 0));
    set_status_hint ("type script, move right", "under");
  }
  else {
    insert_tree (tree (BELOW, "", ""), path (0, 0));
    set_status_hint ("type body, move down, type script", "under");
  }
}

void
edit_math_rep::make_above () {
  if (native_v3_math_context (buf)) {
    const tree body= selection_active_small () ? selection_get_cut () : tree ("");
    insert_tree (tree (MATH_ANNOTATION, body, tree (""), tree ("")),
                 body == "" ? path (0, 0) : path (2, 0));
    return;
  }
  if (selection_active_small ()) {
    insert_tree (tree (ABOVE, selection_get_cut (), ""), path (1, 0));
    set_status_hint ("type script, move right", "above");
  }
  else {
    insert_tree (tree (ABOVE, "", ""), path (0, 0));
    set_status_hint ("type body, move up, type script", "above");
  }
}

void
edit_math_rep::make_script (bool sup, bool right) {
  make_script (sup, right, true);
}

void
edit_math_rep::make_script (bool sup, bool right, bool switch_slots) {
  if (native_v3_math_context (buf)) {
    const int slot= athena::contracts::native_field_index (MATH_SCRIPTS,
      right ? (sup ? "right-superscript" : "right-subscript") :
              (sup ? "left-superscript" : "left-subscript"));
    const bool selected= selection_active_small ();
    const tree content= selected ? selection_get_cut () : tree ("");
    path attached;
    if (switch_slots && find_native_scripts (et, tp, attached)) {
      const tree scripts= subtree (et, attached);
      const path relative= tp / attached;
      // Inside a script, attach to the local token rather than switching to
      // an outer slot. Repeating a command in its empty slot stays there.
      const bool at_slot_end= N(relative) > 1 &&
        tp == end (et, attached * relative->item);
      if (N(relative) == 1 ||
          (at_slot_end &&
           (relative->item == 0 ||
            (relative->item == slot && scripts[slot] == "")))) {
        go_to_end (attached * slot);
        if (selected) insert_tree (content);
        return;
      }
    }
    path atomic_path;
    int offset= 0;
    if (native_script_base (et, tp, atomic_path, offset)) {
      if (has_subtree (et, atomic_path)) {
        const tree old_atom= subtree (et, atomic_path);
        if (is_compound (old_atom)) {
          if (is_func (old_atom, MATH_SCRIPTS, 5)) {
            go_to_end (atomic_path * slot);
            if (selected) insert_tree (content);
          }
          else {
            tree scripts= empty_native_scripts (slot, content);
            scripts[0]= copy (old_atom);
            assign (atomic_path, scripts);
            go_to_end (atomic_path * slot);
          }
          return;
        }
        if (is_atomic (old_atom)) {
          const string original= old_atom->label;
          if (offset > 0 && offset <= N (original)) {
            const int begin= math_previous_token_start (original, offset);
            const string lexical_base= original (begin, offset);
            if (scriptable_math_token (lexical_base)) {
              const string prefix= original (0, begin);
              const string suffix= original (offset, N (original));
              tree base (lexical_base);
              if (prefix == "" && suffix == "")
                athena::node::copy_metadata (old_atom, base);
              tree script= athena::math_v3::scripts (
                base, tree (""), tree (""), tree (""), tree (""));
              // Fill the requested script slot. Copy the policy metadata
              // from the canonical constructor; never edit fixed arity.
              script[slot]= content;
              tree replacement;
              path destination;
              if (prefix == "" && suffix == "") {
                replacement= script;
                destination= atomic_path * slot * end (content);
              }
              else {
                tree sequence (CONCAT);
                append_native_math_tokens (sequence, prefix);
                const int index= N (sequence);
                sequence << script;
                append_native_math_tokens (sequence, suffix);
                athena::node::copy_metadata (old_atom, sequence);
                replacement= sequence;
                destination= atomic_path * index * slot * end (content);
              }
              assign (atomic_path, replacement);
              go_to (destination);
              return;
            }
          }
        }
      }
    }
    // A deliberately independent ^{op} or _i has a legitimate empty base.
    // Do not attach it to unrelated paragraph text or manufacture RSUB/RSUP.
    insert_tree (empty_native_scripts (slot, content), path (slot, end (content)));
    return;
  }
  tree_label s (sup? SUP (right): SUB (right));
  if (selection_active_small ())
    insert_tree (tree (s, selection_get_cut ()));
  else {
    path   p= path_up (tp);
    tree   t= subtree (et, p);
    bool   flag;

    if (is_format (p))
      FAILED ("bad cursor position");
    if (t == "" && is_script (subtree (et, path_up (p))))
      return;
    if (is_script (t, flag) && (flag==right) && (L(t)==s)) {
      go_to_end (p * 0);
      return;
    }
    insert_tree (tree (s, ""), path (0, 0));
    set_status_hint (
      "move to the right when finished",
      (char*) (sup? (right? "superscript": "left superscript"):
                    (right? "subscript": "left subscript")));
  }
}

void
edit_math_rep::make_fraction () {
  using athena::contracts::native_field_index;
  if (selection_active_small ()) {
    insert_tree (tree (FRAC, selection_get_cut (), ""),
                 path (native_field_index (FRAC, "denominator"), 0));
    set_status_hint ("type denominator, move right", "fraction");
  }
  else {
    insert_tree (tree (FRAC, "", ""),
                 path (native_field_index (FRAC, "numerator"), 0));
    set_status_hint ("type numerator, move down, type denominator", "fraction");
  }
}

void
edit_math_rep::make_sqrt () {
  if (selection_active_small ())
    insert_tree (tree (SQRT, selection_get_cut ()));
  else {
    insert_tree (tree (SQRT, ""),
                 path (athena::contracts::native_field_index (SQRT, "radicand"), 0));
    set_status_hint ("move to the right when finished", "square root");
  }
}

void
edit_math_rep::make_var_sqrt () {
  using athena::contracts::native_field_index;
  if (selection_active_small ()) {
    tree t= selection_get_cut ();
    path index (native_field_index (SQRT, "index"), 0);
    if (is_func (t, SQRT, 1))
      insert_tree (tree (SQRT, t[native_field_index (SQRT, "radicand")], ""), index);
    else insert_tree (tree (SQRT, t, ""), index);
  }
  else {
    insert_tree (tree (SQRT, "", ""), path (native_field_index (SQRT, "radicand"), 0));
    set_status_hint (concat (kbd ("left"), ": set n",
                            kbd ("right"), ": when finished"),
                     "n-th root");
  }
}

void
edit_math_rep::make_wide (string wide, bool stretch) {
  if (native_v3_math_context (buf)) {
    string glyph;
    if (!native_math_accent_glyph (wide, glyph)) {
      set_message ("Error: unknown mathematical accent", wide);
      return;
    }
    const tree body= selection_active_small () ? selection_get_cut () : tree ("");
    tree accent (MATH_ACCENT, tree (glyph), tree ("o"), body);
    athena::node::metadata meta;
    meta.properties["math:accent-stretch"]= athena::node::property (stretch);
    athena::node::set (accent, meta);
    insert_tree (accent, body == "" ? path (2, 0) : path (2, end (body)));
    return;
  }
  tree accent= stretch ? tree (WITH, "math-accent-stretch", "true", wide) : tree (wide);
  if (selection_active_small ())
    insert_tree (tree (WIDE, selection_get_cut (), accent));
  else {
    insert_tree (tree (WIDE, "", accent), path (0, 0));
    set_status_hint ("move to the right when finished", "wide accent");
  }
}

void
edit_math_rep::make_wide_under (string wide, bool stretch) {
  if (native_v3_math_context (buf)) {
    string glyph;
    if (!native_math_accent_glyph (wide, glyph)) {
      set_message ("Error: unknown mathematical accent", wide);
      return;
    }
    const tree body= selection_active_small () ? selection_get_cut () : tree ("");
    tree accent (MATH_ACCENT, tree (glyph), tree ("u"), body);
    athena::node::metadata meta;
    meta.properties["math:accent-stretch"]= athena::node::property (stretch);
    athena::node::set (accent, meta);
    insert_tree (accent, body == "" ? path (2, 0) : path (2, end (body)));
    return;
  }
  tree accent= stretch ? tree (WITH, "math-accent-stretch", "true", wide) : tree (wide);
  if (selection_active_small ())
    insert_tree (tree (VAR_WIDE, selection_get_cut (), accent));
  else {
    insert_tree (tree (VAR_WIDE, "", accent), path (0, 0));
    set_status_hint ("move to the right when finished", "wide under accent");
  }
}

void
edit_math_rep::make_neg () {
  if (selection_active_small ())
    insert_tree (tree (NEG, selection_get_cut ()));
  else {
    insert_tree (tree (NEG, ""), path (0, 0));
    set_status_hint ("move to the right when finished", "negation");
  }
}

/******************************************************************************
* Deleting mathematical objects
******************************************************************************/

static bool
is_deleted (tree t) {
  return t == "<nobracket>" || t == tree (LEFT, ".") || t == tree (RIGHT, ".");
}

void
edit_math_rep::back_around (tree t, path p, bool forward) {
  if (is_func (t, DELIMITED, 6)) {
    go_to_border (p * 5, forward);
    return;
  }
  bool match= (get_preference ("automatic brackets") != "off");
  if (is_func (t, BIG_AROUND)) {
    if (match || forward)
      go_to_border (p * 1, forward);
    else {
      remove_node (t, 1);
      correct (path_up (p));
    }
  }
  else {
    int i= (forward? 0: 2);
    if (is_deleted (t[i]));
    else if (is_atomic (t[i]))
      assign (t[i], ".");
    else if (is_func (t[i], LEFT))
      assign (t[i], tree (LEFT, "."));
    else if (is_func (t[i], RIGHT))
      assign (t[i], tree (RIGHT, "."));
    go_to_border (p * 1, forward);
    if (is_deleted (t[0]) && is_deleted (t[2])) {
      remove_node (t, 1);
      correct (path_up (p));
    }
  }
  if (!match) call ("brackets-refresh");
}

void
edit_math_rep::back_in_around (tree t, path p, bool forward) {
  if (is_func (t, DELIMITED, 6)) {
    if (is_empty (t[5])) {
      assign (path_up (p), "");
      correct (path_up (p, 2));
    }
    else go_to_border (path_up (p), !forward);
    return;
  }
  bool match= (get_preference ("automatic brackets") != "off");
  if (is_empty (t[1]) && match) {
    assign (t, "");
    correct (path_up (p, 2));
  }
  else if (is_func (t, BIG_AROUND)) {
    if (match || forward)
      go_to_border (path_up (p), !forward);
    else {
      remove_node (t, 1);
      correct (path_up (p, 2));
    }
  }
  else {
    int i= (forward? 2: 0);
    if (is_deleted (t[i]));
    else if (is_atomic (t[i]))
      assign (t[i], ".");
    else if (is_func (t[i], LEFT))
      assign (t[i], tree (LEFT, "."));
    else if (is_func (t[i], RIGHT))
      assign (t[i], tree (RIGHT, "."));
    go_to_border (path_up (p), !forward);
    if (is_deleted (t[0]) && is_deleted (t[2])) {
      remove_node (t, 1);
      correct (path_up (p, 2));
    }
  }
  if (!match) call ("brackets-refresh");  
}

void
edit_math_rep::back_in_long_arrow (tree t, path p, bool forward) {
  if (is_func (t, MATH_LONG_ARROW, 3)) {
    // Native XML v3 long arrows always own all three physical children:
    // glyph, lower annotation, upper annotation. The legacy LONG_ARROW may
    // remove an empty upper child, which corrupts the fixed native arity.
    // Keep both empty annotation slots and navigate to the neighboring slot
    // or out of the arrow, preserving any identity on the native node.
    const path owner= path_up (p);
    const int index= last_item (p);
    if (index == 2 && !forward) go_to_border (owner * 1, false);
    else if (index == 1 && forward) go_to_border (owner * 2, true);
    else go_to_border (owner, !forward);
    return;
  }
  int i= last_item (p);
  if (i == 2) {
    if (is_empty (t[2])) remove (path_up (p) * 2, 1);
    if (forward) go_to_border (path_up (p), !forward);
    else go_to_border (path_up (p) * 1, forward);
  }
  else if (i == 1) {
    if (N(t) == 2 && is_empty (t[1])) {
      assign (path_up (p), "");
      correct (path_up (p, 2));
    }
    else if (forward && N(t) >= 3)
      go_to_border (path_up (p) * 2, forward);
    else go_to_border (path_up (p), !forward);
  }
  else go_to_border (path_up (p), !forward);
}

void
edit_math_rep::back_prime (tree t, path p, bool forward) {
  if ((N(t) == 1) && is_atomic (t[0])) {
    string s= t[0]->label;
    if (forward) {
      int i= 0, n= N(s);
      i= utf8_grapheme_next (s, i);
      if (i >= n) {
        assign (p, "");
        correct (path_up (p));
      }
      else remove (p * path (0, 0), i);
    }
    else {
      int n= N(s), i= n;
      i= utf8_grapheme_previous (s, i);
      if (i <= 0) {
        assign (p, "");
        correct (path_up (p));
      }
      else remove (p * path (0, i), n-i);
    }
  }
}

void
edit_math_rep::back_in_wide (tree t, path p, bool forward) {
  int i= last_item (p);
  const int body= L(t) == MATH_ACCENT ? 2 : 0;
  if ((i == body) && is_empty (t[body])) {
    assign (path_up (p), "");
    correct (path_up (p, 2));
  }
  else go_to_border (path_up (p), !forward);
}

void
edit_math_rep::pre_remove_around (path p) {
  tree st= subtree (et, p);
  // Native scripts own their base; none are dangling endpoint siblings.
  if (is_func (st, DELIMITED, 6)) return;
  if (is_script (st[1]) || is_prime (st[1])) assign (st[1], "");
  else if (is_concat (st[1]) && N(st[1]) > 0) {
    int li= 0, ri= N(st[1])-1;
    while (li<N(st[1])) {
      tree sst= st[1][li];
      if (!is_func (sst, RSUB) &&
          !is_func (sst, RSUP) &&
          !is_func (sst, RPRIME))
        break;
      li++;
    }
    while (ri >= 0) {
      tree sst= st[1][ri];
      if (!is_func (sst, LSUB) &&
          !is_func (sst, LSUP) &&
          !is_func (sst, LPRIME))
        break;
      ri--;
    }
    if (ri != N(st[1])-1) remove (p * path (1, ri+1), N(st[1])-1-ri);
    if (li != 0) remove (p * path (1, 0), li);
    correct (p * 1);
  }
}

/******************************************************************************
* Trees
******************************************************************************/

void
edit_math_rep::make_tree () {
  if (selection_active_small ())
    insert_tree (tree (TREE, selection_get_cut (), ""), path (1, 0));
  else {
    insert_tree (tree (TREE, "", ""), path (0, 0));
    set_message (concat (kbd_shortcut ("(structured-insert-right)"),
                         ": insert a new branch"),
                 "tree");
  }
}

bool
edit_math_rep::math_select_before_cursor_unit () {
  if (is_nil (tp)) return false;
  const int i= last_item (tp);
  const path current_path= path_up (tp);
  tree current= subtree (et, current_path);
  if (is_atomic (current)) {
    if (i <= 0) { selection_cancel (); return false; }
    const int end_byte= min (i, N(current->label));
    const int begin_byte= math_previous_token_start (current->label, end_byte);
    if (begin_byte >= end_byte) { selection_cancel (); return false; }
    selection_set_paths (current_path * begin_byte, current_path * end_byte);
    return selection_active_any ();
  }
  if (i <= 0) { selection_cancel (); return false; }

  if (is_script_or_around (current)) {
    if (is_nil (current_path)) { selection_cancel (); return false; }
    path parent_path= path_up (current_path);
    int child= last_item (current_path);
    tree parent= subtree (et, parent_path);
    while (child > 0 && child < N(parent) &&
           is_script_or_around (parent[child])) --child;
    if (child < 0 || child >= N(parent)) { selection_cancel (); return false; }
    tree previous= parent[child];
    path begin_path;
    if (!is_atomic (previous) || previous == "") begin_path= start (et, parent_path * child);
    else {
      const string text= previous->label;
      const int begin_byte= math_previous_token_start (text, N(text));
      bool absorb_around= false;
      if (child + 1 < N(parent) && is_script_or_around (parent[child+1])) {
        string tail= text (begin_byte, N(text));
        absorb_around= !math_symbol_or_operator (tail);
      }
      begin_path= absorb_around ? start (et, parent_path * (child + 1)) :
                                  parent_path * child * begin_byte;
    }
    selection_set_paths (begin_path, tp);
    return selection_active_any ();
  }

  selection_set_paths (current_path * 0, tp);
  return selection_active_any ();
}

void
edit_math_rep::math_make_above () {
  if (selection_active_small ()) { make_above (); return; }
  const bool native= native_v3_math_context (buf);
  tree body= "";
  if (math_select_before_cursor_unit ()) body= selection_get_cut ();
  if (native) {
    insert_tree (tree (MATH_ANNOTATION, body, tree (""), tree ("")), path (2, 0));
    return;
  }
  insert_tree (tree (ABOVE, body, ""), path (1, 0));
}

void
edit_math_rep::math_make_below () {
  if (selection_active_small ()) { make_below (); return; }
  const bool native= native_v3_math_context (buf);
  tree body= "";
  if (math_select_before_cursor_unit ()) body= selection_get_cut ();
  if (native) {
    insert_tree (tree (MATH_ANNOTATION, body, tree (""), tree ("")), path (1, 0));
    return;
  }
  insert_tree (tree (BELOW, body, ""), path (1, 0));
}

void
edit_math_rep::math_kbd_select_enlarge () {
  if (selection_active_any ()) { select_enlarge (); return; }
  if (is_nil (tp)) { select_enlarge (); return; }
  const int offset= last_item (tp);
  const path node_path= path_up (tp);
  tree node= subtree (et, node_path);
  if (is_atomic (node)) {
    const string text= node->label;
    const int end_byte= max (0, min (offset, N(text)));
    if (end_byte > 0) {
      int begin_byte= utf8_grapheme_previous (text, end_byte);
      if (text (begin_byte, end_byte) == ">") {
        int scan= begin_byte;
        while (scan > 0) {
          int previous= utf8_grapheme_previous (text, scan);
          if (text (previous, scan) == "<") { begin_byte= previous; break; }
          scan= previous;
        }
      }
      else begin_byte= math_previous_token_start (text, end_byte);
      if (begin_byte < end_byte) {
        selection_set_paths (node_path * begin_byte, node_path * end_byte);
        return;
      }
    }
  }
  else if (offset > 0 && offset <= N(node)) {
    path child= node_path * (offset - 1);
    selection_set_paths (start (et, child), end (et, child));
    return;
  }
  select_enlarge ();
}

void
edit_math_rep::math_evaluation_bar () {
  if (selection_active_any () && !selection_active_small ()) return;
  const bool native= native_v3_math_context (buf);
  if (!selection_active_any ()) {
    if (is_nil (tp)) return;
    path row_path= path_up (tp);
    if (!is_nil (row_path)) {
      path parent= path_up (row_path);
      if (!is_nil (parent) && is_concat (subtree (et, parent))) row_path= parent;
    }
    selection_set_paths (start (et, row_path), tp);
  }
  tree body= selection_active_any () ? selection_get_cut () : tree ("");
  if (native) {
    tree evaluation= athena::math_v3::delimited ("", "|", 'a', body);
    insert_tree (evaluation, body == "" ? path (5, 0) : path (1));
    return;
  }
  tree evaluation (make_tree_label ("around*"), ".", body, "|");
  insert_tree (evaluation, body == "" ? path (1, 0) : path (1));
}


void
edit_math_rep::math_separator (string separator, int large_mode) {
  if (native_v3_math_context (buf)) {
    (void) large_mode;
    if (unpaired_bracket_glyph (separator)) {
      set_message ("Error: mathematical brackets must be paired", separator);
      return;
    }
    if ((separator == "|" || separator == "‖" || separator == "∣" ||
         separator == "⫴") && athena::math_v3::glyph (separator))
      insert_tree (tree (MATH_MIDDLE, tree (separator)));
    else insert_tree (tree (separator)); // slash is a mathematical token
    return;
  }
  const bool large= large_mode < 0 ? get_preference ("use large brackets") != "off" :
                                    large_mode != 0;
  if (large) insert_tree (tree (MID, separator));
  else insert_tree (tree (separator));
}

void
edit_math_rep::math_bracket_open (string left, string right, int large_mode) {
  if (native_v3_math_context (buf)) {
    (void) large_mode; // v3 always inserts a complete automatic delimiter.
    left= native_delimiter_glyph (left);
    right= native_delimiter_glyph (right);
    if ((!athena::math_v3::glyph (left, true)) ||
        (!athena::math_v3::glyph (right, true)) ||
        (left == "" && right == "")) {
      set_message ("Error: invalid paired delimiter", left);
      return;
    }
    path enclosed;
    if (!selection_active_any () && left == right &&
        find_native_delimited (et, tp, enclosed)) {
      const tree pair= subtree (et, enclosed);
      if (pair[1] == right && tp == end (et, enclosed * 5)) {
        go_to_end (enclosed);
        return;
      }
    }
    const tree body= selection_active_normal () ? selection_get_cut () : tree ("");
    tree pair= athena::math_v3::delimited (left, right, 'a', body);
    insert_tree (pair, body == "" ? path (5, 0) : path (5, end (body)));
    return;
  }
  const bool large= large_mode < 0 ? get_preference ("use large brackets") != "off" :
                                    large_mode != 0;
  const bool automatic= get_preference ("automatic brackets") != "off";
  if (!automatic) {
    if (large) insert_tree (tree (LEFT, left));
    else insert_tree (tree (left));
    return;
  }

  const tree_label around_label= make_tree_label (large ? "around*" : "around");
  if (selection_active_normal ()) {
    tree body= selection_get_cut ();
    insert_tree (tree (around_label, left, body, right), path (1, end (body)));
    return;
  }

  path around_path;
  if (find_adjacent_around (et, tp, around_path)) {
    tree around= subtree (et, around_path);
    const path body_path= around_path * 1;
    const bool at_start= tp == start (et, body_path);
    const bool at_end= tp == end (et, body_path) || tp == end (et, around_path);
    if (at_end && missing_bracket (around[2]) &&
        (!missing_bracket (around[0]) || tp == end (et, around_path))) {
      assign (around_path * 2, tree (left));
      go_to_end (around_path);
      return;
    }
    if (at_start && missing_bracket (around[0])) {
      assign (around_path * 0, tree (left));
      go_to_start (body_path);
      return;
    }
    if (at_end && left == right && around[2] == right) {
      go_to_end (around_path);
      return;
    }
    if (at_end && right == "|" && around[0] == "⟨") {
      assign (around_path * 2, tree (right));
      go_to_end (around_path);
      return;
    }
  }

  insert_tree (tree (around_label, left, "", right), path (1, 0));
}

void
edit_math_rep::math_bracket_close (string right, string left, int large_mode) {
  if (native_v3_math_context (buf)) {
    (void) left;
    (void) large_mode;
    const string glyph= native_delimiter_glyph (right);
    path enclosed;
    path search= tp;
    while (find_native_delimited (et, search, enclosed)) {
      const tree expression= subtree (et, enclosed);
      const path body_path= enclosed * 5;
      // An endpoint already crossed belongs to the completed inner pair,
      // not the next closing keystroke for its enclosing pair.
      if (tp == end (et, enclosed)) {
        if (is_nil (enclosed)) break;
        search= path_up (enclosed);
        continue;
      }
      const bool at_end= tp == end (et, body_path);
      if (at_end && athena::math_v3::glyph (glyph, true) &&
          (glyph != "" || expression[0] != "")) {
        // The inserted mate is a default, not a restriction on intervals.
        if (glyph != expression[1]) assign (enclosed * 1, tree (glyph));
        go_to_end (enclosed);
        return;
      }
      break;
    }
    set_message ("Error: bracket does not match", right);
    return; // Never insert a lone RIGHT endpoint into a v3 source tree.
  }
  const bool large= large_mode < 0 ? get_preference ("use large brackets") != "off" :
                                    large_mode != 0;
  const bool automatic= get_preference ("automatic brackets") != "off";
  if (!automatic) {
    if (large) insert_tree (tree (RIGHT, right));
    else insert_tree (tree (right));
    return;
  }

  path around_path;
  if (find_adjacent_around (et, tp, around_path)) {
    tree around= subtree (et, around_path);
    const path body_path= around_path * 1;
    const bool at_start= tp == start (et, body_path);
    const bool at_end= tp == end (et, body_path) || tp == end (et, around_path);
    if (at_start && missing_bracket (around[0]) &&
        (!missing_bracket (around[2]) || tp != end (et, around_path))) {
      assign (around_path * 0, tree (right));
      go_to_start (body_path);
      return;
    }
    if (at_end && missing_bracket (around[2])) {
      assign (around_path * 2, tree (right));
      go_to_end (around_path);
      return;
    }
    if (at_end) {
      assign (around_path * 2, tree (right));
      go_to_end (around_path);
      return;
    }
  }
  set_message (concat ("Error: bracket does not match"), right);
}

void
edit_math_rep::back_in_tree (tree t, path p, bool forward) {
  int i= last_item (p);
  if (i>0) {
    if ((i>0) && (t[i] == "")) {
      path q= path_up (p);
      if (N (t) == 2) {
        assign (q, t[0]);
        correct (path_up (q));
      }
      else {
        remove (q * i, 1);
        if (forward) {
          if (i == N (subtree (et, q))) go_to_end (q);
          else go_to_start (q * i);
        }
      }
    }
    else if (!forward) go_to_end (path_up (p) * (i-1));
    else if (i == N(t)-1) go_to_end (path_up (p));
    else go_to_start (path_up (p) * (i+1));
  }
  else {
    if (t == tree (TREE, "", "")) {
      p= path_up (p);
      assign (p, "");
      correct (path_up (p));
    }
    else if (forward) go_to_start (path_inc (p));
    else go_to_start (path_up (p));
  }
}
