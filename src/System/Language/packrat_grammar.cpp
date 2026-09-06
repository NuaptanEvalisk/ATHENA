
/******************************************************************************
* MODULE     : packrat_grammar.cpp
* DESCRIPTION: packrat grammars
* COPYRIGHT  : (C) 2010  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "packrat_grammar.hpp"
#include "analyze.hpp"
#include "iterator.hpp"
#include "convert.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

thread_local tree                 packrat_uninit (UNINIT);
thread_local int                  packrat_nr_tokens= 256;
thread_local int                  packrat_nr_symbols= 0;
thread_local hashmap<string,C>    packrat_tokens;
thread_local hashmap<tree,C>      packrat_symbols;
thread_local hashmap<C,tree>      packrat_decode (packrat_uninit);

namespace {
enum class grammar_edit_kind { define, property, inherit };
struct grammar_edit {
  grammar_edit_kind kind;
  std::string language, symbol, value, property;
};
struct grammar_definitions {
  std::recursive_mutex mutex;
  std::vector<grammar_edit> edits;
  std::atomic<size_t> revision{0};
};
grammar_definitions& definitions () {
  static grammar_definitions value;
  return value;
}
struct local_grammars {
  hashmap<string,packrat_grammar> values;
  size_t revision= 0;
  ~local_grammars () {
    iterator<string> it= iterate (values);
    while (it->busy ()) tm_delete (values[it->next ()].rep);
  }
};
local_grammars& grammars () {
  static thread_local local_grammars value;
  return value;
}
std::string native_string (const string& s) {
  return std::string (s.data (), N(s));
}
string local_string (const std::string& s) {
  return string (s.data (), static_cast<int> (s.size ()));
}
packrat_grammar local_grammar (string name) {
  auto& values= grammars ().values;
  if (!values->contains (name))
    values(name)= packrat_grammar (tm_new<packrat_grammar_rep> (name));
  return values[name];
}
void inherit_grammar (packrat_grammar gr, packrat_grammar from) {
  iterator<C> it= iterate (from->grammar);
  while (it->busy ()) {
    C sym= it->next ();
    gr->grammar(sym)= from->grammar[sym];
    gr->productions(sym)= from->productions[sym];
  }
  iterator<D> props= iterate (from->properties);
  while (props->busy ()) {
    D key= props->next ();
    gr->properties(key)= from->properties[key];
  }
}
// Registration is cold. Replay value-only edits once per owner/revision;
// never share native trees, hash buckets or token IDs between owners.
void synchronize_grammars () {
  auto& local= grammars ();
  const auto& edits= definitions ().edits;
  while (local.revision < edits.size ()) {
    const auto& edit= edits[local.revision];
    packrat_grammar gr= local_grammar (local_string (edit.language));
    if (edit.kind == grammar_edit_kind::define)
      gr->define (local_string (edit.symbol),
                  scheme_to_tree (local_string (edit.value)));
    else if (edit.kind == grammar_edit_kind::property)
      gr->set_property (local_string (edit.symbol),
                        local_string (edit.property), local_string (edit.value));
    else inherit_grammar (gr, local_grammar (local_string (edit.value)));
    ++local.revision;
  }
}
void publish_edit (grammar_edit edit) {
  auto& shared= definitions ();
  shared.edits.push_back (std::move (edit));
  grammars ().revision= shared.edits.size ();
  shared.revision.store (shared.edits.size (), std::memory_order_release);
}
}

size_t packrat_grammar_revision () { return grammars ().revision; }

/******************************************************************************
* Encoding and decoding of tokens and symbols
******************************************************************************/

C
new_token (string s) {
  if (N(s) == 1)
    return (C) (unsigned char) s[0];
  else {
    C ret= packrat_nr_tokens++;
    //cout << "Encode " << s << " -> " << ret << LF;
    return ret;
  }
}

C
encode_token (string s) {
  if (!packrat_tokens->contains (s)) {
    int pos= 0;
    tm_char_forwards (s, pos);
    if (pos == 0 || pos != N(s)) return -1;
    C sym= new_token (s);
    packrat_tokens (s)= sym;
    packrat_decode (sym)= s;
  }
  return packrat_tokens[s];
}

array<C>
encode_tokens (string s) {
  int pos= 0;
  array<C> ret;
  while (pos < N(s)) {
    int old= pos;
    tm_char_forwards (s, pos);
    ret << encode_token (s (old, pos));
  }
  return ret;
}

