
/******************************************************************************
* MODULE     : to_scheme.cpp
* DESCRIPTION: conversion of scheme expressions to TeXmacs trees
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "convert.hpp"
#include "message.hpp"
#include "analyze.hpp"
#include "drd_std.hpp"
#include "path.hpp"
#include "Xml/legacy_document_reader.hpp"
#include <limits>
#include <functional>

/******************************************************************************
* Handling escape characters
******************************************************************************/

string
unslash (string s) {
  int i, n= N(s);
  string r;
  for (i=0; i<n; i++)
    if ((s[i]=='\\') && ((i+1)<n))
      switch (s[++i]) {
      case '0': r << ((char) 0); break;
      case 'n': r << '\n'; break;
      case 'r': r << '\r'; break;
      case 't': r << '\t'; break;
      default: r << s[i];
      }
    else r << s[i];
  return r;
}

/******************************************************************************
* Converting strings to scheme trees
******************************************************************************/

static bool
is_spc (char c) {
  return (c==' ') || (c=='\t') || (c=='\n') || (c=='\r');
}

static void
skip_scheme_trivia (const string& s, int& i) {
  while (i < N (s)) {
    if (is_spc (s[i])) ++i;
    else if (s[i] == ';') while (i < N (s) && s[i] != '\n') ++i;
    else break;
  }
}

namespace {
struct scheme_read_budget {
  athena::document::codec_limits limits;
  std::size_t nodes= 0;
  [[noreturn]] void fail (int position, const char* message) {
    throw athena::document::codec_exception (
      athena::document::codec_error::invalid_structure, message, -1, -1, position);
  }
  void enter (std::size_t depth) {
    if (depth > limits.depth || ++nodes > limits.nodes)
      throw athena::document::codec_exception (
        athena::document::codec_error::resource_limit, "Legacy Scheme tree exceeds parse budget");
  }
};
}

static scheme_tree
string_to_scheme_tree (string s, int& i, scheme_read_budget* budget= nullptr,
                       std::size_t depth= 0) {
  skip_scheme_trivia (s, i);
  if (budget) budget->enter (depth);
  for (; i<N(s); i++)
    switch (s[i]) {

    case ' ':
    case '\t':
    case '\n':
    case '\r':
      break;
      case '(':
      {
        scheme_tree p (TUPLE);
        i++;
        while (true) {
          skip_scheme_trivia (s, i);
          if ((i==N(s)) || (s[i]==')')) break;
          p << string_to_scheme_tree (s, i, budget, depth + 1);
        }
        if (i == N(s) && budget) budget->fail (i, "Unterminated Scheme list");
        if (i<N(s)) i++;
        return p;
      }
        
      case '\'':
        i++;
        if (budget) budget->fail (i - 1, "Reader abbreviations are not native tree data");
        return scheme_tree (TUPLE, "\'", string_to_scheme_tree (s, i));
        
      case '\"':
      { // "
        int start= i++;
        while ((i<N(s)) && (s[i]!='\"')) { // "
          if ((i<N(s)-1) && (s[i]=='\\')) i++;
          i++;
        }
        if (i == N(s) && budget) budget->fail (i, "Unterminated Scheme string");
        if (i<N(s)) i++;
        return scheme_tree (unslash (s (start, i)));
      }
        
      case ';':
        while ((i<N(s)) && (s[i]!='\n')) i++;
        break;
        
      default:
      {
        if (budget && s[i] == ')') budget->fail (i, "Unexpected closing parenthesis");
        int start= i;
        while ((i<N(s)) && (!is_spc(s[i])) && (s[i]!='(') && (s[i]!=')')) {
          if (budget && s[i]=='\\' && i+1 == N(s)) budget->fail (i, "Incomplete Scheme escape");
          if ((i<N(s)-1) && (s[i]=='\\')) i++;
          i++;
        }
        return scheme_tree (unslash (s (start, i)));
      }
    }
  
  if (budget) budget->fail (i, "Expected Scheme tree data");
  return "";
}

