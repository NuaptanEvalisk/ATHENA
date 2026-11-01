// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "url.hpp"
void document_print_with_cover (url target);
void document_preview_file (url target);
void document_preview ();
void document_prepared_print (url target);
void document_prepared_print_pages (string first, string last);
void document_print_file_dialog ();
void document_print_pages_dialog ();
void document_print_pages_file_dialog ();
void document_print ();
void document_print_direct ();
void document_print_interactive ();
