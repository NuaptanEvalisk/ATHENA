// Native source-owned slideshow pages. GPL-3.0-or-later.
#include "Editor/Commands/presentation_commands.hpp"
#include "Editor/editor.hpp"
#include "Editor/Commands/native_graphics_commands.hpp"
#include "Editor/Commands/generic_editor_commands.hpp"
#include "App/Commands/native_interfaces.hpp"
#include "node_metadata.hpp"
#include "Document/Model/slide_page.hpp"
#include "tree_traverse.hpp"
#include <algorithm>

namespace {
tree page_body () { return get_current_editor ()->the_buffer (); }

std::vector<tree> page_list () {
  return athena::slides::source_pages (page_body ()).pages;
}

athena::slides::reader field_reader () {
  return [ed=get_current_editor ()] (string key) mutable { return ed->get_init_value (key); };
}

int page_offset (tree body) {
  return is_document (body) && N(body) &&
    (is_compound (body[0], "hide-preamble") ||
     is_compound (body[0], "show-preamble")) ? 1 : 0;
}

tree new_page (tree body, std::string kind= {}) {
  if (kind.empty ()) {
    const tree initial= get_current_editor ()->get_init_value ("slide-default-type");
    if (initial != tree (UNINIT)) kind= athena::slides::text (initial);
  }
  if (!kind.empty ()) (void) athena::slides::definition (kind, field_reader ());
  tree result (SLIDE_PAGE, body);
  athena::node::metadata metadata;
  metadata.id= athena::node::new_id ();
  if (!kind.empty ()) metadata.properties["kind"]= athena::node::property (kind);
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

athena::slides::reader presentation_field_reader (editor_rep& ed) {
  return [&ed] (string key) { return ed.get_init_value (key); };
}

std::vector<tree> presentation_field_values (editor_rep& ed, tree source,
                                            const std::string& key) {
  const auto read= presentation_field_reader (ed);
  for (const auto& f: athena::slides::definition (athena::slides::kind (source), read).fields)
    if (f.key == key) return {athena::slides::value (source, f, read)};
  return {};
}

int presentation_page_count () {
  return (int) page_list ().size ();
}

void presentation_ensure_pages () {
  editor ed= get_current_editor ();
  tree body= page_body ();
  int offset= page_offset (body);
  if (!page_list ().empty ()) return;
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
  auto pages= page_list ();
  for (size_t i=0; i<pages.size (); ++i)
    if (obtain_ip (pages[i]) == obtain_ip (page)) return (int) i;
  return 0;
}

tree presentation_page_copy (int index) {
  const auto pages= page_list ();
  if (index < 0 || index >= (int) pages.size ()) return tree (UNINIT);
  return copy (pages[index]);
}

bool presentation_page_empty (int index) {
  const tree page= presentation_page_copy (index);
  if (!empty_content (page)) return false;
  if (const auto* meta= athena::node::get (page))
    for (const auto& [key, property]: meta->properties)
      if (const auto* value= std::get_if<athena::node::rich_text> (&property.data))
        if (!empty_content (value->content)) return false;
  return true;
}

void presentation_switch_to (int index) {
  const auto pages= page_list ();
  if (index < 0 || index >= (int) pages.size ()) return;
  go (pages[index][0]);
}

bool presentation_edit_page (slide_page_operation operation, int index,
                             tree paste_content, std::string kind) {
  auto pages= page_list ();
  const int count= (int) pages.size ();
  if (count == 0) return false;
  if (index < 0) index= presentation_page_index ();
  if (index < 0 || index >= count) return false;
  const path page_ip= obtain_ip (pages[index]);
  tree body= get_current_editor ()->the_subtree (reverse (page_ip->next));
  const int at= page_ip->item;
  if (operation == slide_page_operation::erase) {
    if (count == 1) {
      tree replacement= copy (body[at]);
      replacement[0]= tree (DOCUMENT, "");
      if (auto* meta= athena::node::edit (replacement)) {
        for (auto it= meta->properties.begin (); it != meta->properties.end ();)
          if (std::holds_alternative<athena::node::rich_text> (it->second.data))
            it= meta->properties.erase (it);
          else ++it;
      }
      tree_assign (body[at], replacement);
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
    const bool next= operation == slide_page_operation::move_next;
    const int target= index + (next ? 1 : -1);
    if (target < 0 || target >= count) return false;
    const path target_ip= obtain_ip (pages[target]);
    tree destination= get_current_editor ()->the_subtree (reverse (target_ip->next));
    const int insert= target_ip->item +
      (next && page_ip->next != target_ip->next ? 1 : 0);
    tree moved= copy (body[at]);
    tree_remove (body, at, 1);
    tree_insert (destination, insert, tree (TUPLE, moved));
    go (destination[insert][0]);
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
  else {
    if (kind.empty ()) kind= athena::slides::kind (body[at]);
    page= new_page (tree (DOCUMENT, ""), kind);
  }
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
  const auto fields= athena::slides::fields (t, field_reader ());
  if (!fields.empty ()) {
    go (t[0]);
    get_current_editor ()->open_enunciation_slot (string (fields[0].c_str ()), 0);
    return;
  }
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
