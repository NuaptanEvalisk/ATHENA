// Native source-owned slideshow pages. GPL-3.0-or-later.
#include "Editor/Commands/presentation_commands.hpp"
#include "Editor/editor.hpp"
#include "Editor/Commands/native_graphics_commands.hpp"
#include "Editor/Commands/generic_editor_commands.hpp"
#include "App/Commands/native_interfaces.hpp"
#include "node_metadata.hpp"
#include "tree_traverse.hpp"
#include <algorithm>

namespace {
tree page_body () { return get_current_editor ()->the_buffer (); }

int page_offset (tree body) {
  return is_document (body) && N(body) &&
    (is_compound (body[0], "hide-preamble") ||
     is_compound (body[0], "show-preamble")) ? 1 : 0;
}

tree new_page (tree body) {
  tree result (SLIDE_PAGE, body);
  athena::node::metadata metadata;
  metadata.id= athena::node::new_id ();
  athena::node::set (result, metadata);
  return result;
}

tree current_page () {
  editor ed= get_current_editor ();
  for (path p= path_up (ed->the_path ()); !is_nil (p); p= path_up (p)) {
    tree t= ed->the_subtree (p);
    if (is_func (t, SLIDE_PAGE, 1)) return t;
  }
  return tree (UNINIT);
}

void go (tree t) {
  if (t == tree (UNINIT)) return;
  get_current_editor ()->go_to_start (reverse (obtain_ip (t)));
}

bool empty_content (tree t) {
  if (is_atomic (t)) return t == "";
  if (!is_document (t) && !is_concat (t) &&
      !is_compound (t, "tit") && !is_func (t, SLIDE_PAGE)) return false;
  for (int i=0; i<N(t); ++i)
    if (!empty_content (t[i])) return false;
  return true;
}
}

bool presentation_page_context (tree t) { return is_func (t, SLIDE_PAGE, 1); }

int presentation_page_count () {
  tree body= page_body ();
  int offset= page_offset (body);
  return is_document (body) && N(body)>offset &&
    is_func (body[offset], SLIDE_PAGE) ? N(body)-offset : 0;
}

void presentation_ensure_pages () {
  editor ed= get_current_editor ();
  tree body= page_body ();
  int offset= page_offset (body);
  if (is_document (body) && N(body)>offset &&
      is_func (body[offset], SLIDE_PAGE)) return;
  tree content (DOCUMENT);
  if (is_document (body))
    for (int i=offset; i<N(body); ++i) content << body[i];
  else content << body;
  if (N(content) == 0) content << tree ("");
  tree replacement (DOCUMENT);
  if (offset) replacement << body[0];
  replacement << new_page (content);
  athena::node::copy_metadata (body, replacement);
  path root= reverse (obtain_ip (body));
  path cursor= ed->the_path () / root;
  body= tree_assign (body, replacement);
  if (!is_nil (cursor) && cursor->item >= offset)
    ed->go_to (root * offset * 0 * path (cursor->item-offset, cursor->next));
  else go (body[offset][0]);
}

int presentation_page_index () {
  tree page= current_page ();
  if (page == tree (UNINIT)) return 0;
  return obtain_ip (page)->item - page_offset (page_body ());
}

tree presentation_page_copy (int index) {
  tree body= page_body ();
  const int at= index + page_offset (body);
  if (index < 0 || index >= presentation_page_count ()) return tree (UNINIT);
  return copy (body[at]);
}

bool presentation_page_empty (int index) {
  return empty_content (presentation_page_copy (index));
}

void presentation_switch_to (int index) {
  if (index < 0 || index >= presentation_page_count ()) return;
  tree body= page_body ();
  go (body[index + page_offset (body)][0]);
}

bool presentation_edit_page (slide_page_operation operation, int index,
                             tree paste_content) {
  tree body= page_body ();
  int count= presentation_page_count ();
  if (count == 0) return false;
  if (index < 0) index= presentation_page_index ();
  if (index < 0 || index >= count) return false;
  const int offset= page_offset (body);
  const int at= index + offset;
  if (operation == slide_page_operation::erase) {
    if (count == 1) {
      tree_assign (body[at][0], tree (DOCUMENT, ""));
      go (body[at][0]);
    }
    else {
      tree_remove (body, at, 1);
      presentation_switch_to (std::min (index, count-2));
    }
    return true;
  }
  if (operation == slide_page_operation::copy) return true;
  if (operation == slide_page_operation::move_previous ||
      operation == slide_page_operation::move_next) {
    const int target= at + (operation == slide_page_operation::move_previous ? -1 : 1);
    if (target < offset || target >= N(body)) return false;
    tree moved= copy (body[at]);
    tree_remove (body, at, 1);
    tree_insert (body, target, tree (TUPLE, moved));
    go (body[target][0]);
    return true;
  }
  tree page (UNINIT);
  if (operation == slide_page_operation::duplicate)
    page= tree_duplicate_source (body[at]);
  else if (operation == slide_page_operation::paste_before ||
           operation == slide_page_operation::paste_after) {
    if (!is_func (paste_content, SLIDE_PAGE, 1)) return false;
    page= tree_duplicate_source (paste_content);
  }
  else page= new_page (tree (DOCUMENT, ""));
  const bool before= operation == slide_page_operation::insert_before ||
                     operation == slide_page_operation::paste_before;
  const int insert= at + (before ? 0 : 1);
  tree_insert (body, insert, tree (TUPLE, page));
  go (body[insert][0]);
  return true;
}

tree presentation_slide_document (tree t) {
  return is_func (t, SLIDE_PAGE, 1) ? t[0] : tree (UNINIT);
}

void presentation_slide_title (tree t) {
  tree doc= presentation_slide_document (t);
  if (!is_document (doc)) return;
  if (N(doc) && is_compound (doc[0], "tit")) { go (doc[0][0]); return; }
  tree_insert (doc, 0, tree (TUPLE, compound ("tit", "")));
  go (doc[0][0]);
}

void presentation_slide_graphics (tree t) {
  tree doc= presentation_slide_document (t);
  if (!is_document (doc)) return;
  tree_insert (doc, N(doc), tree (TUPLE, ""));
  go (doc[N(doc)-1]);
  graphics_insert (tree (TUPLE,
    "gr-mode", tree (TUPLE, "hand-edit", "penscript"),
    "gr-frame", tree (TUPLE, "scale", "1cm", tree (TUPLE, "0gw", "1gh")),
    "gr-geometry", tree (TUPLE, "geometry", "1gpar", "1gpag", "axis")));
}

tree presentation_slide_background () {
  tree page= current_page ();
  const auto* meta= athena::node::get (page);
  if (meta) {
    auto it= meta->properties.find ("background");
    if (it != meta->properties.end ())
      if (auto value= std::get_if<std::string> (&it->second.data))
        return string (value->data (), value->size ());
  }
  return "";
}
void presentation_set_slide_background (tree color) {
  tree page= current_page ();
  if (page == tree (UNINIT) || !is_atomic (color)) return;
  tree replacement= copy (page);
  auto* meta= athena::node::edit (replacement);
  if (color == "") meta->properties.erase ("background");
  else meta->properties["background"]= athena::node::property (
    std::string (color->label.data (), N(color->label)));
  tree_assign (page, replacement);
}
