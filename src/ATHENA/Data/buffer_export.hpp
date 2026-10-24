/******************************************************************************
* Explicit buffer export options. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include "url.hpp"
#include "Data/Convert/Generic/conversion_options.hpp"
class editor_rep;

bool buffer_export_with_options (url name, url dest, string format, object overrides);
bool buffer_export_request (url name, url dest, string format,
                            const conversion_options& options);
bool export_tree_with_options (tree document, url dest, string format, url source,
                               const conversion_options& options,
                               editor_rep* rendering_editor= nullptr);
