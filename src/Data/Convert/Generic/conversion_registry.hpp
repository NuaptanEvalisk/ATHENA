/******************************************************************************
* Native conversion registry. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"

tree conversion_format_name (string id);
tree conversion_format_suffixes (string id);
bool conversion_format_hidden (string id);
bool conversion_format_image (string id);
tree conversion_image_formats ();
array<string> conversion_format_ids ();
string conversion_format_from_suffix (string suffix);
string conversion_format_default_suffix (string id);
bool conversion_format_exists (string id);
bool conversion_format_recognizes (string source, string id);
string conversion_format_determine (string source, string suffix);
tree conversion_read_document (string source);
tree conversion_read_fragment (string source);
string conversion_write_document (tree source);
string conversion_write_fragment (tree source);