namespace athena::document {
tree read_legacy_scheme (std::string_view input, codec_limits limits) {
  init_std_drd ();
  if (input.size () > limits.input_bytes || input.size () > std::numeric_limits<int>::max ())
    throw codec_exception (codec_error::resource_limit, "Legacy Scheme input exceeds size budget");
  string source (input.data (), static_cast<int> (input.size ()));
  if (!starts (source, "(document (TeXmacs ") &&
      !starts (source, "(document (apply \"TeXmacs\" ") &&
      !starts (source, "(document (expand \"TeXmacs\" "))
    throw codec_exception (codec_error::invalid_structure, "Missing legacy Scheme document signature");
  scheme_read_budget budget {limits};
  int pos= 0;
  auto parsed= string_to_scheme_tree (source, pos, &budget);
  while (pos < N (source)) {
    if (is_spc (source[pos])) ++pos;
    else if (source[pos] == ';') while (pos < N (source) && source[pos] != '\n') ++pos;
    else budget.fail (pos, "Trailing Scheme document data");
  }
  // Validate the tree grammar before the historical converter, which otherwise
  // turns malformed lists into printable errput nodes.
  std::function<void (const tree&, bool)> validate= [&] (const tree& node, bool tag) {
    if (is_atomic (node)) {
      if (!tag && !is_quoted (node->label)) budget.fail (0, "Unquoted native tree text");
      if (tag && (is_quoted (node->label) || node->label == "")) budget.fail (0, "Invalid native tree tag");
      return;
    }
    if (N (node) == 0 || !is_atomic (node[0])) budget.fail (0, "Missing native tree tag");
    validate (node[0], true);
    for (int i= 1; i < N (node); ++i) validate (node[i], false);
  };
  validate (parsed, false);
  auto result= scheme_tree_to_tree (parsed);
  if (!is_func (result, DOCUMENT) || N (result) == 0)
    budget.fail (0, "Invalid legacy Scheme document envelope");
  const auto& header= result[0];
  if (!(is_compound (header, "TeXmacs", 1) && is_atomic (header[0])) &&
      !((is_func (header, APPLY, 2) || is_func (header, EXPAND, 2)) &&
        header[0] == "TeXmacs" && is_atomic (header[1])))
    budget.fail (0, "Invalid legacy Scheme version header");
  return result;
}
} // namespace athena::document

scheme_tree
string_to_scheme_tree (string s) {
  int i=0;
  return string_to_scheme_tree (s, i);
}

scheme_tree
block_to_scheme_tree (string s) {
  scheme_tree p (TUPLE);
  int i=0;
  while ((i<N(s)) && (is_spc (s[i]) || s[i]==')')) i++;
  while (i<N(s)) {
    p << string_to_scheme_tree (s, i);
    while ((i<N(s)) && (is_spc (s[i]) || s[i]==')')) i++;
  }
  return p;
}

/******************************************************************************
* Converting scheme trees to trees
******************************************************************************/

tree
scheme_tree_to_tree (scheme_tree t, const hashmap<string,int>& codes,
                     bool flag) {
  if (is_atomic (t)) return scm_unquote (t->label);
  else if ((N(t) == 0) || is_compound (t[0])) {
    convert_error << "Invalid scheme tree " << t << "\n";
    return
      compound ("errput", 
                concat ("The tree was ", as_string (L(t)), ": ", tree (t)));
  }
  else {
    int i, n= N(t);
    tree_label code= (tree_label) codes [t[0]->label];
    if (flag) code= make_tree_label (t[0]->label);
    if (code == UNKNOWN) {
      tree u (EXPAND, n);
      u[0]= copy (t[0]);
      for (i=1; i<n; i++)
        u[i]= scheme_tree_to_tree (t[i], codes, flag);
      return u;
    }
    else {
      tree u (code, n-1);
      for (i=1; i<n; i++)
        u[i-1]= scheme_tree_to_tree (t[i], codes, flag);
      return u;
    }
  }
}

tree
scheme_tree_to_tree (scheme_tree t, string version) {
  version= scm_unquote (version);
  tree doc, error (_ERROR, "bad format or data");
  if (version_inf (version, "2.1.3"))
    std_warning << "ATHENA: Scheme TeXmacs tree version " << version
                << " predates the supported 2.1.3 baseline; interpreting it "
                << "with current tree codes" << LF;
  doc= scheme_tree_to_tree (t);
  if (!is_document (doc)) return error;
  return doc;
}

tree
scheme_tree_to_tree (scheme_tree t) {
  return scheme_tree_to_tree (t, standard_codes_for_thread (), true);
}

/******************************************************************************
* Converting scheme strings to trees
******************************************************************************/

tree
scheme_to_tree (string s) {
  return scheme_tree_to_tree (string_to_scheme_tree (s));
}

tree
scheme_document_to_tree (string s) {
  tree error (_ERROR, "bad format or data");
  if (starts (s, "(document (apply \"TeXmacs\" ") ||
      starts (s, "(document (expand \"TeXmacs\" ") ||
      starts (s, "(document (TeXmacs "))
  {
    int i, begin=27;
    if (starts (s, "(document (expand \"TeXmacs\" ")) begin= 28;
    if (starts (s, "(document (TeXmacs ")) begin= 19;
    for (i=begin; i<N(s); i++)
      if (s[i] == ')') break;
    string version= s (begin, i);
    tree t  = string_to_scheme_tree (s);
    return scheme_tree_to_tree (t, version);
  }
  return error;
}
