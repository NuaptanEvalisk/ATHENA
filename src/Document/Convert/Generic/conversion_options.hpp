/******************************************************************************
* Native conversion options. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "string.hpp"
#include <map>
#include <string>

using conversion_options= std::map<std::string, std::string>;
struct conversion_export_request {
  string format;
  conversion_options options;
};
conversion_options conversion_options_snapshot ();
string encode_conversion_export_request (const conversion_export_request& request);
conversion_export_request decode_conversion_export_request (string source);
