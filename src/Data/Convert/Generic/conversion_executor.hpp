#pragma once
#include "tree.hpp"
#include "url.hpp"
class object;
object conversion_dispatch (object input, string from, string to, object options);
object conversion_to_file (object input, string from, string to, url target, object options);
object conversion_file (url source, url target, object options);
string conversion_file_format (url source);
bool conversion_file_available (url source, url target);

object conversion_execute (object input, string from, string to,
                           object options, url source, url target);
url conversion_source_url ();
url conversion_target_url ();
string conversion_latex_document (tree input, object options, url source, url target);
