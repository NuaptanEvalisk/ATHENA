#pragma once
#include "tree.hpp"

tree html_import_convert (scheme_tree source);
tree html_import_document (scheme_tree source);
scheme_tree html_import_document_serial (scheme_tree source);
scheme_tree parse_html_document (string source);
scheme_tree parse_html_snippet (string source);
