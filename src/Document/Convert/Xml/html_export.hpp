/******************************************************************************
* Native HTML document export. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include "url.hpp"
#include "Document/Convert/Generic/conversion_options.hpp"
class editor_rep;

// Source is the prepared serialized tree, not a native document tree.
tree html_export_document (tree source, const conversion_options& options, url origin, url target,
                           editor_rep* rendering_editor= nullptr);
