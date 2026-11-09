// Native source-owned slideshow pages. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"
#include "App/Runtime/thumbnail_snapshot.hpp"
#include "Document/Model/slide_page.hpp"
class editor_rep;
athena::slides::reader presentation_field_reader (editor_rep&);
std::vector<tree> presentation_field_values (editor_rep&, tree, const std::string&);

bool presentation_page_context (tree);
void presentation_ensure_pages ();
int presentation_page_count ();
int presentation_page_index ();
bool presentation_page_empty (int);
tree presentation_page_copy (int);
bool presentation_edit_page (slide_page_operation, int,
                            tree paste_content= tree (UNINIT), std::string kind= {});
void presentation_switch_to (int);
tree presentation_slide_document (tree);
void presentation_slide_title (tree);
void presentation_slide_graphics (tree);
tree presentation_slide_background ();
void presentation_set_slide_background (tree);