C
new_symbol (tree t) {
  if (is_atomic (t)) {
    C ret= encode_token (t->label);
    if (ret != -1) return ret;
  }
  C ret= (packrat_nr_symbols++) + PACKRAT_SYMBOLS;
  //cout << "Encode " << t << " -> " << ret << LF;
  return ret;
}

C
encode_symbol (tree t) {
  if (!packrat_symbols->contains (t)) {
    C sym= new_symbol (t);
    packrat_symbols (t)= sym;
    packrat_decode  (sym)= t;
  }
  return packrat_symbols[t];
}

/******************************************************************************
* Left recursion
******************************************************************************/

bool
left_recursive (string s, tree t) {
  if (is_atomic (t))
    return false;
  else if (is_compound (t, "symbol", 1) && is_atomic (t[0]))
    return t[0]->label == s;
  else if (is_compound (t, "or")) {
    for (int i=0; i<N(t); i++)
      if (left_recursive (s, t[i]))
        return true;
    return false;
  }
  else if (is_compound (t, "concat"))
    return N(t) > 0 && left_recursive (s, t[0]);
  else return false;
}

tree
left_head (string s, tree t) {
  if (is_atomic (t)) return t;
  else if (is_compound (t, "symbol", 1) && is_atomic (t[0])) {
    if (t[0]->label == s) return compound ("or");
    else return t;
  }
  else if (is_compound (t, "or")) {
    tree r= compound ("or");
    for (int i=0; i<N(t); i++) {
      tree h= left_head (s, t[i]);
      if (is_compound (h, "or")) r << A(h);
      else r << h;
    }
    return r;
  }
  else if (is_compound (t, "concat")) {
    if (N(t) == 0 || !left_recursive (s, t)) return t;
    else return left_head (s, t[0]);
  }
  else return t;
}

tree
left_tail (string s, tree t) {
  if (is_atomic (t)) return t;
  else if (is_compound (t, "symbol", 1) && is_atomic (t[0])) {
    if (t[0]->label == s) return compound ("concat");
    else return compound ("or");
  }
  else if (is_compound (t, "or")) {
    tree r= compound ("or");
    for (int i=0; i<N(t); i++) {
      tree u= left_tail (s, t[i]);
      if (is_compound (u, "or")) r << A(u);
      else r << u;
    }
    return r;
  }
  else if (is_compound (t, "concat")) {
    if (N(t) == 0 || !left_recursive (s, t)) return compound ("or");
    else {
      tree r= compound ("concat");
      tree u= left_tail (s, t[0]);
      if (u == compound ("or")) return u;
      else if (is_compound (u, "concat")) r << A(u);
      else r << u;
      r << A (t (1, N(t)));
      return r;
    }
  }
  else return compound ("or");
}

/******************************************************************************
* Constructor
******************************************************************************/

array<C>
singleton (C c) {
  array<C> ret;
  ret << c;
  return ret;
}

packrat_grammar_rep::packrat_grammar_rep (string s):
  lan_name (s),
  grammar (singleton (PACKRAT_TM_FAIL)),
  productions (packrat_uninit)
{
  grammar (PACKRAT_TM_OPEN)= singleton (PACKRAT_TM_OPEN);
  grammar (PACKRAT_TM_ANY )= singleton (PACKRAT_TM_ANY );
  grammar (PACKRAT_TM_ARGS)= singleton (PACKRAT_TM_ARGS);
  grammar (PACKRAT_TM_LEAF)= singleton (PACKRAT_TM_LEAF);
  grammar (PACKRAT_TM_FAIL)= singleton (PACKRAT_TM_FAIL);
}

packrat_grammar
find_packrat_grammar (string s) {
  auto& local= grammars ();
  auto& shared= definitions ();
  if (local.revision == shared.revision.load (std::memory_order_acquire) &&
      local.values->contains (s)) return local.values[s];
  std::lock_guard<std::recursive_mutex> guard (shared.mutex);
  synchronize_grammars ();
  if (!local.values->contains (s)) {
    eval ("(lazy-language-force " * s * ")");
    synchronize_grammars ();
  }
  return local_grammar (s);
}

/******************************************************************************
* Accelerate big lists of consecutive symbols
******************************************************************************/

