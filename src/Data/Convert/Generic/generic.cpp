
/******************************************************************************
* MODULE     : generic.cpp
* DESCRIPTION: routines for generic formats
* COPYRIGHT  : (C) 2000  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "convert.hpp"
#include "conversion_registry.hpp"
#include "conversion_executor.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include <stdexcept>

tree conversion_read_document (string source) {
  return athena::document::read_xml_v3 (
    std::string_view (source.data (), N(source)), athena::document::xml_kind::document);
}

tree conversion_read_fragment (string source) {
  return athena::document::read_xml_v3 (
    std::string_view (source.data (), N(source)), athena::document::xml_kind::fragment);
}

string conversion_write_document (tree source) {
  const auto result= athena::document::write_xml_v3 (source, athena::document::xml_kind::document);
  return string (result.data (), result.size ());
}

string conversion_write_fragment (tree source) {
  const auto result= athena::document::write_xml_v3 (source, athena::document::xml_kind::fragment);
  return string (result.data (), result.size ());
}

bool
is_snippet (tree doc) {
  if (!is_document (doc)) return true;
  int i, n= N(doc);
  for (i=0; i<n; i++)
    if (is_compound (doc[i], "body", 1) ||
        is_compound (doc[i], "TeXmacs", 1))
      return false;
  return true;
}

string
suffix_to_format (string suffix) {
  return conversion_format_from_suffix (suffix);
}

string
format_to_suffix (string fm) {
  return conversion_format_default_suffix (fm);
}

string
get_format (string s, string suffix) {
  return conversion_format_determine (s, suffix);
}

bool
format_exists (string format) {
  return conversion_format_exists (format);
}

tree
generic_to_tree (string s, string fm) {
  object result= conversion_execute (object (s), fm, "texmacs-tree", null_object (),
                                    conversion_source_url (), conversion_target_url ());
  if (!is_tree (result))
    throw std::runtime_error ("Native conversion did not produce a document tree");
  return as_tree (result);
}

string
tree_to_generic (tree doc, string fm) {
  object result= conversion_execute (object (doc), "texmacs-tree", fm, null_object (),
                                    conversion_source_url (), conversion_target_url ());
  if (!is_string (result))
    throw std::runtime_error ("Native conversion did not produce serialized text");
  return as_string (result);
}
