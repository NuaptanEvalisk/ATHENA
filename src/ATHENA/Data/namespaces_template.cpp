/******************************************************************************
* MODULE     : namespaces_template.cpp
* DESCRIPTION: Template matching and derivation for ATHENA namespaces
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "namespaces_private.hpp"

#include <functional>

namespace athena_namespaces {

const char*
field_type_name (ns_field_type t) {
  switch (t) {
  case ns_string_field: return "string";
  case ns_word_field: return "word";
  case ns_char_field: return "char";
  case ns_int_field: return "int";
  case ns_pos_int_field: return "positive-int";
  case ns_roman_field: return "roman";
  }
  return "string";
}

bool
parse_template_std (const std::string& templ, std::vector<template_token>& out,
                    std::string& error) {
  const std::string& t= templ;
  std::string lit;
  for (size_t i=0; i<t.size (); i++) {
    if (t[i] != '%') {
      lit.push_back (t[i]);
      continue;
    }
    if (i + 1 >= t.size ()) {
      error= "Template ends with a bare %.";
      return false;
    }
    if (t[i + 1] == '%') {
      lit.push_back ('%');
      i++;
      continue;
    }
    if (!lit.empty ()) {
      template_token tok;
      tok.field= false;
      tok.literal= lit;
      tok.type= ns_string_field;
      out.push_back (tok);
      lit.clear ();
    }
    template_token tok;
    tok.field= true;
    tok.literal= "";
    switch (t[i + 1]) {
    case 's': tok.type= ns_string_field; break;
    case 'w': tok.type= ns_word_field; break;
    case 'c': tok.type= ns_char_field; break;
    case 'd': tok.type= ns_int_field; break;
    case 'N': tok.type= ns_pos_int_field; break;
    case 'R': tok.type= ns_roman_field; break;
    default:
      error= "Unknown namespace template placeholder %" +
             std::string (1, t[i + 1]) + ".";
      return false;
    }
    out.push_back (tok);
    i++;
  }
  if (!lit.empty ()) {
    template_token tok;
    tok.field= false;
    tok.literal= lit;
    tok.type= ns_string_field;
    out.push_back (tok);
  }
  return true;
}

bool
parse_template (string templ, std::vector<template_token>& out,
                string& error) {
  std::string native_error;
  bool ok= parse_template_std (tm_to_std (templ), out, native_error);
  if (!ok) error= std_to_tm (native_error);
  return ok;
}

static size_t
utf8_char_len (const std::string& s, size_t pos) {
  if (pos >= s.size ()) return 0;
  unsigned char c= (unsigned char) s[pos];
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xe0) == 0xc0 && pos + 1 < s.size ()) return 2;
  if ((c & 0xf0) == 0xe0 && pos + 2 < s.size ()) return 3;
  if ((c & 0xf8) == 0xf0 && pos + 3 < s.size ()) return 4;
  return 1;
}

static bool
is_space_byte (char c) {
  return std::isspace ((unsigned char) c) != 0;
}

static int
roman_value (char c) {
  switch (std::toupper ((unsigned char) c)) {
  case 'I': return 1;
  case 'V': return 5;
  case 'X': return 10;
  case 'L': return 50;
  case 'C': return 100;
  case 'D': return 500;
  case 'M': return 1000;
  }
  return 0;
}

int
parse_roman_value (std::string_view s) {
  if (s.empty ()) return 0;
  int total= 0;
  int prev= 0;
  for (int i=(int) s.size () - 1; i>=0; i--) {
    int v= roman_value (s[(size_t) i]);
    if (v == 0) return 0;
    if (v < prev) total -= v;
    else {
      total += v;
      prev= v;
    }
  }
  return total;
}

static bool
match_field_lengths (ns_field_type type, const std::string& stem, size_t pos,
                     std::vector<size_t>& lens) {
  if (pos >= stem.size ()) return false;

  if (type == ns_char_field) {
    size_t len= utf8_char_len (stem, pos);
    if (len == 0) return false;
    lens.push_back (len);
    return true;
  }

  if (type == ns_word_field) {
    size_t end= pos;
    while (end < stem.size () && !is_space_byte (stem[end])) end++;
    if (end == pos) return false;
    lens.push_back (end - pos);
    return true;
  }

  if (type == ns_int_field || type == ns_pos_int_field) {
    size_t end= pos;
    if (type == ns_int_field && stem[end] == '-') end++;
    size_t digits= end;
    while (end < stem.size () && std::isdigit ((unsigned char) stem[end]))
      end++;
    if (end == digits) return false;
    if (type == ns_pos_int_field) {
      long long v= std::strtoll (stem.substr (pos, end - pos).c_str (), nullptr,
                                 10);
      if (v <= 0) return false;
    }
    lens.push_back (end - pos);
    return true;
  }

  if (type == ns_roman_field) {
    size_t end= pos;
    while (end < stem.size () && roman_value (stem[end]) != 0) end++;
    if (end == pos) return false;
    for (size_t len=end - pos; len>0; len--)
      lens.push_back (len);
    return true;
  }

  for (size_t end= stem.size (); end>pos; end--)
    lens.push_back (end - pos);
  return true;
}

struct match_candidate {
  std::vector<std::string> captures;
  std::vector<ns_field_type> types;
};

static void
match_backtrack (const std::vector<template_token>& toks,
                 const std::string& stem, size_t tok_i, size_t pos,
                 match_candidate cur, std::vector<match_candidate>& out) {
  if (out.size () > 256) return;
  if (tok_i == toks.size ()) {
    if (pos == stem.size ()) out.push_back (cur);
    return;
  }
  const template_token& tok= toks[tok_i];
  if (!tok.field) {
    if (stem.compare (pos, tok.literal.size (), tok.literal) == 0)
      match_backtrack (toks, stem, tok_i + 1, pos + tok.literal.size (), cur,
                       out);
    return;
  }

  std::vector<size_t> lens;
  if (!match_field_lengths (tok.type, stem, pos, lens)) return;
  for (size_t len: lens) {
    std::string cap= stem.substr (pos, len);
    if (tok.type == ns_roman_field && parse_roman_value (cap) <= 0) continue;
    match_candidate next= cur;
    next.captures.push_back (cap);
    next.types.push_back (tok.type);
    match_backtrack (toks, stem, tok_i + 1, pos + len, next, out);
  }
}

static bool
better_candidate (const match_candidate& a, const match_candidate& b) {
  size_t n= std::min (a.captures.size (), b.captures.size ());
  for (size_t i=0; i<n; i++) {
    if (a.captures[i].size () != b.captures[i].size ())
      return a.captures[i].size () > b.captures[i].size ();
    if (a.captures[i] != b.captures[i])
      return a.captures[i] < b.captures[i];
  }
  return a.captures.size () < b.captures.size ();
}

bool
match_stem (const athena_namespace_definition& ns, const std::string& stem,
            athena_namespace_match& out, string& error) {
  std::vector<std::string> captures;
  std::vector<std::string> capture_types;
  bool ambiguous= false;
  std::string native_error;
  bool matched= match_stem_std (tm_to_std (ns.templ), stem, captures,
                                capture_types, ambiguous, native_error);
  if (!native_error.empty ()) error= std_to_tm (native_error);
  if (!matched) return false;
  out.stem= std_to_tm (stem);
  out.ambiguous= ambiguous;
  for (size_t i=0; i<captures.size (); ++i) {
    out.captures.push_back (std_to_tm (captures[i]));
    out.capture_types.push_back (std_to_tm (capture_types[i]));
  }
  return true;
}

bool
match_stem_std (const std::string& templ, const std::string& stem,
                std::vector<std::string>& captures,
                std::vector<std::string>& capture_types, bool& ambiguous,
                std::string& error) {
  captures.clear ();
  capture_types.clear ();
  ambiguous= false;
  std::vector<template_token> toks;
  if (!parse_template_std (templ, toks, error)) return false;
  std::vector<match_candidate> matches;
  match_backtrack (toks, stem, 0, 0, match_candidate (), matches);
  if (matches.empty ()) return false;
  std::sort (matches.begin (), matches.end (), better_candidate);
  captures= matches[0].captures;
  ambiguous= matches.size () > 1;
  capture_types.reserve (matches[0].types.size ());
  for (ns_field_type type: matches[0].types)
    capture_types.emplace_back (field_type_name (type));
  return true;
}

static child_template_position
normalize_child_position (const std::vector<template_token>& child,
                          child_template_position pos) {
  while (pos.tok < child.size () && !child[pos.tok].field &&
         pos.off >= child[pos.tok].literal.size ()) {
    pos.tok++;
    pos.off= 0;
  }
  return pos;
}

static bool
field_types_compatible_for_derivation (ns_field_type parent,
                                       ns_field_type child) {
  if (parent == child) return true;
  if (parent == ns_string_field) return true;
  if (parent == ns_word_field)
    return child == ns_char_field || child == ns_int_field ||
           child == ns_pos_int_field || child == ns_roman_field;
  if (parent == ns_int_field)
    return child == ns_pos_int_field;
  return false;
}

bool
field_value_satisfies_type (ns_field_type type, const std::string& s) {
  if (s.empty ()) return false;
  if (type == ns_string_field) return true;
  if (type == ns_word_field) {
    for (char c: s)
      if (is_space_byte (c)) return false;
    return true;
  }
  if (type == ns_char_field)
    return utf8_char_len (s, 0) == s.size ();
  if (type == ns_int_field || type == ns_pos_int_field) {
    size_t p= 0;
    if (type == ns_int_field && s[p] == '-') p++;
    if (p >= s.size ()) return false;
    for (size_t i=p; i<s.size (); i++)
      if (!std::isdigit ((unsigned char) s[i])) return false;
    if (type == ns_pos_int_field)
      return std::strtoll (s.c_str (), nullptr, 10) > 0;
    return true;
  }
  if (type == ns_roman_field)
    return parse_roman_value (s) > 0;
  return false;
}

static std::vector<size_t>
field_fill_lengths_for_literal (ns_field_type type, const std::string& lit,
                                size_t off) {
  std::vector<size_t> lens;
  if (off >= lit.size ()) return lens;

  if (type == ns_char_field) {
    size_t len= utf8_char_len (lit, off);
    if (len != 0 && off + len <= lit.size ()) lens.push_back (len);
    return lens;
  }

  if (type == ns_word_field) {
    size_t end= off;
    while (end < lit.size () && !is_space_byte (lit[end])) end++;
    for (size_t len=end - off; len>0; len--)
      lens.push_back (len);
    return lens;
  }

  if (type == ns_int_field || type == ns_pos_int_field) {
    size_t end= off;
    if (type == ns_int_field && lit[end] == '-') end++;
    size_t digits= end;
    while (end < lit.size () && std::isdigit ((unsigned char) lit[end]))
      end++;
    for (size_t len=end - off; len>0; len--) {
      std::string s= lit.substr (off, len);
      if (field_value_satisfies_type (type, s)) lens.push_back (len);
    }
    return lens;
  }

  if (type == ns_roman_field) {
    size_t end= off;
    while (end < lit.size () && roman_value (lit[end]) != 0) end++;
    for (size_t len=end - off; len>0; len--) {
      std::string s= lit.substr (off, len);
      if (field_value_satisfies_type (type, s)) lens.push_back (len);
    }
    return lens;
  }

  for (size_t len=lit.size () - off; len>0; len--)
    lens.push_back (len);
  return lens;
}

static bool
consume_child_literal (const std::vector<template_token>& child,
                       child_template_position pos, const std::string& lit,
                       child_template_position& next) {
  pos= normalize_child_position (child, pos);
  size_t p= 0;
  while (p < lit.size ()) {
    if (pos.tok >= child.size ()) return false;
    if (pos.tok < child.size () && child[pos.tok].field &&
        child[pos.tok].type == ns_string_field) {
      pos.tok++;
      pos.off= 0;
      pos= normalize_child_position (child, pos);
      continue;
    }
    if (child[pos.tok].field) return false;
    const std::string& cur= child[pos.tok].literal;
    if (pos.off >= cur.size ()) {
      pos= normalize_child_position (child, pos);
      continue;
    }
    size_t n= std::min (lit.size () - p, cur.size () - pos.off);
    if (cur.compare (pos.off, n, lit, p, n) != 0) return false;
    p += n;
    pos.off += n;
    pos= normalize_child_position (child, pos);
  }
  next= normalize_child_position (child, pos);
  return true;
}

static int
child_field_index_at (const std::vector<template_token>& child, size_t tok) {
  int index= 0;
  for (size_t i=0; i<tok && i<child.size (); i++)
    if (child[i].field) index++;
  return index;
}

static bool
literal_allows_field_fragment (ns_field_type type, const std::string& s) {
  if (s.empty ()) return false;
  if (type == ns_string_field) return true;
  if (type == ns_word_field) {
    for (char c: s)
      if (is_space_byte (c)) return false;
    return true;
  }
  return field_value_satisfies_type (type, s);
}

static std::vector<size_t>
field_fragment_literal_lengths (ns_field_type type, const std::string& lit,
                                size_t off) {
  std::vector<size_t> lens;
  if (off >= lit.size ()) return lens;
  if (type == ns_string_field) {
    for (size_t len=lit.size () - off; len>0; len--)
      lens.push_back (len);
    return lens;
  }
  if (type == ns_word_field) {
    size_t end= off;
    while (end < lit.size () && !is_space_byte (lit[end])) end++;
    for (size_t len=end - off; len>0; len--)
      lens.push_back (len);
    return lens;
  }
  return field_fill_lengths_for_literal (type, lit, off);
}

static bool
field_expr_identity (const parent_field_expr& expr,
                     const std::vector<template_token>& child,
                     ns_field_type parent_type) {
  if (expr.parts.size () != 1 || !expr.parts[0].child) return false;
  int index= expr.parts[0].child_index;
  int seen= 0;
  for (size_t i=0; i<child.size (); i++) {
    if (!child[i].field) continue;
    if (seen == index) return child[i].type == parent_type;
    seen++;
  }
  return false;
}

static bool
field_expr_can_stop (const parent_field_expr& expr,
                     ns_field_type type) {
  if (expr.parts.empty ()) return false;
  if (type == ns_string_field || type == ns_word_field) return true;
  if (expr.parts.size () != 1) return false;
  const field_fragment& f= expr.parts[0];
  if (f.child) return true;
  return field_value_satisfies_type (type, f.literal);
}

static void
enumerate_parent_field_exprs (const std::vector<template_token>& child,
                              child_template_position start,
                              ns_field_type type,
                              std::vector<std::pair<
                                parent_field_expr,
                                child_template_position> >& out) {
  std::function<void(child_template_position,parent_field_expr)> rec;
  rec= [&] (child_template_position pos, parent_field_expr expr) {
    if (out.size () > 512) return;
    pos= normalize_child_position (child, pos);

    if (pos.tok < child.size ()) {
      const template_token& tok= child[pos.tok];
      if (tok.field) {
        if (field_types_compatible_for_derivation (type, tok.type)) {
          parent_field_expr next= expr;
          field_fragment f;
          f.child= true;
          f.child_index= child_field_index_at (child, pos.tok);
          next.parts.push_back (f);
          rec (child_template_position { pos.tok + 1, 0 }, next);
        }
      }
      else {
        for (size_t len: field_fragment_literal_lengths (type, tok.literal,
                                                         pos.off)) {
          std::string s= tok.literal.substr (pos.off, len);
          if (!literal_allows_field_fragment (type, s)) continue;
          parent_field_expr next= expr;
          field_fragment f;
          f.child= false;
          f.child_index= -1;
          f.literal= s;
          next.parts.push_back (f);
          rec (child_template_position { pos.tok, pos.off + len }, next);
        }
      }
    }

    if (field_expr_can_stop (expr, type))
      out.push_back (std::make_pair (expr, pos));
  };

  parent_field_expr empty;
  empty.type= type;
  rec (start, empty);
}

bool
template_derivation_mapping_tokens (
  const std::vector<template_token>& child,
  const std::vector<template_token>& parent,
  bool require_changed, derivation_result& result) {
  derivation_result current;

  std::function<bool(size_t, child_template_position)> rec;
  rec= [&] (size_t pi, child_template_position cpos) -> bool {
    cpos= normalize_child_position (child, cpos);

    if (pi == parent.size ()) {
      child_template_position end= normalize_child_position (child, cpos);
      if (end.tok == child.size () &&
          (!require_changed || current.changed)) {
        result= current;
        return true;
      }
      return false;
    }

    const template_token& ptok= parent[pi];
    if (!ptok.field) {
      child_template_position next;
      if (!consume_child_literal (child, cpos, ptok.literal, next))
        return false;
      return rec (pi + 1, next);
    }

    std::vector<std::pair<parent_field_expr, child_template_position> > exprs;
    enumerate_parent_field_exprs (child, cpos, ptok.type, exprs);
    for (auto& cand: exprs) {
      bool old_changed= current.changed;
      current.fields.push_back (cand.first);
      current.changed= current.changed ||
        !field_expr_identity (cand.first, child, ptok.type);
      if (rec (pi + 1, cand.second)) return true;
      current.fields.pop_back ();
      current.changed= old_changed;
    }
    return false;
  };

  return rec (0, child_template_position { 0, 0 });
}

bool
template_derivation_mapping (string child_template, string parent_template,
                             bool require_changed, derivation_result& result,
                             string& error) {
  std::vector<template_token> child;
  std::vector<template_token> parent;
  if (!parse_template (child_template, child, error)) return false;
  if (!parse_template (parent_template, parent, error)) return false;
  return template_derivation_mapping_tokens (child, parent, require_changed,
                                            result);
}

bool
template_derives_from (string child_template, string parent_template,
                       bool& derives, string& error) {
  std::string native_error;
  bool ok= template_derives_from_std (tm_to_std (child_template),
                                      tm_to_std (parent_template), derives,
                                      native_error);
  if (!ok) error= std_to_tm (native_error);
  return ok;
}

bool
template_derives_from_std (const std::string& child_template,
                           const std::string& parent_template, bool& derives,
                           std::string& error) {
  derives= false;
  std::vector<template_token> child;
  std::vector<template_token> parent;
  if (!parse_template_std (child_template, child, error)) return false;
  if (!parse_template_std (parent_template, parent, error)) return false;
  derivation_result mapping;
  derives= template_derivation_mapping_tokens (child, parent, true, mapping);
  return true;
}

static std::string
first_literal_before_field (const std::vector<template_token>& toks) {
  for (const template_token& tok: toks) {
    if (tok.field) return "";
    if (!tok.literal.empty ()) return tok.literal;
  }
  return "";
}

static bool
template_starts_with_field (const std::vector<template_token>& toks) {
  return !toks.empty () && toks[0].field;
}

static bool
has_string_field (const std::vector<template_token>& toks) {
  for (const template_token& tok: toks)
    if (tok.field && tok.type == ns_string_field) return true;
  return false;
}

static std::string
placeholder_for_type (ns_field_type type) {
  switch (type) {
  case ns_string_field: return "%s";
  case ns_word_field: return "%w";
  case ns_char_field: return "%c";
  case ns_int_field: return "%d";
  case ns_pos_int_field: return "%N";
  case ns_roman_field: return "%R";
  }
  return "%s";
}

std::string
template_to_std (const std::vector<template_token>& toks) {
  std::string out;
  for (const template_token& tok: toks)
    out += tok.field ? placeholder_for_type (tok.type) : tok.literal;
  return out;
}

static void
insert_non_aggressive_string_tail (std::string& candidate) {
  size_t last_percent= candidate.rfind ('%');
  if (last_percent == std::string::npos || last_percent == 0) return;
  size_t insert_at= candidate.rfind (' ', last_percent);
  if (insert_at == std::string::npos) insert_at= last_percent;
  candidate.insert (insert_at, "%s");
}

bool
subproduct_candidate_from_order (string first_template,
                                 string second_template,
                                 bool aggressive_string,
                                 string& suggestion,
                                 string& error) {
  std::vector<template_token> first, second;
  if (!parse_template (first_template, first, error)) return false;
  if (!parse_template (second_template, second, error)) return false;
  if (!template_starts_with_field (first)) return false;

  std::string lit= first_literal_before_field (second);
  if (lit.empty ()) return false;
  std::string fill= trim_std (lit);
  if (fill.empty ()) return false;

  std::vector<template_token> out= first;
  out.erase (out.begin ());
  std::string candidate= fill + template_to_std (out);
  if (!aggressive_string && has_string_field (second))
    insert_non_aggressive_string_tail (candidate);
  suggestion= std_to_tm (candidate);
  return true;
}



} // namespace athena_namespaces

using namespace athena_namespaces;

bool
athena_namespace_template_derives (string child_template,
                                   string parent_template,
                                   bool& derives, string& error) {
  return template_derives_from (child_template, parent_template, derives,
                                error);
}

bool
athena_namespace_suggest_subproduct_template (string first_template,
                                              string second_template,
                                              bool aggressive_string,
                                              string& suggestion,
                                              string& error) {
  bool derives= false;
  if (!athena_namespace_template_derives (first_template, second_template,
                                          derives, error))
    return false;
  if (derives) {
    suggestion= first_template;
    return true;
  }
  if (!athena_namespace_template_derives (second_template, first_template,
                                          derives, error))
    return false;
  if (derives) {
    suggestion= second_template;
    return true;
  }

  string candidate;
  if (subproduct_candidate_from_order (first_template, second_template,
                                       aggressive_string, candidate, error)) {
    bool d1= false, d2= false;
    if (!athena_namespace_template_derives (candidate, first_template, d1,
                                            error))
      return false;
    if (!athena_namespace_template_derives (candidate, second_template, d2,
                                            error))
      return false;
    if (d1 && d2) {
      suggestion= candidate;
      return true;
    }
  }

  error= "";
  if (subproduct_candidate_from_order (second_template, first_template,
                                       aggressive_string, candidate, error)) {
    bool d1= false, d2= false;
    if (!athena_namespace_template_derives (candidate, first_template, d1,
                                            error))
      return false;
    if (!athena_namespace_template_derives (candidate, second_template, d2,
                                            error))
      return false;
    if (d1 && d2) {
      suggestion= candidate;
      return true;
    }
  }

  error= "Could not infer a sub-product template. Please enter one manually.";
  return false;
}
