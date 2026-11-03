// Copyright (C) 2001 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Typesetting/Env/utility_markup.hpp"
#include "tm_configure.hpp"
#include "native_interfaces.hpp"
#include "Editor/editor.hpp"
#include "Editor/Commands/tree_pattern.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include <optional>

namespace {
using selector_kind= tree_selector::kind;
tree_selector selector (tree pattern) {
  tree_selector result;
  if (is_atomic (pattern)) {
    const string text= pattern->label;
    if (starts (text, "(")) return result;
    const tree parsed= string_to_scheme_tree (text);
    if (is_tuple (parsed, "'", 1) || is_tuple (parsed, "quote", 1)) {
      result.operation= selector_kind::capture;
      result.name= scheme_tree_to_string (parsed[1]);
      return result;
    }
    if (!is_atomic (parsed)) return result;
    const string value= parsed->label;
    if (starts (value, "#:")) {
      result.name= value (2, N(value));
      if (result.name == "*") result.operation= selector_kind::descendants;
      else if (starts (result.name, "%") && is_int (result.name (1, N(result.name)))) {
        result.operation= selector_kind::depth;
        result.count= as_int (result.name (1, N(result.name)));
      }
      else if (result.name == "same" || result.name == "up" || result.name == "down" ||
               result.name == "first" || result.name == "last" ||
               result.name == "next" || result.name == "previous")
        result.operation= selector_kind::navigate;
    }
    else if (is_int (value)) {
      result.operation= selector_kind::index;
      result.count= as_int (value);
    }
    else if (!is_double (value) && value != "#t" && value != "#f" && !is_quoted (value)) {
      result.operation= selector_kind::tag;
      result.name= value;
    }
    return result;
  }
  const string name= as_string (L(pattern));
  if (name == "pat-any") { result.operation= selector_kind::depth; result.count= 1; }
  else if (name == "pat-any-repeat") result.operation= selector_kind::descendants;
  else {
    if (name == "pat-or") result.operation= selector_kind::unite;
    else if (name == "pat-and") result.operation= selector_kind::intersect;
    else if (name == "pat-and-not") result.operation= selector_kind::subtract;
    else if (name == "pat-group") result.operation= selector_kind::sequence;
    else return result;
    for (int i=0; i<N(pattern); ++i) result.branches.push_back (selector (pattern[i]));
  }
  return result;
}
std::optional<tree> reference (tree body, tree arguments) {
  std::vector<tree_selector> selectors;
  for (int i=0; i<N(arguments); ++i) {
    tree_selector decoded {selector_kind::navigate, "same"};
    if (is_atomic (arguments[i])) {
      const string text= arguments[i]->label;
      if (is_int (text)) {
        decoded.operation= selector_kind::index;
        decoded.count= as_int (text);
      }
      else if (text == "up" || text == "next" || text == "previous" ||
               text == "first" || text == "last") decoded.name= text;
    }
    selectors.push_back (std::move (decoded));
  }
  const auto result= native_tree_select (body, selectors);
  if (N(result) == 0) return std::nullopt;
  return result[0];
}
tree mark (tree root, tree source, tree rewritten) {
  path root_ip= obtain_ip (root), source_ip= obtain_ip (source);
  if (!ip_attached (root_ip) || !ip_attached (source_ip)) return rewritten;
  path rp= reverse (root_ip), p= reverse (source_ip);
  if (!(rp <= p)) return rewritten;
  tree argument (ARG, "body");
  for (path relative= p / rp; !is_nil (relative); relative= relative->next)
    argument << as_string (relative->item);
  return tree (MARK, argument, rewritten);
}
tree apply_paragraphs (tree macro, tree source, tree root) {
  if (is_document (source)) {
    tree result (DOCUMENT);
    for (int i=0; i<N(source); ++i) result << markup_apply_paragraphs (macro, source[i]);
    return result;
  }
  if (!is_multi_line (source)) return source;
  tree result (L(source));
  for (int i=0; i<N(source); ++i) result << apply_paragraphs (macro, source[i], root);
  return mark (root, source, result);
}
tree numbered_line (tree source, tree root);
tree numbered_subtree (tree source, tree root) {
  if (is_document (source)) {
    tree result (DOCUMENT);
    for (int i=0; i<N(source); ++i) result << numbered_line (source[i], root);
    return result;
  }
  if (!is_multi_line (source)) return source;
  tree result (L(source));
  for (int i=0; i<N(source); ++i) result << numbered_subtree (source[i], root);
  return mark (root, source, result);
}
tree numbered_line (tree source, tree root) {
  return is_multi_line (source) ? numbered_subtree (source, root) : compound ("numbered-line", source);
}
}

