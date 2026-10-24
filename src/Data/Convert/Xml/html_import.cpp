/******************************************************************************
* MODULE     : html_import.cpp
* DESCRIPTION: Native HTML-to-document semantics
* COPYRIGHT  : (C) 2026 ATHENA authors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "convert.hpp"
#include "html_import.hpp"
#include "analyze.hpp"
#include "colors.hpp"
#include "svg_colors.hpp"
#include "math_tree_v3.hpp"
#include "math_source_normalization.hpp"
#include "node_metadata.hpp"
#include <cctype>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace {
tree text (string value) { return tree (scm_quote (value)); }

string attribute (tree attrs, const char* name) {
  for (int i=0; i<N(attrs); ++i) {
    tree attr= attrs[i];
    if (is_tuple (attr) && N(attr) == 2 && attr[0] == name && is_atomic (attr[1]))
      return is_quoted (attr[1]->label) ? scm_unquote (attr[1]->label) : attr[1]->label;
  }
  return "";
}

bool has_attribute (tree attrs, const char* name) {
  for (int i=0; i<N(attrs); ++i)
    if (is_tuple (attrs[i]) && N(attrs[i]) && attrs[i][0] == name) return true;
  return false;
}

string import_color (string value) {
  value= locase_all (trim_spaces (value));
  if (N(value) && value[0] == '#') {
    if (N(value) != 4 && N(value) != 7) return "";
    for (int i=1; i<N(value); ++i)
      if (!std::isxdigit ((unsigned char) value[i])) return "";
    return get_hex_color (value);
  }
  for (const auto& entry: SVGColors)
    if (entry.name[0] && value == entry.name)
      return get_hex_color (rgb_color (entry.r, entry.g, entry.b));
  return "";
}

string dimension (string value) {
  value= trim_spaces (value);
  if (is_double (value)) return as_string (as_double (value)) * "px";
  if (ends (value, "%")) {
    string number= value (0, N(value)-1);
    if (is_double (number)) return as_string (as_double (number)/100) * "par";
  }
  return "";
}

bool html_space (char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool string_node (tree t) {
  return is_atomic (t) && is_quoted (t->label);
}

bool node_is (tree t, const char* name) {
  return is_tuple (t) && N(t)>0 && t[0] == name;
}

tree html_attributes (tree node) {
  return N(node)>1 && node_is (node[1], "@") ? node[1](1,N(node[1])) : tree (TUPLE);
}

int table_span (tree attrs, const char* name, int maximum) {
  string value= trim_spaces (attribute (attrs, name));
  int result=0, i=0;
  if (N(value) && value[0] == '+') ++i;
  int first=i;
  while (i<N(value) && value[i]>='0' && value[i]<='9') {
    result= std::min (maximum, result*10+value[i]-'0');
    ++i;
  }
  return i == first || !result ? 1 : result;
}

using nodes = std::vector<tree>;

void append_inline (nodes& into, tree t) {
  if (node_is (t, "concat")) {
    for (int i=1; i<N(t); ++i) append_inline (into, t[i]);
  }
  else if (string_node (t)) {
    string s= scm_unquote (t->label);
    if (!N(s)) return;
    if (!into.empty () && string_node (into.back ()))
      into.back ()= text (scm_unquote (into.back ()->label) * s);
    else into.push_back (t);
  }
  else into.push_back (t);
}

tree concat (const nodes& items) {
  if (items.empty ()) return text ("");
  if (items.size () == 1) return items.front ();
  tree result= tuple ("concat");
  for (const tree& item: items) result << item;
  return result;
}

bool list_marker (tree item) {
  while ((node_is (item, "concat") || node_is (item, "para")) && N(item)>1)
    item= item[1];
  return item == tuple ("item") || node_is (item, "item*");
}

void trim_line (nodes& items, bool left) {
  // Labels are zero-width anchors, not boundaries of visible whitespace.
  size_t offset=0;
  while (offset<items.size ()) {
    size_t index= left ? offset : items.size ()-1-offset;
    tree item= items[index];
    if (node_is (item, "label")) { ++offset; continue; }
    if (!string_node (item)) break;
    string value= scm_unquote (item->label);
    int first=0, last=N(value);
    if (left) while (first<last && html_space (value[first])) ++first;
    else while (last>first && html_space (value[last-1])) --last;
    if (first == last) items.erase (items.begin ()+index);
    else { items[index]= text (value(first,last)); break; }
  }
}

tree inline_run (const nodes& source, bool preserve, bool block_boundary) {
  nodes flat;
  for (const tree& item: source) append_inline (flat, item);
  if (preserve) return concat (flat);
  nodes result, line;
  bool follows_break=false;
  auto flush= [&] (bool before_break) {
    if (follows_break || block_boundary) trim_line (line, true);
    if (before_break || block_boundary) trim_line (line, false);
    result.insert (result.end (), line.begin (), line.end ());
    line.clear ();
  };
  for (const tree& item: flat) {
    if (node_is (item, "next-line") || node_is (item, "new-line")) {
      flush (true);
      result.push_back (tuple ("next-line"));
      follows_break=true;
    }
    else line.push_back (item);
  }
  flush (false);
  return concat (result);
}

tree insert_label (tree body, tree label) {
  if (string_node (body)) return inline_run ({label, body}, true, false);
  // Section destinations belong after the heading, not inside its title.
  for (const char* name: {"part", "chapter", "section", "subsection",
                          "subsubsection", "paragraph", "subparagraph"})
    if (node_is (body, name)) return inline_run ({body, label}, true, false);
  if (node_is (body, "concat")) {
    nodes items;
    if (N(body)>1) items.push_back (insert_label (body[1], label));
    else items.push_back (label);
    for (int i=2; i<N(body); ++i) items.push_back (body[i]);
    return inline_run (items, true, false);
  }
  int index=-1;
  if (node_is (body, "document") || node_is (body, "para")) {
    if (N(body) == 1) { tree result= copy (body); result << label; return result; }
    index=1;
  }
  else if (node_is (body, "compound") && N(body) == 3 &&
           node_is (body[2], "document")) index=2;
  else if (node_is (body, "with") && N(body)>1 &&
           node_is (body[N(body)-1], "document")) index=N(body)-1;
  else if (body == tuple ("item")) return inline_run ({body, label}, true, false);
  else if (node_is (body, "item*") && N(body) == 2) index=1;
  else if (is_tuple (body) && N(body) == 2 &&
           node_is (body[1], "document") &&
           is_extension (scheme_tree_to_tree (body))) index=1;
  if (index>=0) {
    tree result= copy (body);
    result[index]= insert_label (body[index], label);
    return result;
  }
  if (is_multi_paragraph (scheme_tree_to_tree (body)))
    return tuple ("document", label, body);
  return inline_run ({label, body}, true, false);
}
}

scheme_tree html_import_serial (scheme_tree source, bool preserve) {
  bool has_blocks=false;
  for (int i=0; i<N(source); ++i)
    has_blocks |= node_is (source[i], "document");
  nodes run;
  if (!has_blocks) {
    for (int i=0; i<N(source); ++i) run.push_back (source[i]);
    return inline_run (run, preserve, false);
  }
  tree result= tuple ("document");
  auto flush= [&] {
    if (run.empty ()) return;
    tree line= inline_run (run, preserve, true);
    if (preserve || line != text ("")) result << line;
    run.clear ();
  };
  for (int i=0; i<N(source); ++i) {
    if (node_is (source[i], "document")) {
      flush ();
      for (int j=1; j<N(source[i]); ++j) result << source[i][j];
    }
    else run.push_back (source[i]);
  }
  flush ();
  return result;
}

scheme_tree html_import_whitespace (string mode, scheme_tree nodes, bool preserve) {
  tree result (TUPLE);
  if (mode == "element") {
    for (int i=0; i<N(nodes); ++i)
      if (!string_node (nodes[i])) result << nodes[i];
    return result;
  }
  if (mode != "collapse" && mode != "mixed" && mode != "pre")
    throw std::runtime_error ("HTML import: unknown whitespace mode");
  for (int i=0; i<N(nodes); ++i) {
    if (!string_node (nodes[i])) { result << nodes[i]; continue; }
    string value= scm_unquote (nodes[i]->label);
    // Adjacent DOM text nodes are one text run, including across parser splits.
    while (i+1<N(nodes) && string_node (nodes[i+1]))
      value << scm_unquote (nodes[++i]->label);
    if (preserve || mode == "pre") {
      // Lexbor already applies HTML's leading-newline rule for PRE elements.
      // Preserve all remaining lines, including intentional final blank lines.
      bool multiline=false;
      for (int j=0; j<N(value); ++j) multiline |= value[j] == '\n';
      if (multiline) {
        int start=0;
        for (int j=0; j<=N(value); ++j)
          if (j == N(value) || value[j] == '\n') {
            result << tuple ("h:p", text (value(start,j)));
            start=j+1;
          }
      }
      else result << text (value);
      continue;
    }
    string collapsed;
    bool space=false;
    for (int j=0; j<N(value); ++j) {
      if (html_space (value[j])) {
        if (!space) collapsed << ' ';
        space=true;
      }
      else { collapsed << value[j]; space=false; }
    }
    int first=0, last=N(collapsed);
    if (mode == "mixed") {
      if (N(result) == 0 && first<last && collapsed[first] == ' ') ++first;
      if (i+1 == N(nodes) && first<last && collapsed[last-1] == ' ') --last;
    }
    result << text (collapsed(first,last));
  }
  return result;
}

scheme_tree html_import_table_plan (scheme_tree table) {
  nodes groups, footers;
  for (int i=1; i<N(table); ++i) {
    tree child= table[i];
    if (node_is (child, "h:tfoot")) footers.push_back (child);
    else if (node_is (child, "h:thead") || node_is (child, "h:tbody"))
      groups.push_back (child);
  }
  groups.insert (groups.end (), footers.begin (), footers.end ());
  tree rows (TUPLE), spans (TUPLE);
  std::vector<int> occupied;
  int width=0;
  for (const tree& group: groups) {
    for (int r=1; r<N(group); ++r) {
      tree row= group[r];
      if (!node_is (row, "h:tr")) continue;
      tree cells (TUPLE);
      int column=0, row_number=N(rows)+1;
      for (int c=1; c<N(row); ++c) {
        tree cell= row[c];
        if (!node_is (cell, "h:td") && !node_is (cell, "h:th")) continue;
        while (column<(int) occupied.size () && occupied[column]>0) ++column;
        tree attrs= html_attributes (cell);
        int row_span= table_span (attrs, "rowspan", 65534);
        int col_span= table_span (attrs, "colspan", 1000);
        while (N(cells)<column) cells << tree ("#f");
        tree ancestry (TUPLE);
        ancestry << group << row << cell;
        cells << ancestry;
        auto span= [&] (const char* name, int extent) {
          if (extent == 1) return;
          tree format= tuple ("cwith", as_string (row_number), as_string (row_number),
                              as_string (column+1));
          format << tree (as_string (column+1)) << text (name) << tree (as_string (extent));
          spans << format;
        };
        span ("cell-row-span", row_span);
        span ("cell-col-span", col_span);
        if ((int) occupied.size ()<column+col_span) occupied.resize (column+col_span, 0);
        for (int j=column; j<column+col_span; ++j)
          occupied[j]= std::max (occupied[j], row_span);
        column+=col_span;
      }
      while (column<(int) occupied.size () && occupied[column]>0) ++column;
      width= std::max (width, column);
      rows << cells;
      for (int& remaining: occupied) remaining= std::max (0, remaining-1);
    }
  }
  for (int i=0; i<N(rows); ++i)
    while (N(rows[i])<width) rows[i] << tree ("#f");
  tree reverse_spans (TUPLE);
  for (int i=N(spans)-1; i>=0; --i) reverse_spans << spans[i];
  tree result (TUPLE);
  result << rows << reverse_spans;
  return result;
}

scheme_tree html_import_table (scheme_tree attrs, scheme_tree cells,
                               scheme_tree spans) {
  tree output (TUPLE);
  if (!N(cells)) return output;
  tree format= tuple ("tformat");
  auto table_property= [&] (string key, string value) {
    format << tuple ("twith", text (key), text (value));
  };
  auto cell_property= [&] (string key, string value,
                          const char* last_row="-1", const char* last_col="-1") {
    tree property= tuple ("cwith", text ("1"), text (last_row), text ("1"));
    property << text (last_col) << text (key) << text (value);
    format << property;
  };
  string width= dimension (attribute (attrs, "width"));
  table_property ("table-width", N(width) ? width : string ("1par"));
  if (!N(width)) table_property ("table-hmode", "min");
  cell_property ("cell-hyphen", "t");

  string frame="void", rules="none";
  double border=1;
  if (has_attribute (attrs, "border")) {
    string value= attribute (attrs, "border");
    border= is_double (value) ? as_double (value) : 1;
    if (border>0) { frame="border"; rules="all"; }
    else border=0;
  }
  if (has_attribute (attrs, "frame")) {
    frame= locase_all (attribute (attrs, "frame"));
    if (frame != "void" && frame != "above" && frame != "below" &&
        frame != "hsides" && frame != "lhs" && frame != "rhs" &&
        frame != "vsides" && frame != "box" && frame != "border") frame="border";
  }
  if (has_attribute (attrs, "rules")) {
    rules= locase_all (attribute (attrs, "rules"));
    if (rules != "none" && rules != "groups" && rules != "rows" &&
        rules != "cols" && rules != "all") rules="all";
  }
  if (border>0) {
    string thickness= as_string (border)*"px";
    bool all= frame == "border" || frame == "box";
    if (all || frame == "above" || frame == "hsides")
      table_property ("table-tborder", thickness);
    if (all || frame == "below" || frame == "hsides")
      table_property ("table-bborder", thickness);
    if (all || frame == "lhs" || frame == "vsides")
      table_property ("table-lborder", thickness);
    if (all || frame == "rhs" || frame == "vsides")
      table_property ("table-rborder", thickness);
  }
  if (rules == "all" || rules == "rows")
    cell_property ("cell-bborder", "1px", "-2");
  if (rules == "all" || rules == "cols")
    cell_property ("cell-rborder", "1px", "-1", "-2");
  string background= import_color (attribute (attrs, "bgcolor"));
  if (N(background)) cell_property ("cell-background", background);
  for (int i=N(spans)-1; i>=0; --i) {
    tree property= tuple ("cwith");
    for (int j=1; j<N(spans[i]); ++j) {
      tree value= spans[i][j];
      property << (string_node (value) ? value : text (value->label));
    }
    format << property;
  }
  tree table= tuple ("table");
  for (int i=0; i<N(cells); ++i) {
    tree row= tuple ("row");
    for (int j=0; j<N(cells[i]); ++j) {
      tree body= cells[i][j];
      row << tuple ("cell", node_is (body, "document") ? body : tuple ("document", body));
    }
    table << row;
  }
  format << table;
  output << tuple ("tabular", format);
  string id= attribute (attrs, "id");
  if (N(id)) output << tuple ("label", text (id));
  string align= locase_all (attribute (attrs, "align"));
  if (align == "left" || align == "center" || align == "right") {
    tree body= html_import_serial (output, false);
    if (node_is (body, "document") && N(body) == 2) body= body[1];
    output= tree (TUPLE);
    output << tuple ("document", tuple ("with", text ("par-mode"), text (align), body));
  }
  return output;
}

scheme_tree html_import_element (string tag, scheme_tree attrs, scheme_tree body) {
  if (tag == "font-color") {
    string color= import_color (attribute (attrs, "color"));
    return N(color) ? tuple ("with", text ("color"), text (color), body) : body;
  }
  if (tag == "label") {
    string target= attribute (attrs, "target");
    return N(target) ? insert_label (body, tuple ("label", text (target))) : body;
  }
  if (tag == "list-body") {
    tree result= tuple ("document");
    for (int i=1; i<N(body); ++i) {
      tree item= body[i];
      if (list_marker (item) && i+1<N(body) && !list_marker (body[i+1])) {
        tree pair (TUPLE);
        pair << item << body[++i];
        item= html_import_serial (pair, true);
      }
      result << item;
    }
    return result;
  }
  if (tag == "a") {
    string href= attribute (attrs, "href");
    // URL spelling stays intact: decoding here would change link identity.
    return N(href) ? tuple ("hlink", body, text (href)) : body;
  }
  if (tag == "img") {
    string width= dimension (attribute (attrs, "width"));
    string height= dimension (attribute (attrs, "height"));
    if (!N(width) && !N(height)) width= "0.6383w";
    tree image= tuple ("image", text (attribute (attrs, "src")), text (width), text (height));
    image << text ("") << text ("");
    return image;
  }
  if (tag == "font-size") {
    string size= trim_spaces (attribute (attrs, "size"));
    struct size_mapping { const char* html; const char* multiplier; };
    static const size_mapping sizes[]= {
      {"-4", "0.5"}, {"-3", "0.6"}, {"-2", "0.7"}, {"-1", "0.8"},
      {"+1", "1.2"}, {"+2", "1.4"}, {"+3", "1.7"}, {"+4", "2"}
    };
    for (const auto& entry: sizes)
      if (size == entry.html)
        return tuple ("with", text ("font-size"), text (entry.multiplier), body);
    return body;
  }
  throw std::runtime_error ("HTML import: unknown element operation");
}

namespace {
string element_name (tree source) {
  if (!is_tuple (source) || !N(source) || !is_atomic (source[0])) return "";
  string name= source[0]->label;
  return starts (name, "h:") || starts (name, "m:") ? name(2,N(name)) : name;
}

tree element_content (tree source) {
  int start= N(source)>1 && node_is (source[1], "@") ? 2 : 1;
  return source(start,N(source));
}

bool one_of (string name, std::initializer_list<const char*> names) {
  for (const char* candidate: names) if (name == candidate) return true;
  return false;
}

class html_importer {

  void metadata_to_serial (tree native, tree& serial) {
    if (const auto* meta= athena::node::get (native)) athena::node::set (serial, *meta);
    if (is_atomic (native)) return;
    int offset=N(serial)-N(native);
    for (int i=0; i<N(native); ++i) metadata_to_serial (native[i], serial[i+offset]);
  }

  tree structured (tree native) {
    tree serial= tree_to_scheme_tree (native);
    metadata_to_serial (native, serial);
    return serial;
  }

  void metadata_to_native (tree serial, tree& native) {
    if (const auto* meta= athena::node::get (serial)) athena::node::set (native, *meta);
    if (is_atomic (native)) return;
    int offset=N(serial)-N(native);
    for (int i=0; i<N(native); ++i) metadata_to_native (serial[i+offset], native[i]);
  }

  tree materialize (tree serial) {
    tree native= scheme_tree_to_tree (serial);
    metadata_to_native (serial, native);
    return native;
  }

  tree latex (string source) {
    return structured (athena::document::normalize_imported_math (
      latex_to_tree (parse_latex (source))));
  }

  tree content (tree children, bool preserve, string mode, string parent) {
    tree cleaned= html_import_whitespace (mode, children, preserve);
    tree converted (TUPLE);
    for (int i=0; i<N(cleaned); ++i)
      converted << run (cleaned[i], preserve || mode == "pre", parent);
    return html_import_serial (converted, preserve || mode == "pre");
  }

  tree label (tree attrs, tree body, const char* key="id") {
    string id= attribute (attrs, key);
    return N(id) ? insert_label (body, tuple ("label", text (id))) : body;
  }

  tree block (tree body) {
    return node_is (body, "document") ? body : tuple ("document", body);
  }

  tree table (tree source, bool preserve) {
    tree plan= html_import_table_plan (source), cells (TUPLE);
    for (int i=0; i<N(plan[0]); ++i) {
      tree row (TUPLE);
      for (int j=0; j<N(plan[0][i]); ++j) {
        tree ancestry= plan[0][i][j];
        if (ancestry == tree ("#f")) row << text ("");
        else {
          tree cell= ancestry[N(ancestry)-1];
          row << label (html_attributes (cell),
                        content (element_content (cell), preserve, "mixed", element_name (cell)));
        }
      }
      cells << row;
    }
    return block (html_import_serial (html_import_table (html_attributes (source), cells, plan[1]), preserve));
  }

public:
  tree convert (tree source) { return materialize (run (source)); }

  tree run (tree source, bool preserve=false, string parent="") {
    if (string_node (source)) return source;
    string tag= element_name (source);
    if (tag == "*TOP*") return content (element_content (source), preserve, "mixed", tag);
    if (!N(tag) || tag[0] == '*' || one_of (tag, {
        "head", "title", "meta", "link", "base", "style", "script", "object",
        "param", "applet", "map", "area", "basefont", "frameset", "frame",
        "form", "input", "button", "select", "optgroup", "option", "textarea",
        "isindex", "label", "fieldset", "legend"})) return text ("");
    tree attrs= html_attributes (source), children= element_content (source);
    if (tag == "math")
      return structured (athena::document::normalize_imported_math (mathml_to_tree (source)));
    if (tag == "mathjax") {
      if (N(children) == 1 && string_node (children[0]))
        return latex (scm_unquote (children[0]->label));
      return text ("");
    }
    if (tag == "img") {
      if (attribute (attrs, "class") == "tex" && N(attribute (attrs, "alt")))
        return latex ("$\\displaystyle " * attribute (attrs, "alt") * "$");
      return label (attrs, html_import_element ("img", attrs, text ("")));
    }
    if (tag == "span" && attribute (attrs, "class") == "mwe-math-element" &&
        N(children) && element_name (children[0]) == "span")
      return run (children[0], preserve, tag);
    if (tag == "table") return table (source, preserve);
    if (tag == "br") return one_of (parent, {"ul", "ol", "dl", "menu", "dir"})
      ? text ("") : tuple ("next-line");
    if (tag == "hr") return block (label (attrs, tuple ("hrule")));

    bool list= one_of (tag, {"ul", "ol", "dl", "menu", "dir"});
    bool is_block= list || one_of (tag, {"div", "p", "pre", "blockquote", "li", "dt",
      "dd", "address", "center", "iframe", "h1", "h2", "h3", "h4", "h5", "h6"});
    string mode= tag == "pre" ? "pre" : list ? "element" :
      (is_block || tag == "html" || tag == "body" || tag == "a" ||
       tag == "sub" || tag == "sup") ? "mixed" : "collapse";
    tree body= content (children, preserve, mode, tag);
    if (tag == "a")
      return html_import_element ("a", attrs, label (attrs, label (attrs, body, "name")));
    if (tag == "font")
      return html_import_element ("font-size", attrs,
        html_import_element ("font-color", attrs, label (attrs, body)));
    if (tag == "sub" || tag == "sup") {
      tree script= materialize (label (attrs, body));
      return structured (athena::math_v3::scripts (
        "", "", "", tag == "sub" ? script : tree (""),
        tag == "sup" ? script : tree (""), 'r'));
    }
    if (tag == "span" && attribute (attrs, "class") == "texhtml")
      return tuple ("math", body);
    if (tag == "li") {
      tree parts (TUPLE);
      parts << tuple ("document", tuple ("item")) << body;
      return block (label (attrs, html_import_serial (parts, preserve)));
    }
    if (list) {
      body= html_import_element ("list-body", tree (TUPLE), block (body));
      string name= tag == "ol" ? "enumerate" : tag == "dl" ? "description" : "itemize";
      return block (tuple (name, label (attrs, body)));
    }
    if (tag == "q") return label (attrs, inline_run ({text ("``"), body, text ("''")}, preserve, false));
    if (tag == "pre" || tag == "blockquote")
      return block (tuple (tag == "pre" ? "code" : "quotation", label (attrs, block (body))));
    if (tag == "center") return block (tuple ("with", text ("par-mode"), text ("center"), label (attrs, body)));

    struct mapping { const char* html; const char* native; };
    static const mapping wrappers[]= {
      {"h1", "chapter*"}, {"h2", "section*"}, {"h3", "subsection*"},
      {"h4", "subsubsection*"}, {"h5", "paragraph*"}, {"h6", "subparagraph*"},
      {"em", "em"}, {"strong", "strong"}, {"cite", "cite*"}, {"dfn", "dfn"},
      {"code", "code*"}, {"samp", "samp"}, {"kbd", "kbd"}, {"var", "var"},
      {"abbr", "abbr"}, {"acronym", "acronym"}, {"u", "underline"}, {"dt", "item*"}
    };
    for (const auto& entry: wrappers) if (tag == entry.html) {
      tree wrapped= tuple (entry.native, label (attrs, body));
      return is_block ? block (wrapped) : node_is (body, "document") ? block (wrapped) : wrapped;
    }
    struct style { const char* tag; const char* key; const char* value; };
    static const style styles[]= {
      {"tt", "font-family", "tt"}, {"i", "font-shape", "italic"},
      {"b", "font-series", "bold"}, {"big", "font-size", "1.2"}, {"small", "font-size", "0.83"}
    };
    for (const auto& entry: styles) if (tag == entry.tag) {
      tree wrapped= tuple ("with", text (entry.key), text (entry.value), label (attrs, body));
      return node_is (body, "document") ? block (wrapped) : wrapped;
    }
    body= label (attrs, body);
    return is_block ? block (body) : body;
  }
};
}

tree html_import_convert (scheme_tree source) {
  return html_importer ().convert (source);
}

tree html_import_document (scheme_tree source) {
  if (!is_tuple (source, "!file", 1)) return html_import_convert (source);
  tree body= html_import_convert (source[1]);
  if (!is_document (body)) body= tree (DOCUMENT, body);
  return tree (DOCUMENT, compound ("body", body), compound ("style", "generic"));
}

scheme_tree html_import_document_serial (scheme_tree source) {
  return tree_to_scheme_tree (html_import_document (source));
}

scheme_tree parse_html_document (string source) {
  return tuple ("!file", parse_html_import (source, false));
}

scheme_tree parse_html_snippet (string source) {
  return parse_html_import (source, true);
}
