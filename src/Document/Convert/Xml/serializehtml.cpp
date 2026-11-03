/******************************************************************************
* MODULE     : serializehtml.cpp
* DESCRIPTION: HTML SXML to a Lexbor DOM and HTML5 serialization
* COPYRIGHT  : (C) 2026 ATHENA authors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "convert.hpp"
#include <lexbor/html/html.h>
#include <lexbor/html/serialize.h>
#include <lexbor/html/interfaces/template_element.h>
#include <lexbor/dom/interfaces/document_type.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespaces= std::map<std::string, string>;
const string html_ns= "http://www.w3.org/1999/xhtml";
const string math_ns= "http://www.w3.org/1998/Math/MathML";
const string svg_ns= "http://www.w3.org/2000/svg";

const lxb_char_t* bytes (const string& s) {
  return reinterpret_cast<const lxb_char_t*> (s.data ());
}

bool head (tree t, string name) {
  return is_tuple (t) && N(t) > 0 && t[0] == name;
}

[[noreturn]] void invalid (string path, const char* message) {
  throw std::runtime_error (std::string ("HTML serialization at ") +
                            path.c_str () + ": " + message);
}

string atom (tree t, string path) {
  if (!is_atomic (t)) invalid (path, "expected an atomic value");
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

void append (lxb_dom_document_t* doc, lxb_dom_node_t* parent, tree t,
             namespaces ns, string path) {
  if (is_atomic (t)) {
    if (!is_quoted (t->label)) invalid (path, "text must be a string");
    string s= scm_unquote (t->label);
    auto* text= lxb_dom_document_create_text_node (doc, bytes (s), N(s));
    if (!text) throw std::bad_alloc ();
    lxb_dom_node_insert_child (parent, lxb_dom_interface_node (text));
    return;
  }
  if (!is_tuple (t)) invalid (path, "expected an SXML list");
  if (N(t) == 0) return;
  string tag= atom (t[0], path);
  if (tag == "*TOP*" || tag == "!concat") {
    for (int i= 1; i < N(t); ++i)
      append (doc, parent, t[i], ns, path * "/" * as_string (i));
    return;
  }
  lxb_dom_node_t* node= nullptr;
  if (tag == "*DOCTYPE*") {
    if (N(t) != 2 && N(t) != 4 && N(t) != 5)
      invalid (path, "invalid doctype");
    string name= atom (t[1], path), pub, sys;
    if (N(t) == 4 && t[2] == "SYSTEM") sys= atom (t[3], path);
    else if (N(t) == 5 && t[2] == "PUBLIC") {
      pub= atom (t[3], path); sys= atom (t[4], path);
    }
    else if (N(t) != 2) invalid (path, "invalid doctype identifiers");
    node= lxb_dom_interface_node (lxb_dom_document_type_create (
      doc, bytes (name), N(name), bytes (pub), N(pub), bytes (sys), N(sys), nullptr));
  }
  else if (tag == "*COMMENT*") {
    if (N(t) != 2) invalid (path, "invalid comment");
    string s= atom (t[1], path);
    node= lxb_dom_interface_node (lxb_dom_document_create_comment (doc, bytes (s), N(s)));
  }
  else if (tag == "*PI*") {
    if (N(t) != 3) invalid (path, "invalid processing instruction");
    string target= atom (t[1], path), s= atom (t[2], path);
    node= lxb_dom_interface_node (lxb_dom_document_create_processing_instruction (
      doc, bytes (target), N(target), bytes (s), N(s)));
  }
  else {
    if (N(tag) == 0 || tag[0] == '*' || tag == "@")
      invalid (path, "unsupported SXML control node");
    if (lxb_html_tree_mathml_text_integration_point (parent) &&
        tag != "mglyph" && tag != "malignmark") ns[""]= html_ns;
    tree attrs= N(t) > 1 && head (t[1], "@") ? t[1] : tuple ("@");
    for (int i= 1; i < N(attrs); ++i) {
      if (!is_tuple (attrs[i]) || N(attrs[i]) != 2)
        invalid (path, "invalid attribute");
      string key= atom (attrs[i][0], path), value= atom (attrs[i][1], path);
      if (key == "xmlns") ns[""]= value;
      else if (starts (key, "xmlns:")) ns[key(6, N(key)).c_str ()]= value;
    }
    string local= tag, prefix, uri= ns[""];
    int colon= search_forwards (":", 0, tag);
    if (colon >= 0) {
      prefix= tag(0, colon); local= tag(colon + 1, N(tag));
      auto found= ns.find (prefix.c_str ());
      if (found == ns.end ()) invalid (path, "undeclared namespace prefix");
      uri= found->second;
    }
    else if (tag == "math") uri= math_ns;
    else if (tag == "svg") uri= svg_ns;
    bool html_vocabulary= uri == html_ns || uri == math_ns || uri == svg_ns;
    auto* element= lxb_dom_element_create (
      doc, bytes (local), N(local), bytes (uri), N(uri),
      html_vocabulary || N(prefix) == 0 ? nullptr : bytes (prefix),
      html_vocabulary ? 0 : N(prefix),
      nullptr, 0, false);
    if (!element) invalid (path, "cannot create element");
    node= lxb_dom_interface_node (element);
    for (int i= 1; i < N(attrs); ++i) {
      string key= atom (attrs[i][0], path), value= atom (attrs[i][1], path);
      if (value == "<implicit>") value= "";
      if (!lxb_dom_element_set_attribute (
            element, bytes (key), N(key), bytes (value), N(value)))
        invalid (path, "cannot create attribute");
    }
    int start= N(t) > 1 && head (t[1], "@") ? 2 : 1;
    if (lxb_html_node_is_void (node) && N(t) > start)
      invalid (path, "a void element cannot have children");
    ns[""]= uri;
    if (lxb_html_tree_html_integration_point (node)) ns[""]= html_ns;
    auto* container= node;
    if (node->ns == LXB_NS_HTML && node->local_name == LXB_TAG_TEMPLATE)
      container= lxb_dom_interface_node (lxb_html_interface_template (node)->content);
    for (int i= start; i < N(t); ++i)
      append (doc, container, t[i], ns, path * "/" * as_string (i));
  }
  if (!node) throw std::bad_alloc ();
  lxb_dom_node_insert_child (parent, node);
}

lxb_status_t collect (const lxb_char_t* data, size_t len, void* context) {
  *static_cast<string*> (context) << string ((const char*) data, (int) len);
  return LXB_STATUS_OK;
}
} // namespace

string serialize_html (tree t) {
  std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)>
    doc (lxb_html_document_create (), lxb_html_document_destroy);
  if (!doc) throw std::bad_alloc ();
  auto* root= lxb_dom_interface_node (doc.get ());
  append (&doc->dom_document, root, t,
          {{"", html_ns}, {"h", html_ns}, {"m", math_ns}, {"svg", svg_ns}}, "root");
  string output;
  for (auto* node= root->first_child; node; node= node->next) {
    // The regular serializer deliberately omits PUBLIC/SYSTEM identifiers.
    auto status= node->type == LXB_DOM_NODE_TYPE_DOCUMENT_TYPE
      ? lxb_html_serialize_pretty_cb (node, LXB_HTML_SERIALIZE_OPT_FULL_DOCTYPE,
                                      0, collect, &output)
      : lxb_html_serialize_tree_cb (node, collect, &output);
    if (status != LXB_STATUS_OK) throw std::runtime_error ("HTML serialization failed");
  }
  return output;
}
