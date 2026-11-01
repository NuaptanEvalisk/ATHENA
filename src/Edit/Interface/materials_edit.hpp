// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_MATERIALS_EDIT_HPP
#define ATHENA_MATERIALS_EDIT_HPP
#include "scheme.hpp"

void materials_insert_citation ();
void materials_insert_references ();
void materials_update_current_document ();
void materials_append_references ();
void materials_set_reference_style (string style);
void materials_set_document_style (string style);
void materials_use_default_style ();
string materials_page_title (string name, object document);
bool materials_page_permission (string name, string kind);
#endif
