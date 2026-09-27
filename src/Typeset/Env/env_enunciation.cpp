/******************************************************************************
* MODULE     : env_enunciation.cpp
* DESCRIPTION: Source-preserving native enunciation title and body presentation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "enunciation_presentation.hpp"
#include "convert.hpp"
#include <set>

namespace {

namespace node= athena::node;
namespace en= athena::enunciation;

string text (const std::string& s) {
  return string (s.data (), static_cast<int> (s.size ()));
}

template<typename T> const T* property (const tree& t, const char* key) {
  const auto* metadata= node::get (t);
  if (!metadata) return nullptr;
  auto found= metadata->properties.find (key);
  return found == metadata->properties.end () ? nullptr :
    std::get_if<T> (&found->second.data);
}

tree append (tree left, tree right, string separator= " ") {
  if (left == "") return right;
  if (right == "") return left;
  return tree (CONCAT, left, separator, right);
}

bool presentation_primitive (tree_label label) {
  switch (label) {
  case DOCUMENT: case CONCAT:
  case FRAC: case SQRT: case RSUB: case RSUP: case LSUB: case LSUP:
  case ABOVE: case BELOW: case WIDE: case VAR_WIDE: case NEG:
  case AROUND: case VAR_AROUND: case BIG_AROUND:
  case LEFT: case MID: case RIGHT: case BIG: case NAMED_SYMBOL:
    return true;
  default: return false;
  }
}

bool formatting_variable (const string& name) {
  static const std::set<string> allowed {
    "mode", "font", "font-family", "font-series", "font-shape", "font-size",
    "font-base-size", "color", "math-font", "math-font-family",
    "math-font-series", "math-font-shape", "math-display"
  };
  return allowed.count (name) != 0;
}

tree rich (const tree& source, int depth) {
  // Property storage has stricter traversal budgets. This also protects direct
  // callers; a rejected display is not a destructive source normalization.
  if (depth > 128) return tree ("[structured title depth exceeded]");
  if (is_atomic (source)) return tree (source->label);
  // Label lookup is a read-only document reference, not an arbitrary macro
  // invocation supplied by a property. Keep historical proof-of references.
  if ((is_func (source, REFERENCE, 1) || is_func (source, PAGEREF, 1)) &&
      is_atomic (source[0])) {
    tree value= is_func (source, PAGEREF) ?
      tree (GET_BINDING, tree (source[0]->label), "1") :
      tree (GET_BINDING, tree (source[0]->label));
    return tree (HLINK, value, "#" * source[0]->label);
  }
  if (presentation_primitive (L(source))) {
    tree result (L(source), N(source));
    for (int i= 0; i < N(source); ++i) result[i]= rich (source[i], depth + 1);
    return result;
  }
  if (is_func (source, WITH) && N(source) >= 3 && (N(source) & 1)) {
    bool safe= true;
    for (int i= 0; i + 1 < N(source); i+= 2)
      safe= safe && is_atomic (source[i]) && is_atomic (source[i + 1]) &&
        formatting_variable (source[i]->label);
    if (safe) {
      tree result (WITH, N(source));
      for (int i= 0; i + 1 < N(source); ++i) result[i]= tree (source[i]->label);
      result[N(source) - 1]= rich (source[N(source) - 1], depth + 1);
      return result;
    }
  }
  if (N(source) == 1) {
    const string tag= as_string (L(source));
    string variable, value;
    if (tag == "math") { variable= "mode"; value= "math"; }
    else if (tag == "text") { variable= "mode"; value= "text"; }
    else if (tag == "strong") { variable= "font-series"; value= "bold"; }
    else if (tag == "em" || tag == "emphasize") {
      variable= "font-shape"; value= "italic";
    }
    else if (tag == "verbatim") { variable= "font-family"; value= "tt"; }
    if (variable != "")
      return tree (WITH, variable, value, rich (source[0], depth + 1));
  }
  // Never evaluate arbitrary macros, computed bindings, external calls, labels,
  // or activation escapes from a property. Preserve the original stored tree.
  return tree (tree_to_scheme (source));
}

tree label_text (edit_env_rep* env, const en::registry& registry,
                 const tree& source, const en::render_metadata* render) {
  if (render && !render->text.empty () && env->provides (text (render->text)))
    return compound (text (render->text));
  auto display= registry.display_name (source);
  return tree (text (display.empty () ? registry.kind_name (source) : display));
}

} // namespace

tree native_enunciation_rich_text (const tree& source) { return rich (source, 0); }

tree native_enunciation_macro (edit_env_rep* env, const tree& source) {
  if (!en::is_canonical (source)) return tree (_ERROR, "Malformed enunciation");
  const auto& properties= node::get (source)->properties;
  if ((properties.count ("attribution") && !property<node::property::list> (source, "attribution")) ||
      (properties.count ("year") && !property<std::string> (source, "year")))
    return tree (_ERROR, "Malformed enunciation title properties");
  const auto& registry= en::standard_registry ();
  const auto* definition= registry.definition (source);
  const auto* render= registry.rendering (source);
  const bool complete= render && render->title_mode == "complete";
  const bool subject= render && render->title_mode == "subject";
  tree name= rich (property<node::rich_text> (source, "name")->content, 0);
  tree heading= complete ? name : label_text (env, registry, source, render);
  tree advance= "";
  const string kind= definition ? text (definition->kind) : string ("enunciation");
  const bool numbered= *property<bool> (source, "numbered") && !complete;
  if (numbered) {
    string counter= kind;
    if (!env->provides ("next-" * counter) || !env->provides ("the-" * counter))
      counter= render ? text (render->counter) : string ("theorem-env");
    if (counter == "") counter= "theorem-env";
    if (counter != "" && env->provides ("next-" * counter) &&
        env->provides ("the-" * counter)) {
      advance= compound ("next-" * counter);
      tree number= compound ("the-" * counter);
      if (render && !render->number_format.empty () &&
          env->provides (text (render->number_format)))
        heading= compound (text (render->number_format), number);
      else if (definition && env->provides (kind * "-numbered"))
        heading= compound (kind * "-numbered", heading, number);
      else heading= append (heading, env->provides ("env-number") ?
                            compound ("env-number", number) : number);
    }
    else heading= append (heading, tree (_ERROR, "Missing enunciation counter"));
  }
  else if (!complete && definition && env->provides (kind * "-unnumbered"))
    heading= compound (kind * "-unnumbered", heading);

  tree details= "";
  if (!complete) {
    if (subject) heading= append (heading, name);
    else details= name;
  }
  if (const auto* authors= property<node::property::list> (source, "attribution")) {
    tree names= "";
    for (const auto& author: *authors) {
      const auto* value= std::get_if<node::rich_text> (&author.data);
      if (!value) return tree (_ERROR, "Malformed enunciation attribution");
      names= append (names, rich (value->content, 0), ", ");
    }
    details= append (details, names, "; ");
  }
  if (const auto* year= property<std::string> (source, "year"))
    details= append (details, tree (text (*year)), ", ");
  if (details != "") heading= append (heading, tree (CONCAT, "(", details, ")"));
  if (const auto* target= property<node::reference> (source, "target"))
    heading= tree (HLINK, heading, "tmfs://wikilink/" * text (target->id));

  tree body (ARG, "body");
  tree presented;
  if (render && render->body_only && env->provides (text (render->style))) {
    presented= compound (text (render->style), body);
    if (numbered || name != "" || details != "" || property<node::reference> (source, "target"))
      presented= tree (SURROUND, append (heading, tree (" "), "."), "", presented);
  }
  else {
    string style= render ? text (render->style) : string ("render-remark");
    if (!env->provides (style)) style= "render-enunciation";
    if (env->provides (style)) presented= compound (style, heading, body);
    else presented= tree (SURROUND, append (heading, tree (" "), "."), "", body);
  }
  if (advance != "") presented= tree (SURROUND, advance, "", presented);
  return tree (MACRO, "body", tree (DOCUMENT, presented));
}