void
packrat_grammar_rep::accelerate (array<C>& def) {
  if (N(def) == 0 || def[0] != PACKRAT_OR) return;
  hashmap<C,bool> all;
  for (int i=1; i<N(def); i++)
    if (def[i] >= PACKRAT_OR) return;
    else all (def[i])= true;
  array<C> ret;
  ret << PACKRAT_OR;
  hashmap<C,bool> done;
  for (int i=1; i<N(def); i++) {
    C c= def[i];
    if (done->contains (c)) continue;
    C start= c;
    while (start > 0 && all->contains (start-1)) start--;
    C end= c;
    while (end+1 < PACKRAT_OR && all->contains (end+1)) end++;
    if (end == start) ret << c;
    else {
      tree t= compound ("range", packrat_decode[start], packrat_decode[end]);
      C sym= encode_symbol (t);
      array<C> rdef;
      rdef << PACKRAT_RANGE << start << end;
      grammar (sym)= rdef;
      ret << sym;
      //cout << "Made range " << packrat_decode[start]
      //<< "--" << packrat_decode[end] << "\n";
    }
    for (int j=start; j<=end; j++) done(j)= true;
  }
  if (N(ret) == 2) {
    ret= range (ret, 1, 2);
    if (ret[0] >= PACKRAT_SYMBOLS) ret= grammar[ret[0]];
  }
  //cout << "Was: " << def << "\n";
  //cout << "Is : " << ret << "\n";
  def= ret;
}

/******************************************************************************
* Definition of grammars
******************************************************************************/

array<C>
packrat_grammar_rep::define (tree t) {
  //cout << "Define " << t << INDENT << LF;
  array<C> def;
  if (t == "")
    def << PACKRAT_CONCAT;
  else if (is_atomic (t)) {
    int pos= 0;
    string s= t->label;
    tm_char_forwards (s, pos);
    if (pos > 0 && pos == N(s)) def << encode_symbol (s);
    else def << PACKRAT_CONCAT << encode_tokens (s);
  }
  else if (is_compound (t, "symbol", 1))
    def << encode_symbol (t);
  else if (is_compound (t, "range", 2) &&
           is_atomic (t[0]) && is_atomic (t[1]) &&
           N(t[0]->label) == 1 && N(t[1]->label) == 1)
    def << PACKRAT_RANGE
        << encode_token (t[0]->label)
        << encode_token (t[1]->label);
  else if (is_compound (t, "or", 1))
    def << define (t[0]);
  else {
    if (is_compound (t, "or")) def << PACKRAT_OR;
    else if (is_compound (t, "concat")) def << PACKRAT_CONCAT;
    else if (is_compound (t, "while")) def << PACKRAT_WHILE;
    else if (is_compound (t, "repeat")) def << PACKRAT_REPEAT;
    else if (is_compound (t, "not")) def << PACKRAT_NOT;
    else if (is_compound (t, "except")) def << PACKRAT_EXCEPT;
    else if (is_compound (t, "tm-open")) def << PACKRAT_TM_OPEN;
    else if (is_compound (t, "tm-any")) def << PACKRAT_TM_ANY;
    else if (is_compound (t, "tm-args")) def << PACKRAT_TM_ARGS;
    else if (is_compound (t, "tm-leaf")) def << PACKRAT_TM_LEAF;
    else if (is_compound (t, "tm-char")) def << PACKRAT_TM_CHAR;
    else if (is_compound (t, "tm-cursor")) def << PACKRAT_TM_CURSOR;
    else def << PACKRAT_TM_FAIL;
    for (int i=0; i<N(t); i++) {
      (void) define (t[i]);
      def << encode_symbol (t[i]);
    }
    accelerate (def);
  }
  if (N (def) != 1 || def[0] != encode_symbol (t)) {
    C sym= encode_symbol (t);
    grammar (sym)= def;
  }
  //cout << UNINDENT << "Defined " << t << " -> " << def << LF;
  return def;
}

void
packrat_grammar_rep::define (string s, tree t) {
  //cout << "Define " << s << " := " << t << "\n";
  if (left_recursive (s, t)) {
    string s1= s * "-head";
    string s2= s * "-tail";
    tree   t1= left_head (s, t);
    tree   t2= left_tail (s, t);
    define (s1, t1);
    define (s2, t2);
    tree   u1= compound ("symbol", s1);
    tree   u2= compound ("while", compound ("symbol", s2));
    define (s, compound ("concat", u1, u2));
  }
  else {
    C        sym= encode_symbol (compound ("symbol", s));
    array<C> def= define (t);
    grammar (sym)= def;
  }
}

