/******************************************************************************
* Native HTML document export. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include "url.hpp"
class object;

tree html_export_document (object source, object options, url origin, url target);