string markup_application_version () { return ATHENA_VERSION; }
string markup_build_user () { return BUILD_USER; }
string markup_build_date () { return BUILD_DATE; }
string markup_host_os () { return HOST_OS; }
string markup_host_vendor () { return HOST_VENDOR; }
string markup_host_cpu () { return HOST_CPU; }
string markup_version_release (tree type) { return texmacs_version (as_string (type)); }
string markup_language_suffix () { return "en"; }
tree markup_map (tree function, tree tuple) {
  tree result (TUPLE);
  for (int i=0; i<N(tuple); ++i) result << tree (COMPOUND, function, tuple[i]);
  return tree (QUOTE, result);
}
tree markup_concat_tuple (tree tuple, tree separator, tree final_separator) {
  if (N(tuple) == 0) return "";
  if (N(tuple) == 1) return tuple[0];
  tree result (CONCAT, tuple[0]);
  for (int i=1; i<N(tuple)-1; ++i) result << tree (CONCAT, separator, tuple[i]);
  result << (is_func (final_separator, UNINIT) ? separator : final_separator) << tuple[N(tuple)-1];
  return result;
}
tree markup_select (tree body, tree arguments) {
  std::vector<tree_selector> selectors;
  for (int i=1; i<N(arguments); ++i) selectors.push_back (selector (arguments[i]));
  tree result (TUPLE);
  const auto selected= native_tree_select (body, selectors);
  for (int i=0; i<N(selected); ++i) result << selected[i];
  return tree (QUOTE, result);
}
tree markup_reference (tree body, tree arguments) {
  auto result= reference (body, arguments);
  return result ? *result : tree ("false");
}
string markup_reference_arity (tree body, tree arguments) {
  auto result= reference (body, arguments);
  return result ? as_string (N(*result)) : string ("false");
}
string markup_reference_index (tree body, tree arguments) {
  auto result= reference (body, arguments);
  if (!result) return "false";
  tree t= *result;
  path ip= obtain_ip (t);
  return ip_attached (ip) && !is_nil (ip) ? as_string (ip->item) : string ("false");
}
string markup_reference_last (tree body, tree arguments) {
  auto result= reference (body, arguments);
  if (!result) return "false";
  tree t= *result;
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return "false";
  tree parent= get_current_editor ()->the_subtree (reverse (ip->next));
  return ip->item == N(parent)-1 ? "true" : "false";
}
tree markup_apply_paragraphs (tree macro, tree body) {
  if (is_multi_line (body)) return apply_paragraphs (macro, body, body);
  return is_atomic (macro) ? tree (as_tree_label (macro->label), body) : body;
}
tree markup_numbered (tree body) {
  if (!is_document (body)) return body;
  return compound ("numbered-block", numbered_subtree (body, body));
}
tree markup_listing (tree body) {
  if (!is_document (body)) return body;
  tree table (TABLE);
  for (int i=0; i<N(body); ++i)
    table << tree (ROW, tree (CELL, tree (WITH, "color", "dark grey", "prog-language", "verbatim", as_string (i+1))),
                        tree (CELL, tree (DOCUMENT, body[i])));
  tree format (TFORMAT);
  format << tree (TWITH, "table-width", "1par") << tree (TWITH, "table-hmode", "exact")
         << tree (TWITH, "table-hyphen", "y")
         << tree (CWITH, "1", "-1", "1", "1", "cell-halign", "r")
         << tree (CWITH, "1", "-1", "1", "1", "cell-lsep", "0em")
         << tree (CWITH, "1", "-1", "2", "2", "cell-halign", "l")
         << tree (CWITH, "1", "-1", "2", "2", "cell-rsep", "0em")
         << tree (CWITH, "1", "-1", "2", "2", "cell-hpart", "1")
         << tree (CWITH, "1", "-1", "2", "2", "cell-hyphen", "t")
         << tree (CWITH, "1", "-1", "1", "-1", "cell-background",
                  compound ("if", compound ("equal", compound ("mod", tree (VALUE, "cell-row-nr"), "2"), "0"), "#f4f4ff", ""))
         << table;
  return format;
}