void
packrat_grammar_rep::set_property (string s, string var, string val) {
  //cout << "Set property " << s << ", " << var << " -> " << val << "\n";
  C sym = encode_symbol (compound ("symbol", s));
  C prop= encode_symbol (compound ("property", var));
  D key = (((D) prop) << 32) + ((D) (sym ^ prop));
  properties (key)= val;
}

/******************************************************************************
* Member analysis
******************************************************************************/

string
packrat_grammar_rep::decode_as_string (C sym) {
  string r;
  if (sym < PACKRAT_OR) {
    tree t= packrat_decode [sym];
    if (is_atomic (t)) r << t->label;
  }
  else {
    array<C> def= grammar[sym];
    if (N(def) == 1 && (def[0] < PACKRAT_OR || def[0] >= PACKRAT_SYMBOLS))
      r << decode_as_string (def[0]);
    else if (N(def) >= 1 && def[0] == PACKRAT_CONCAT)
      for (int i=1; i<N(def); i++)
        r << decode_as_string (def[i]);
    else {
      cout << "Warning: could not transform " << packrat_decode[sym]
           << " into a string\n";
    }
  }
  return r;
}

array<string>
packrat_grammar_rep::decode_as_array_string (C sym) {
  array<string> r;
  array<C> def= grammar[sym];
  if (N(def) == 1 && def[0] >= PACKRAT_SYMBOLS)
    r << decode_as_array_string (def[0]);
  else if (N(def) >= 1 && def[0] == PACKRAT_OR)
    for (int i=1; i<N(def); i++)
      r << decode_as_array_string (def[i]);
  else if (N(def) == 3 && def[0] == PACKRAT_RANGE)
    for (C c=def[1]; c<=def[2]; c++)
      r << decode_as_string (c);
  else r << decode_as_string (sym);
  return r;
}

array<string>
packrat_grammar_rep::members (string s) {
  C sym= encode_symbol (compound ("symbol", s));
  //cout << s << " -> " << decode_as_array_string (sym) << "\n";
  return decode_as_array_string (sym);
}

/******************************************************************************
* Interface
******************************************************************************/

void
packrat_define (string lan, string s, tree t) {
  std::lock_guard<std::recursive_mutex> guard (definitions ().mutex);
  synchronize_grammars ();
  packrat_grammar gr= find_packrat_grammar (lan);
  std::string source= native_string (tree_to_scheme (t));
  gr->define (s, t);
  publish_edit ({grammar_edit_kind::define, native_string (lan),
                native_string (s), std::move (source), {}});
}

void
packrat_property (string lan, string s, string var, string val) {
  std::lock_guard<std::recursive_mutex> guard (definitions ().mutex);
  synchronize_grammars ();
  packrat_grammar gr= find_packrat_grammar (lan);
  gr->set_property (s, var, val);
  publish_edit ({grammar_edit_kind::property, native_string (lan),
                native_string (s), native_string (val), native_string (var)});
}

void
packrat_inherit (string lan, string from) {
  std::lock_guard<std::recursive_mutex> guard (definitions ().mutex);
  synchronize_grammars ();
  packrat_grammar gr = find_packrat_grammar (lan);
  packrat_grammar inh= find_packrat_grammar (from);
  inherit_grammar (gr, inh);
  publish_edit ({grammar_edit_kind::inherit, native_string (lan), {},
                native_string (from), {}});
}

int
packrat_abbreviation (string lan, string s) {
  static thread_local int nr= 1;
  static thread_local hashmap<string,int> abbrs (-1);
  static thread_local size_t revision= 0;
  packrat_grammar gr= find_packrat_grammar (lan);
  if (revision != packrat_grammar_revision ()) {
    abbrs= hashmap<string,int> (-1);
    revision= packrat_grammar_revision ();
  }
  string key= lan * ":" * s;
  int r= abbrs[key];
  if (r >= 0) return r;
  C sym= encode_symbol (compound ("symbol", s));
  if (gr->grammar->contains (sym)) r= nr++;
  else r= 0;
  abbrs (key)= r;
  return r;
}
