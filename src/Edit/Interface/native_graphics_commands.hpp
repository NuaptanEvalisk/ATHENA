// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
#include <vector>

tree graphics_default_property (string name);
const std::vector<string>& graphics_property_names ();
tree graphics_object_attribute (tree object, string name);
void graphics_set_object_attribute (tree object, string name, tree value);
string graphics_vertical_alignment (tree object);
void graphics_insert (tree properties);
void graphics_make (object properties);
void graphics_update_proviso (tree source, string value);
void graphics_rotate (double angle, bool xz);
void graphics_grid_subdivisions (int divisions);
