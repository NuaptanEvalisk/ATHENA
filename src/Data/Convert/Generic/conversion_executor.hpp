#pragma once
#include "tree.hpp"
#include "url.hpp"
#include "conversion_options.hpp"
class object;
class editor_rep;
object conversion_dispatch (object input, string from, string to, object options);
object conversion_dispatch (object input, string from, string to, conversion_options options);
object conversion_to_file (object input, string from, string to, url target, object options);
object conversion_file (url source, url target, object options);
string conversion_file_format (url source);
bool conversion_file_available (url source, url target);

object conversion_execute (object input, string from, string to,
                           object options, url source, url target,
                           editor_rep* rendering_editor= nullptr);
object conversion_execute (object input, string from, string to,
                           conversion_options options, url source, url target,
                           editor_rep* rendering_editor= nullptr,
                           url destination= url_none ());
url conversion_source_url ();
url conversion_target_url ();
editor_rep* conversion_rendering_editor ();
string conversion_latex_document (tree input, const conversion_options& options, url source, url target);
