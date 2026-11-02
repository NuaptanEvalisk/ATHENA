// Copyright (C) 2001 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "utility_markup.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "Edit/Interface/tree_pattern.hpp"
#include "analyze.hpp"

namespace {
object selector (tree pattern) {
  if (is_atomic (pattern)) {
    string s= pattern->label;
    return starts (s, "(") ? object (s) : string_to_object (s);
  }
  const string name= as_string (L(pattern));
  if (name == "pat-any") return keyword_object ("%1");
  if (name == "pat-any-repeat") return keyword_object ("*");
  if (name != "pat-or" && name != "pat-and" && name != "pat-group" && name != "pat-and-not")
    return object (false);
  object result= null_object ();
  for (int i=N(pattern)-1; i>=0; --i) result= cons (selector (pattern[i]), result);
  return cons (keyword_object (name (4, N(name))), result);
}
object reference (tree body, tree arguments) {
  object selectors= null_object ();
  for (int i=N(arguments)-1; i>=0; --i) {
    tree arg= arguments[i];
    object decoded= keyword_object ("same");
    if (is_atomic (arg)) {
      string s= arg->label;
      if (is_int (s)) decoded= object (as_int (s));
      else if (s == "up" || s == "next" || s == "previous" || s == "first" || s == "last")
        decoded= keyword_object (s);
    }
    selectors= cons (decoded, selectors);
  }
  object result= native_tree_select (object (body), selectors);
  return is_null (result) ? object (false) : car (result);
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
  object selectors= null_object ();
  for (int i=N(arguments)-1; i>=1; --i) selectors= cons (selector (arguments[i]), selectors);
  tree result (TUPLE);
  for (object selected= native_tree_select (object (body), selectors); !is_null (selected); selected= cdr (selected))
    result << content_to_tree (car (selected));
  return tree (QUOTE, result);
}
tree markup_reference (tree body, tree arguments) {
  object result= reference (body, arguments);
  return is_tree (result) ? as_tree (result) : tree ("false");
}
string markup_reference_arity (tree body, tree arguments) {
  object result= reference (body, arguments);
  return is_tree (result) ? as_string (N(as_tree (result))) : string ("false");
}
string markup_reference_index (tree body, tree arguments) {
  object result= reference (body, arguments);
  if (!is_tree (result)) return "false";
  tree t= as_tree (result);
  path ip= obtain_ip (t);
  return ip_attached (ip) && !is_nil (ip) ? as_string (ip->item) : string ("false");
}
string markup_reference_last (tree body, tree arguments) {
  object result= reference (body, arguments);
  if (!is_tree (result)) return "false";
  tree t= as_tree (result);
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
