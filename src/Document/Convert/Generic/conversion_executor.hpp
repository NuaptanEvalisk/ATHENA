#pragma once
#include "tree.hpp"
#include "url.hpp"
#include "Document/Convert/Generic/conversion_options.hpp"
#include <variant>

// A request owns native values on its calling thread. Text and file bytes are
// distinct, as are document trees (including node metadata) and serialized
// syntax trees. Conversion never passes these values through a runtime object.
struct conversion_bytes { string data; };
struct conversion_stree { tree data; };
using conversion_payload= std::variant<string, conversion_bytes, url, tree,
                                       conversion_stree>;

class editor_rep;
conversion_payload conversion_dispatch (conversion_payload input, string from,
                                         string to, conversion_options options= {});
conversion_payload conversion_to_file (conversion_payload input, string from,
                                        string to, url target,
                                        conversion_options options= {});
string conversion_file_format (url source);
bool conversion_file_available (url source, url target);

conversion_payload conversion_execute (conversion_payload input, string from,
                                        string to, conversion_options options,
                                        url source, url target,
                                        editor_rep* rendering_editor= nullptr,
                                        url destination= url_none ());
url conversion_source_url ();
url conversion_target_url ();
editor_rep* conversion_rendering_editor ();
string conversion_latex_document (tree input, const conversion_options& options, url source, url target);
