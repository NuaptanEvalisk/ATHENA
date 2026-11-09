// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include <string>
using editor_mode_predicate= bool (*) ();
editor_mode_predicate editor_mode_resolve (const std::string& name);
bool editor_mode_source ();
bool editor_mode_text ();
bool editor_mode_math ();
bool editor_mode_program ();
bool editor_mode_table ();
bool editor_mode_standard ();
bool editor_mode_standard_text ();
bool editor_mode_smart_reference ();
bool editor_mode_verbatim ();
bool editor_mode_variants_disabled ();
bool editor_mode_emacs ();
