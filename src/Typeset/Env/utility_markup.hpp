// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"
string markup_version_release (tree type);
string markup_language_suffix ();
tree markup_map (tree function, tree tuple);
tree markup_concat_tuple (tree tuple, tree separator, tree final_separator);
tree markup_select (tree body, tree arguments);
tree markup_reference (tree body, tree arguments);
string markup_reference_arity (tree body, tree arguments);
string markup_reference_index (tree body, tree arguments);
string markup_reference_last (tree body, tree arguments);
tree markup_apply_paragraphs (tree macro, tree body);
tree markup_numbered (tree body);
tree markup_listing (tree body);
