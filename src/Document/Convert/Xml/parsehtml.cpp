
/******************************************************************************
* MODULE     : parsehtml.cpp
* DESCRIPTION: HTML5 DOM parsing with self-contained MathJax payloads
* COPYRIGHT  : (C) 2019 Joris van der Hoeven, (C) 2026 ATHENA authors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "convert.hpp"
#include "parse_string.hpp"
#include <lexbor/html/html.h>
#include <lexbor/html/interfaces/template_element.h>
#include <lexbor/dom/interfaces/document_type.h>
#include <lexbor/dom/interfaces/character_data.h>
#include <memory>
#include <stdexcept>

namespace {
using html_document = std::unique_ptr<lxb_html_document_t,
                                     decltype(&lxb_html_document_destroy)>;

string text (const lxb_char_t* p, size_t n) {
  return n ? string ((const char*) p, (int) n) : string ("");
}

string characters (lxb_dom_node_t* node) {
  auto* data= lxb_dom_interface_character_data (node);
  return text (data->data.data, data->data.length);
}

/******************************************************************************
* MathJax extension
******************************************************************************/

bool contains_mathjax (lxb_dom_node_t* node) {
  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
      node->ns == LXB_NS_HTML && node->local_name == LXB_TAG_SCRIPT) {
    size_t len= 0;
    auto* src= lxb_dom_element_get_attribute (
      lxb_dom_interface_element (node), (const lxb_char_t*) "src", 3, &len);
    if (occurs ("mathjax", locase_all (text (src, len)))) return true;
  }
  for (auto* child= node->first_child; child; child= child->next)
    if (contains_mathjax (child)) return true;
  return false;
}

bool
get_mathjax (string s, int& i, string close) {
  while (i < N(s)) {
    string expect= "";
    if (read (s, i, "$$")) {
      if (close == "$$") return true;
      else expect= "$$";
    }
    else if (read (s, i, "$")) {
      if (close == "$") return true;
      else expect= "$";
    }
    else if (read (s, i, "\\(")) expect= "\\)";
    else if (read (s, i, "\\[")) expect= "\\]";
    else if (read (s, i, "\\begin{equation}")) expect= "\\end{equation}";
    else if (read (s, i, "\\begin{equation*}")) expect= "\\end{equation*}";
    else if (read (s, i, "\\begin{eqnarray}")) expect= "\\end{eqnarray}";
    else if (read (s, i, "\\begin{eqnarray*}")) expect= "\\end{eqnarray*}";
    else if (test (s, i, "\\)") ||
             test (s, i, "\\]") ||
             test (s, i, "\\end{equation}") ||
             test (s, i, "\\end{equation*}") ||
             test (s, i, "\\end{eqnarray}") ||
             test (s, i, "\\end{eqnarray*}")) {
      if (!test (s, i, close)) return false;
      i += N(close);
      return true;
    }
    else if (close == "") return false;
    else i++;
    if (N(expect) != 0) {
      if (!get_mathjax (s, i, expect)) return false;
      if (close == "") return true;
    }
  }
  return false;
}

bool
acceptable_mathjax (string s) {
  if (!starts (s, "$") || starts (s, "$$")) return true;
  if (occurs ("</", s)) return false;
  if (occurs ("<span", s)) return false;
  if (occurs ("<div ", s)) return false;
  return true;
}

void append_text (tree& result, string s, bool mathjax) {
  int start= 0;
  for (int i= 0; mathjax && i < N(s);) {
    int pos= i;
    if ((s[i] == '\\' || s[i] == '$') && get_mathjax (s, i, "") &&
        acceptable_mathjax (s(pos, i))) {
      if (pos > start) result << tree (scm_quote (s(start, pos)));
      result << tuple ("mathjax", scm_quote (s(pos, i)));
      start= i;
    }
    else i= pos + 1;
  }
  if (start < N(s)) result << tree (scm_quote (s(start, N(s))));
}

