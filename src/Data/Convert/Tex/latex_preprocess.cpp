/******************************************************************************
* MODULE     : latex_preprocess.cpp
* DESCRIPTION: Native paragraph and internal bracket normalization for LaTeX
* COPYRIGHT  : (C) 2002 Joris van der Hoeven, (C) 2026 ATHENA authors
* GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#include "latex_preprocess.hpp"
#include "tree_correct.hpp"
#include "file.hpp"
#include <nlohmann/json.hpp>
#include <deque>
#include <set>
#include <stdexcept>

namespace {
using Names= std::set<std::string>;
struct Groups {
  Names inline_environments, sectional, theorem_environments;
  Groups () {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/convert/latex-preprocess.json"), source, false))
      throw std::runtime_error ("Cannot load LaTeX preprocessing groups");
    auto data= nlohmann::json::parse (source.data (), source.data () + N(source));
    if (data.at ("version").get<int> () != 1)
      throw std::runtime_error ("Unsupported LaTeX preprocessing registry version");
    inline_environments= data.at ("inline_environments").get<Names> ();
    sectional= data.at ("sectional").get<Names> ();
    theorem_environments= data.at ("theorem_environments").get<Names> ();
  }
};
const Groups& groups () { static const Groups data; return data; }
bool member (tree t, const Names& names) {
  if (is_atomic (t)) return false;
  string name= as_string (L(t));
  return names.count (std::string (name.data (), N(name))) != 0;
}

bool has_bracket (tree t) {
  if (is_atomic (t)) return false;
  if (is_func (t, LEFT) || is_func (t, RIGHT)) return true;
  for (int i=0; i<N(t); ++i) if (has_bracket (t[i])) return true;
  return false;
}

tree match_brackets (tree t);
tree match_children (tree t) {
  if (is_atomic (t)) return t;
  if (is_concat (t) && has_bracket (t)) {
    int open= 0, missing_left= 0;
    for (int i=0; i<N(t); ++i) {
      if (is_func (t[i], LEFT, 1)) ++open;
      else if (is_func (t[i], RIGHT, 1)) {
        if (open > 0) --open;
        else ++missing_left;
      }
    }
    tree result (CONCAT);
    for (int i=0; i<missing_left; ++i) result << tree (LEFT, ".");
    for (int i=0; i<N(t); ++i) result << match_children (t[i]);
    for (int i=0; i<open; ++i) result << tree (RIGHT, ".");
    return result;
  }
  if (is_func (t, AROUND, 3) || is_func (t, VAR_AROUND, 3) || is_func (t, BIG_AROUND, 2))
    return match_brackets (downgrade_brackets (t, false, false));
  tree result (L(t), N(t));
  for (int i=0; i<N(t); ++i) result[i]= match_brackets (t[i]);
  return result;
}
tree match_brackets (tree t) {
  if (is_func (t, LEFT, 1)) return tree (CONCAT, t, tree (RIGHT, "."));
  if (is_func (t, RIGHT, 1)) return tree (CONCAT, tree (LEFT, "."), t);
  return match_children (t);
}

bool non_isolated (tree t) {
  return !(is_compound (t) && N(t) == 1 && member (t, groups ().sectional));
}
bool glueable (tree t) {
  if (is_func (t, ASSIGN, 2)) return true;
  if (is_compound (t) && N(t) == 1 && member (t, groups ().inline_environments))
    return is_document (t[0]) || is_func (t[0], TFORMAT) || is_func (t[0], TABLE);
  return is_compound (t, "mtm", 2) && glueable (t[1]);
}
bool empty_heading (tree t) {
  if (t == "" || is_func (t, LABEL, 1)) return true;
  if (!is_concat (t)) return false;
  for (int i=0; i<N(t); ++i) if (!empty_heading (t[i])) return false;
  return true;
}

tree preprocess (tree t);
tree preprocess_document (tree t, int start= 0) {
  std::deque<tree> paragraphs;
  // The join decision uses adjacent source nodes, not already merged output.
  for (int i=N(t)-1; i>=start; --i) {
    tree current= preprocess (t[i]);
    if (i+1 < N(t) && ((non_isolated (t[i]) && glueable (t[i+1])) ||
                        (glueable (t[i]) && non_isolated (t[i+1])))) {
      tree joined (PARA);
      joined << current;
      tree next= paragraphs.front ();
      if (is_func (next, PARA))
        for (int j=0; j<N(next); ++j) joined << next[j];
      else joined << next;
      paragraphs.front ()= joined;
    }
    else paragraphs.push_front (current);
  }
  tree result (DOCUMENT);
  for (const auto& paragraph: paragraphs) result << paragraph;
  return result;
}
tree preprocess (tree t) {
  if (is_atomic (t)) return t;
  if (N(t) == 1 && member (t, groups ().theorem_environments) &&
      is_document (t[0]) && N(t[0]) >= 2) {
    tree body;
    if (empty_heading (t[0][0]) && glueable (t[0][1])) {
      body= tree (DOCUMENT, tree (CONCAT, preprocess (t[0][0]), tree (as_tree_label ("tmdummy"))));
      tree tail= preprocess_document (t[0], 1);
      for (int i=0; i<N(tail); ++i) body << tail[i];
    }
    else body= preprocess_document (t[0]);
    return tree (L(t), body);
  }
  if (is_document (t)) {
    if (N(t) == 1 && (is_func (t[0], TFORMAT) || is_func (t[0], TABLE)))
      return preprocess (t[0]);
    return preprocess_document (t);
  }
  tree result (L(t), N(t));
  for (int i=0; i<N(t); ++i) result[i]= preprocess (t[i]);
  return result;
}
}

tree latex_preprocess_source (tree source) {
  return preprocess (match_brackets (source));
}
