/******************************************************************************
* Native image conversion tools. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include "url.hpp"

tree conversion_image_routes ();
url conversion_image_run (string from, string to, url source, url destination,
                          string resolution);
bool conversion_image_file (url source, url destination);