void append_dom (tree& result, lxb_dom_node_t* node,
                 lxb_ns_id_t parent_ns, bool mathjax, bool import) {
  if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
    append_text (result, characters (node), mathjax);
    return;
  }
  if (node->type == LXB_DOM_NODE_TYPE_COMMENT) {
    result << tuple ("*COMMENT*", scm_quote (characters (node)));
    return;
  }
  if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_TYPE) {
    auto* dt= lxb_dom_interface_document_type (node);
    size_t len= 0;
    auto* name= lxb_dom_document_type_name (dt, &len);
    tree t= tuple ("*DOCTYPE*", text (name, len));
    if (dt->public_id.length) {
      t << tree ("PUBLIC")
        << tree (scm_quote (text (dt->public_id.data, dt->public_id.length)))
        << tree (scm_quote (text (dt->system_id.data, dt->system_id.length)));
    }
    else if (dt->system_id.length)
      t << tree ("SYSTEM")
        << tree (scm_quote (text (dt->system_id.data, dt->system_id.length)));
    result << t;
    return;
  }
  if (node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    throw std::runtime_error ("HTML parser: unsupported DOM node");
  auto* element= lxb_dom_interface_element (node);
  size_t len= 0;
  auto* name= lxb_dom_element_qualified_name (element, &len);
  string tag= text (name, len);
  if (import) {
    if (node->ns == LXB_NS_HTML) tag= "h:" * tag;
    else if (node->ns == LXB_NS_MATH) tag= "m:" * tag;
    else if (node->ns == LXB_NS_SVG) tag= "svg:" * tag;
  }
  tree t= tuple (tag), attrs= tuple ("@");
  for (auto* attr= element->first_attr; attr; attr= attr->next) {
    auto* key= lxb_dom_attr_qualified_name (attr, &len);
    string k= text (key, len);
    auto* value= lxb_dom_attr_value (attr, &len);
    // Namespace identity comes from HTML5 tree construction, not an xmlns hint.
    if (k != "xmlns" && (!import || !starts (k, "xmlns:")))
      attrs << tuple (k, scm_quote (text (value, len)));
  }
  if (node->ns != parent_ns && !import) {
    auto* uri= lxb_ns_by_id (node->owner_document->ns, node->ns, &len);
    attrs << tuple ("xmlns", scm_quote (text (uri, len)));
  }
  if (import && node->ns == LXB_NS_MATH && node->local_name == LXB_TAG_MATH)
    attrs << tuple ("xmlns:m", scm_quote ("http://www.w3.org/1998/Math/MathML"));
  if (N(attrs) > 1) t << attrs;
  bool literal= node->ns != LXB_NS_HTML ||
    node->local_name == LXB_TAG_SCRIPT || node->local_name == LXB_TAG_STYLE ||
    node->local_name == LXB_TAG_PRE || node->local_name == LXB_TAG_CODE ||
    node->local_name == LXB_TAG_TEXTAREA || t[0] == "mathjax" || t[0] == "h:mathjax";
  auto* children= node;
  if (node->ns == LXB_NS_HTML && node->local_name == LXB_TAG_TEMPLATE)
    children= lxb_dom_interface_node (lxb_html_interface_template (node)->content);
  for (auto* child= children->first_child; child; child= child->next)
    append_dom (t, child, node->ns, mathjax && !literal, import);
  result << t;
}

tree parse (string s, bool fragment, bool import=false) {
  html_document doc (lxb_html_document_create (), lxb_html_document_destroy);
  if (!doc) throw std::bad_alloc ();
  lxb_html_document_scripting_set (doc.get (), false);
  auto* root= lxb_dom_interface_node (doc.get ());
  if (fragment) {
    auto* context= lxb_dom_document_create_element (
      &doc->dom_document, (const lxb_char_t*) "body", 4, nullptr);
    if (!context) throw std::bad_alloc ();
    root= lxb_html_document_parse_fragment (
      doc.get (), context, (const lxb_char_t*) s.data (), N(s));
    if (!root) throw std::runtime_error ("HTML fragment parsing failed");
  }
  else if (lxb_html_document_parse (
             doc.get (), (const lxb_char_t*) s.data (), N(s)) != LXB_STATUS_OK)
    throw std::runtime_error ("HTML document parsing failed");
  tree result= tuple ("*TOP*");
  bool mathjax= contains_mathjax (root);
  for (auto* child= root->first_child; child; child= child->next)
    append_dom (result, child, LXB_NS_HTML, mathjax, import);
  return result;
}
} // namespace

/******************************************************************************
* Interface
******************************************************************************/

tree
parse_html (string s) {
  return parse (s, false);
}

tree parse_html_fragment (string s) { return parse (s, true); }

scheme_tree parse_html_import (string s, bool fragment) {
  return parse (s, fragment, true);
}
