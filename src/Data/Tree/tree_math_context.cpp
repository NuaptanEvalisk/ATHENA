// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "tree_math_context.hpp"
#include "tree_analyze.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"
#include "Data/NodeContracts/syntax_query.hpp"
#include "Env/local_environment.hpp"
#include "new_style.hpp"
#include "convert.hpp"
#include "node_metadata.hpp"
#include <stdexcept>

math_correction_source::math_correction_source (
    athena::contracts::source_environment_preparation prepare): prepare (std::move (prepare)) {}

std::shared_ptr<const evaluation_environment_checkpoint>
math_correction_source::entry (path address) const {
  auto result= prepare (address);
  if (!result) {
    const string where= as_string (address);
    throw std::runtime_error ("Math correction has no source environment at " +
      std::string (where.data (), N(where)));
  }
  return result;
}

void math_correction_source::retain (tree t, path address) {
  if (is_compound (t) && (L(t) >= START_EXTENSIONS ||
      is_func (t, VALUE) || is_func (t, OR_VALUE)))
    origins.emplace (t.operator-> (), origin {t, address});
}

std::shared_ptr<const evaluation_environment_checkpoint>
math_correction_source::context (tree t) const {
  return entry (origins.at (t.operator-> ()).address);
}

tree math_correction_source::map (tree t, path address,
    const std::function<tree (tree, bool)>& transform, bool literal) {
  if (is_func (t, ASSIGN) || is_func (t, MACRO) || is_func (t, XMACRO) ||
      is_func (t, QUOTE) || is_func (t, QUASI) || is_func (t, QUASIQUOTE)) return t;
  const auto prepared= entry (address);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  tree result= t;
  if (is_compound (t)) {
    result= tree (t, N(t));
    athena::node::copy_metadata (t, result);
    for (int i=0; i<N(t); ++i) {
      if ((!is_func (t, WITH) || i == N(t)-1) && is_correctable_child (t, i, query)) {
        bool child_literal= literal && query.value (t, i, MODE, UNINIT) == UNINIT;
        if (is_func (t, WITH) && i == N(t)-1)
          for (int j=0; j+1<N(t); j+=2)
            if (t[j] == MATH_FONT_FAMILY) child_literal= true;
        result[i]= map (t[i], address * i, transform, child_literal);
      }
      else {
        // Distinct occurrences of a shared macro need distinct output handles.
        result[i]= is_atomic (t[i]) ? t[i] : t[i] (0, N(t[i]));
        athena::node::copy_metadata (t[i], result[i]);
        retain (result[i], address * i);
      }
    }
    retain (result, address);
  }
  return transform (result, !literal && prepared->effective_value (MODE) == "math");
}

tree math_correction_source::map (tree t, const std::function<tree (tree, bool)>& transform) {
  origins.clear ();
  return map (t, path (), transform, false);
}

int math_correction_source::classify (tree t) const {
  if (is_func (t, WITH) || is_func (t, STYLE_WITH) || is_func (t, VAR_STYLE_WITH))
    return N(t) == 0 ? SYMBOL_SKIP : classify (t[N(t)-1]);
  if (is_compound (t) && (L(t) >= START_EXTENSIONS ||
      is_func (t, VALUE) || is_func (t, OR_VALUE))) {
    const auto prepared= context (t);
    const auto values= prepared->variables ();
    return symbol_type (t, athena::contracts::syntax_query {
      {prepared->contract_context ()}, values});
  }
  const hashmap<string,tree> values (UNINIT);
  return symbol_type (t, athena::contracts::syntax_query {
    {athena::contracts::builtin_contracts ()}, values});
}

array<int> math_correction_source::types (array<tree> tokens) const {
  array<int> result (N(tokens));
  for (int i=0; i<N(tokens); ++i) result[i]= classify (tokens[i]);
  return result;
}

tree correct_detached_math (tree t,
    const std::function<tree (tree, const correction_context_factory&)>& correct) {
  const bool snippet= is_snippet (t);
  const auto contexts= [t, snippet] (tree stage) {
    const bool wrapped= !is_document (stage);
    const tree body= wrapped ? tree (DOCUMENT, stage) : stage;
    const tree document= snippet ? tree (DOCUMENT,
      compound ("style", tuple ("generic")), compound ("body", body)) :
      change_doc_attr (t, "body", body);
    const auto prepared= prepare_document_source_environments (document);
    return [prepared, wrapped] (path source) {
      std::vector<int> address;
      if (wrapped) address.push_back (0);
      for (; !is_nil (source); source= source->next) address.push_back (source->item);
      return prepared (address);
    };
  };
  const tree result= correct (snippet ? t : extract (t, "body"), contexts);
  return snippet ? result : change_doc_attr (t, "body", result);
}
