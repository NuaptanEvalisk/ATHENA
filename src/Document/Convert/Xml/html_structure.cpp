/******************************************************************************
* MODULE     : html_structure.cpp
* DESCRIPTION: HTML source block normalization and output finalization
* COPYRIGHT  : (C) 2002 Joris van der Hoeven, David Allouche
*              (C) 2026 ATHENA authors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "convert.hpp"
#include "analyze.hpp"
#include <stdexcept>
#include <vector>

namespace {
bool head (scheme_tree t, const char* name) {
  return is_tuple (t) && N(t) > 0 && t[0] == name;
}

bool text_node (scheme_tree t) {
  return is_atomic (t) && is_quoted (t->label);
}

void append_text (scheme_tree& nodes, scheme_tree t) {
  if (text_node (t)) {
    string value= scm_unquote (t->label);
    if (value == "") return;
    if (N(nodes) && text_node (nodes[N(nodes)-1])) {
      nodes[N(nodes)-1]= tree (scm_quote (
        scm_unquote (nodes[N(nodes)-1]->label) * value));
      return;
    }
  }
  nodes << t;
}

// Each nested WITH gets its own accumulator; no exporter-global paragraph state.
class document_normalizer {
  tree paragraphs= tuple ("document");
  tree line= tuple ("concat");

  void flush () {
    paragraphs << line;
    line= tuple ("concat");
  }

  tree wrap (tree source, tree body) {
    tree result (TUPLE);
    for (int i=0; i<N(source)-1; ++i) result << source[i];
    result << body;
    return result;
  }

  void visit (tree source) {
    if (head (source, "document")) {
      for (int i=1; i<N(source); ++i) {
        visit (source[i]);
        if (i+1<N(source)) flush ();
      }
    }
    else if (head (source, "concat")) {
      for (int i=1; i<N(source); ++i) visit (source[i]);
    }
    else if (head (source, "surround") && N(source) == 4) {
      visit (source[1]);
      visit (source[3]);
      visit (source[2]);
    }
    else if (head (source, "with") && N(source) == 2)
      visit (source[1]);
    else if (head (source, "with") && N(source) >= 4) {
      tree body= document_normalizer ().run (source[N(source)-1]);
      if (head (body, "document")) {
        for (int i=1; i<N(body); ++i) {
          visit (wrap (source, body[i]));
          if (i+1<N(body)) flush ();
        }
      }
      else append_text (line, wrap (source, body));
    }
    else if (head (source, "locus") && N(source) > 1)
      visit (source[N(source)-1]);
    else append_text (line, source);
  }

public:
  tree run (tree source) {
    visit (source);
    flush ();
    return N(paragraphs) == 2 ? paragraphs[1] : paragraphs;
  }
};

bool block_document (tree source) {
  if (head (source, "document")) return true;
  if (head (source, "concat")) {
    for (int i=1; i<N(source); ++i)
      if (block_document (source[i])) return true;
  }
  if ((head (source, "surround") && N(source) == 4) ||
      (head (source, "with") && N(source) > 1))
    return block_document (source[N(source)-1]);
  return false;
}

tree finalize_node (tree node) {
  if (!is_tuple (node) || N(node) == 0) return node;
  // Namespace removal applies to element names, never attributes or text.
  if (head (node, "@") || head (node, "@@")) return node;
  tree result (TUPLE);
  tree name= node[0];
  if (is_atomic (name) && starts (name->label, "h:"))
    name= tree (name->label (2, N(name->label)));
  result << name;
  for (int i=1; i<N(node); ++i) result << finalize_node (node[i]);
  if (head (result, "p") && N(result) == 2) {
    tree child= result[1];
    for (const char* tag: {"div", "p", "li", "dt", "dd", "center",
                          "blockquote", "ul", "ol", "dl"})
      if (head (child, tag)) return child;
  }
  return result;
}

tree id_attribute (tree node) {
  if (is_tuple (node) && N(node) > 1 && head (node[1], "@"))
    for (int i=1; i<N(node[1]); ++i)
      if (head (node[1][i], "id") && N(node[1][i]) == 2) return node[1][i];
  return tree ("#f");
}

bool label_node (tree node) {
  return head (node, "h:a") && id_attribute (node) != tree ("#f");
}

tree attach_label (tree element, tree label) {
  tree attrs= tuple ("@", id_attribute (label));
  int start= 1;
  if (N(element) > 1 && head (element[1], "@")) {
    start= 2;
    for (int i=1; i<N(element[1]); ++i) attrs << element[1][i];
  }
  tree result= tuple (element[0], attrs);
  for (int i=start; i<N(element); ++i) result << element[i];
  return result;
}

tree css_nodes (tree nodes);

tree css_node (tree node) {
  if (!is_tuple (node) || !N(node) || head (node, "@") || head (node, "@@")) return node;
  tree children (TUPLE);
  for (int i=1; i<N(node); ++i) children << node[i];
  children= css_nodes (children);
  tree result= tuple (node[0]);
  for (int i=0; i<N(children); ++i) result << children[i];
  return result;
}

tree css_nodes (tree nodes) {
  tree result (TUPLE);
  for (int i=0; i<N(nodes); ++i) {
    tree node= css_node (nodes[i]);
    if (head (node, "h:img") && i+1<N(nodes) && text_node (nodes[i+1])) {
      string text= scm_unquote (nodes[i+1]->label);
      if (!starts (text, " ")) {
        int split= 0;
        while (split<N(text) && text[split] != ' ') ++split;
        tree span= tuple ("h:span", tuple ("@", tuple ("class", tree (scm_quote ("no-breaks")))), node);
        span << tree (scm_quote (text (0, split)));
        result << span;
        if (split<N(text)) result << tree (scm_quote (text (split, N(text))));
        ++i;
        continue;
      }
    }
    result << node;
  }
  return result;
}
} // namespace

namespace {
bool paragraph_block (tree node) {
  if (!is_tuple (node) || !N(node)) return false;
  for (const char* name: {"h:p", "h:div", "h:pre", "h:h1", "h:h2", "h:h3",
       "h:h4", "h:h5", "h:h6", "h:ol", "h:ul", "h:dl", "h:li", "h:dt",
       "h:dd", "h:table", "h:blockquote", "h:section", "h:article", "h:aside",
       "h:figure", "h:header", "h:footer", "h:nav", "h:main", "h:hr"})
    if (head (node, name)) return true;
  for (const char* name: {"h:i", "h:b", "h:u", "h:var", "h:span", "h:a",
                         "h:em", "h:strong"})
    if (head (node, name)) {
      for (int i=1; i<N(node); ++i)
        if (paragraph_block (node[i])) return true;
    }
  return false;
}

void paragraph_spaces (tree source, bool after, std::vector<string>& values) {
  if ((head (source, after ? "vspace" : "vspace*") && N(source) == 2)) {
    tree length= html_export_length (source[1], "css");
    if (length != tree ("#f")) {
      string value= scm_unquote (length->label);
      for (const auto& previous: values) if (value == previous) return;
      values.push_back (value);
    }
  }
  else if (head (source, "concat")) {
    for (int i=1; i<N(source); ++i) paragraph_spaces (source[i], after, values);
  }
  else if (head (source, "with") && N(source) > 1)
    paragraph_spaces (source[N(source)-1], after, values);
}

string paragraph_margin (tree source, bool after) {
  std::vector<string> values;
  paragraph_spaces (source, after, values);
  if (values.empty ()) return "";
  string value= values[0];
  if (values.size () > 1) {
    value= "max(" * value;
    for (size_t i=1; i<values.size (); ++i) value << ", " << values[i];
    value << ")";
  }
  return string (after ? "margin-bottom: " : "margin-top: ") * value;
}

tree paragraph_style (tree node, string style) {
  if (!N(style)) return node;
  tree attrs= tuple ("@");
  int start= 1;
  if (N(node) > 1 && head (node[1], "@")) {
    start= 2;
    for (int i=1; i<N(node[1]); ++i) {
      tree attr= node[1][i];
      if (head (attr, "style") && N(attr) == 2)
        style= scm_unquote (attr[1]->label) * "; " * style;
      else attrs << attr;
    }
  }
  attrs << tuple ("style", tree (scm_quote (style)));
  tree out= tuple (node[0], attrs);
  for (int i=start; i<N(node); ++i) out << node[i];
  return out;
}

tree paragraph_flow (tree node) {
  bool wrapper= false;
  string style;
  for (const char* name: {"h:i", "h:b", "h:u", "h:var", "h:span", "h:a",
                         "h:em", "h:strong"})
    wrapper= wrapper || head (node, name);
  if (!wrapper || !paragraph_block (node)) return node;
  tree out= copy (node);
  for (int i=1; i<N(out); ++i) out[i]= paragraph_flow (out[i]);
  // Anchors have a transparent content model; phrasing-only wrappers do not.
  if (head (node, "h:a")) return out;
  if (head (node, "h:b") || head (node, "h:strong")) style= "font-weight: bold";
  if (head (node, "h:i") || head (node, "h:em") || head (node, "h:var"))
    style= "font-style: italic";
  if (head (node, "h:u")) style= "text-decoration: underline";
  out[0]= tree ("h:div");
  if (!N(style)) return out;
  // Element defaults precede authored styles, which must still win.
  if (N(out) > 1 && head (out[1], "@")) {
    tree attrs= copy (out[1]);
    for (int i=1; i<N(attrs); ++i)
      if (head (attrs[i], "style") && N(attrs[i]) == 2) {
        attrs[i]= tuple ("style", tree (scm_quote (
          style * "; " * scm_unquote (attrs[i][1]->label))));
        out[1]= attrs;
        return out;
      }
  }
  return paragraph_style (out, style);
}
}

scheme_tree html_export_paragraph (scheme_tree source, scheme_tree body) {
  tree result= tuple (), paragraph= tuple ("h:p");
  auto flush= [&] {
    if (N(paragraph) > 1) result << paragraph;
    paragraph= tuple ("h:p");
  };
  for (int i=0; i<N(body); ++i) {
    tree node= paragraph_flow (body[i]);
    if (paragraph_block (node)) {
      flush ();
      result << node;
    }
    else paragraph << node;
  }
  flush ();
  if (!N(result)) result << tuple ("h:p");
  result[0]= paragraph_style (result[0], paragraph_margin (source, false));
  int last= N(result)-1;
  result[last]= paragraph_style (result[last], paragraph_margin (source, true));
  return result;
}

scheme_tree html_export_sequence_plan (scheme_tree source, bool preformatted) {
  if (!head (source, "document") && !head (source, "para"))
    throw std::runtime_error ("HTML export: expected a document or paragraph sequence");
  tree inputs= tuple ();
  bool paragraphs= head (source, "document") && !preformatted;
  for (int i=1; i<N(source); ++i) {
    tree input= source[i];
    if (paragraphs && (head (input, "verbatim") || head (input, "code")) &&
        N(input) == 2 && !head (input[1], "document"))
      input= tuple (input[0], tuple ("document", input[1]));
    inputs << input;
  }
  return tuple (inputs, source, tree (paragraphs ? "paragraphs" :
                 head (source, "para") ? "lines" : "preformatted"));
}

scheme_tree html_export_sequence_output (scheme_tree plan, scheme_tree children) {
  if (!is_tuple (plan) || N(plan) != 3 || !is_tuple (children) ||
      N(children) != N(plan[0]))
    throw std::runtime_error ("HTML export: invalid converted sequence");
  tree result= tuple ();
  for (int i=0; i<N(children); ++i) {
    tree nodes= children[i];
    if (plan[2] == "paragraphs") nodes= html_export_paragraph (plan[1][i+1], nodes);
    else if (plan[2] == "preformatted" && i) result << tree (scm_quote ("\n"));
    else if (plan[2] == "lines" && N(result) && N(nodes)) result << tuple ("h:br");
    for (int j=0; j<N(nodes); ++j) append_text (result, nodes[j]);
  }
  return result;
}

scheme_tree html_export_text (string source, bool preformatted) {
  if (N(source) == 0) return tuple ();
  if (!preformatted) {
    source= replace (source, "--", "\xe2\x80\x93");
    source= replace (source, "``", "\xe2\x80\x9c");
    source= replace (source, "''", "\xe2\x80\x9d");
  }
  return tuple (tree (scm_quote (source)));
}

scheme_tree html_export_finish_body (scheme_tree nodes, bool css) {
  return css ? css_nodes (nodes) : nodes;
}

namespace {
int count_tabs (tree source) {
  if (head (source, "htab")) return 1;
  if (head (source, "with") && N(source) > 1)
    return count_tabs (source[N(source)-1]);
  int count= 0;
  if (head (source, "concat"))
    for (int i=1; i<N(source); ++i) count+= count_tabs (source[i]);
  return count;
}

tree simplify_tabs (tree source, int total, int& seen) {
  if (head (source, "htab")) {
    int index= seen++;
    if (N(source) == 3) {
      bool first= source[2] == tree (scm_quote ("first"));
      bool last= source[2] == tree (scm_quote ("last"));
      if ((first && index != 0) || (last && index != total-1))
        return tree (scm_quote (""));
      if (first || last) return tuple ("htab", source[1], tree (scm_quote ("0")));
    }
    return source;
  }
  if (head (source, "concat") || head (source, "with")) {
    tree result= copy (source);
    int start= head (source, "with") ? N(source)-1 : 1;
    for (int i=start; i<N(source); ++i) result[i]= simplify_tabs (source[i], total, seen);
    return result;
  }
  return source;
}
}

scheme_tree html_export_concat_plan (scheme_tree source) {
  if (!head (source, "concat"))
    throw std::runtime_error ("HTML export: expected a concatenation");
  int seen= 0;
  source= simplify_tabs (source, count_tabs (source), seen);
  int separators= 0;
  for (int i=1; i<N(source); ++i) if (head (source[i], "htab")) ++separators;
  tree result= tuple ();
  if (!separators) {
    for (int i=1; i<N(source); ++i) result << source[i];
    return result;
  }
  int segment= 0;
  tree group= tuple ("!left");
  for (int i=1; i<N(source); ++i) {
    if (!head (source[i], "htab")) { group << source[i]; continue; }
    ++segment;
    const char* next= segment == 1 && separators > 1 ? "!middle" : "!right";
    if (group[0] != next) {
      result << group;
      group= tuple (next);
    }
  }
  result << group;
  return result;
}

scheme_tree html_export_concat_output (scheme_tree nodes) {
  if (N(nodes) == 0 || text_node (nodes[0])) return html_export_simplify_nodes (nodes);
  int first= 0;
  while (first<N(nodes) && label_node (nodes[first])) ++first;
  if (first<N(nodes) && (head (nodes[first], "h:h1") || head (nodes[first], "h:h2") ||
                         head (nodes[first], "h:h3") || head (nodes[first], "h:h4"))) {
    tree heading= nodes[first], result (TUPLE);
    if (id_attribute (heading) != tree ("#f")) {
      result << heading;
      for (int i=0; i<N(nodes); ++i) if (i != first) result << nodes[i];
    }
    else {
      tree labels (TUPLE), rest (TUPLE);
      for (int i=0; i<N(nodes); ++i) {
        if (i == first) continue;
        (label_node (nodes[i]) ? labels : rest) << nodes[i];
      }
      if (!N(labels)) return html_export_simplify_nodes (nodes);
      heading= attach_label (heading, labels[0]);
      tree combined= tuple (heading[0], heading[1]);
      for (int i=1; i<N(labels); ++i) combined << labels[i];
      for (int i=2; i<N(heading); ++i) combined << heading[i];
      result << combined;
      for (int i=0; i<N(rest); ++i) result << rest[i];
    }
    return html_export_simplify_nodes (result);
  }
  bool tables= false;
  for (int i=0; i<N(nodes); ++i) tables= tables || head (nodes[i], "h:table");
  if (tables) {
    // Fold from the right, matching the previous choice of adjacent label.
    std::vector<tree> reversed;
    for (int i=N(nodes)-1; i>=0; --i) {
      tree node= nodes[i];
      if (!reversed.empty () && label_node (node) && head (reversed.back (), "h:table") &&
          id_attribute (reversed.back ()) == tree ("#f"))
        reversed.back ()= attach_label (reversed.back (), node);
      else if (!reversed.empty () && head (node, "h:table") && label_node (reversed.back ()) &&
               id_attribute (node) == tree ("#f"))
        reversed.back ()= attach_label (node, reversed.back ());
      else reversed.push_back (node);
    }
    tree result (TUPLE);
    for (auto i=reversed.rbegin (); i!=reversed.rend (); ++i) result << *i;
    return html_export_simplify_nodes (result);
  }
  if (N(nodes) == 1 && head (nodes[0], "h:div") && N(nodes[0]) > 1 &&
      nodes[0][1] == tuple ("@", tuple ("class", tree (scm_quote ("left-tab"))))) {
    tree result (TUPLE);
    for (int i=2; i<N(nodes[0]); ++i) result << nodes[0][i];
    return html_export_simplify_nodes (result);
  }
  return html_export_simplify_nodes (nodes);
}

scheme_tree html_export_blockify (scheme_tree source) {
  if (head (source, "document")) return source;
  if ((head (source, "surround") && N(source) == 4) ||
      (head (source, "with") && N(source) > 1)) {
    tree result (TUPLE);
    for (int i=0; i<N(source)-1; ++i) result << source[i];
    result << html_export_blockify (source[N(source)-1]);
    return result;
  }
  return tuple ("document", source);
}

scheme_tree html_export_normalize_document (scheme_tree source, bool preserve_block) {
  tree result= document_normalizer ().run (source);
  if (preserve_block && block_document (source) && !head (result, "document"))
    result= html_export_blockify (result);
  return result;
}

scheme_tree html_export_simplify_nodes (scheme_tree nodes) {
  tree result (TUPLE);
  for (int i=0; i<N(nodes); ++i) append_text (result, nodes[i]);
  return result;
}

scheme_tree html_export_list_items (scheme_tree source) {
  auto item= [] (tree t) { return head (t, "item") || head (t, "item*"); };
  if (head (source, "concat")) {
    int start= 1;
    if (N(source) > 2 && head (source[1], "assign")) start= 2;
    if (N(source) > start && item (source[start])) {
      tree body= tuple ("concat");
      for (int i=start+1; i<N(source); ++i) body << source[i];
      return tuple ("!item", source[start], body);
    }
  }
  if (item (source))
    return tuple ("!item", source, tuple ("hspace", tree (scm_quote ("1pt"))));
  if (head (source, "with") && N(source) > 1) {
    tree result (TUPLE);
    for (int i=0; i<N(source)-1; ++i) result << source[i];
    result << html_export_list_items (source[N(source)-1]);
    return result;
  }
  if (!head (source, "document")) return source;

  tree result= tuple ("document");
  tree group= tuple ("document");
  tree marker;
  bool marked= false;
  auto flush= [&] {
    if (N(group) > 1)
      result << (marked ? tuple ("!item", marker, group) : group);
    group= tuple ("document");
  };
  for (int i=1; i<N(source); ++i) {
    tree child= html_export_list_items (source[i]);
    if (head (child, "!item")) {
      flush ();
      marker= child[1];
      marked= true;
      group << child[2];
    }
    else group << child;
  }
  flush ();
  return result;
}

scheme_tree html_export_finalize (scheme_tree source, bool document) {
  tree result= tuple ("*TOP*");
  if (document) {
    result << tuple ("*DOCTYPE*", "html") << finalize_node (source);
  }
  else for (int i=0; i<N(source); ++i) result << finalize_node (source[i]);
  return result;
}
