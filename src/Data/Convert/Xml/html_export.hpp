/******************************************************************************
* Native HTML document export. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include "url.hpp"
class object;
class editor_rep;

tree html_export_document (object source, object options, url origin, url target,
                           editor_rep* rendering_editor= nullptr);
