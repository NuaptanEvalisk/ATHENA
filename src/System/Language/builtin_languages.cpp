/******************************************************************************
* MODULE     : builtin_languages.cpp
* DESCRIPTION: native built-in packrat language declarations
* COPYRIGHT  : (C) 2010 Joris van der Hoeven
*               (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/>.
*******************************************************************************/

#include "packrat.hpp"
#include "analyze.hpp"
#include "convert.hpp"

namespace {

#include "builtin_language_data.inc"

unsigned
child_index (const builtin_expr_node& node, unsigned index) {
  return builtin_expr_children[node.first_child + index];
}

tree
symbol_expression (const char* text) {
  string s (text);
  if (s == ":<") return compound ("tm-open");
  if (s == ":/") return tree ("<|>");
  if (s == ":>") return tree ("</>");
  if (s == ":any") return compound ("tm-any");
  if (s == ":args") return compound ("tm-args");
  if (s == ":leaf") return compound ("tm-leaf");
  if (s == ":char") return compound ("tm-char");
  if (s == ":cursor") return compound ("tm-cursor");
  if (starts (s, ":<") && N(s) > 2)
    return tree ("<\\" * s (2, N(s)) * ">");
  return compound ("symbol", tree (s));
}

tree
packrat_expression (unsigned index) {
  const builtin_expr_node& node= builtin_expr_nodes[index];
  switch (node.kind) {
  case builtin_expr_kind::symbol:
    return symbol_expression (node.text);
  case builtin_expr_kind::literal:
    return tree (string (node.text));
  case builtin_expr_kind::sequence: {
    tree result= compound ("concat");
    for (unsigned i=0; i<node.child_count; ++i)
      result << packrat_expression (child_index (node, i));
    return result;
  }
  case builtin_expr_kind::choice: {
    tree result= compound ("or");
    for (unsigned i=0; i<node.child_count; ++i)
      result << packrat_expression (child_index (node, i));
    return result;
  }
  case builtin_expr_kind::zero_or_more:
    return compound ("while", packrat_expression (child_index (node, 0)));
  case builtin_expr_kind::one_or_more:
    return compound ("repeat", packrat_expression (child_index (node, 0)));
  case builtin_expr_kind::range:
    return compound (
      "range", packrat_expression (child_index (node, 0)),
      packrat_expression (child_index (node, 1)));
  case builtin_expr_kind::negate:
    return compound ("not", packrat_expression (child_index (node, 0)));
  case builtin_expr_kind::except_:
    return compound (
      "except", packrat_expression (child_index (node, 0)),
      packrat_expression (child_index (node, 1)));
  }
  return compound ("concat");
}

scheme_tree
definition_expression (unsigned index) {
  const builtin_expr_node& node= builtin_expr_nodes[index];
  if (node.kind == builtin_expr_kind::symbol)
    return scheme_tree (string (node.text));
  if (node.kind == builtin_expr_kind::literal)
    return scheme_tree (scm_quote (string (node.text)));

  scheme_tree result (TUPLE);
  switch (node.kind) {
  case builtin_expr_kind::choice: result << scheme_tree (string ("or")); break;
  case builtin_expr_kind::zero_or_more: result << scheme_tree (string ("*")); break;
  case builtin_expr_kind::one_or_more: result << scheme_tree (string ("+")); break;
  case builtin_expr_kind::range: result << scheme_tree (string ("-")); break;
  case builtin_expr_kind::negate: result << scheme_tree (string ("not")); break;
  case builtin_expr_kind::except_: result << scheme_tree (string ("except")); break;
  case builtin_expr_kind::sequence:
  case builtin_expr_kind::symbol:
  case builtin_expr_kind::literal:
    break;
  }
  for (unsigned i=0; i<node.child_count; ++i)
    result << definition_expression (child_index (node, i));
  return result;
}

void
register_definition (const builtin_language_op& op) {
  tree grammar= compound ("or");
  for (unsigned i=0; i<op.root_count; ++i)
    grammar << packrat_expression (builtin_rule_roots[op.first_root + i]);
  packrat_define (string (op.language), string (op.symbol), grammar);
}

} // namespace

void
register_builtin_packrat_languages () {
  for (const builtin_language_op& op: builtin_language_ops) {
    switch (op.kind) {
    case builtin_op_kind::define:
      register_definition (op);
      break;
    case builtin_op_kind::property:
      packrat_property (
        string (op.language), string (op.symbol), string (op.name),
        string (op.value));
      break;
    case builtin_op_kind::inherit:
      packrat_inherit (string (op.language), string (op.symbol));
      break;
    }
  }
}

scheme_tree
builtin_packrat_definition (string language, string symbol) {
  for (const builtin_language_op& op: builtin_language_ops) {
    if (op.kind != builtin_op_kind::define ||
        language != op.language || symbol != op.symbol)
      continue;
    scheme_tree result (TUPLE);
    for (unsigned i=0; i<op.root_count; ++i)
      result << definition_expression (
        builtin_rule_roots[op.first_root + i]);
    return result;
  }
  return scheme_tree ();
}
