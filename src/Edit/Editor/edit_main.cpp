
/******************************************************************************
* MODULE     : editor.cpp
* DESCRIPTION: routines for the editor
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_main.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "printer.hpp"
#include "Pdf/pdf_hummus_renderer.hpp"
#include "convert.hpp"
#include "Data/Convert/Verbatim/verbatim.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "typesetter.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"
#include "cursor_navigation.hpp"
#include "message.hpp"
#include <setjmp.h>
#include <stdexcept>
#include "image_files.hpp"
#include "Data/Convert/Generic/image_conversion.hpp"
#include "iterator.hpp"
#include "actor_ui_bridge.hpp"
#include "scheme_execution_context.hpp"
#include "tm_frame.hpp"
#include "iterator.hpp"

#ifdef EXPERIMENTAL
#include "../../Style/Memorizer/clean_copy.hpp"
#endif


#ifdef QTTEXMACS
#include "Qt/qt_gui.hpp"
#include "Qt/qt_utilities.hpp"
#endif

/******************************************************************************
* Constructors and destructor
******************************************************************************/

editor_rep::editor_rep ():
  simple_widget_rep (), cvw (NULL), mvw (NULL),
  runtime_view_id (ATHENA_NO_VIEW), ui_endpoint (nullptr),
  et (current_document_tree ()), rp (),
  progressive_typeset_pending (false), progressive_typeset_continue (false) {
  std_warning << "warning, this virtual constructor should never be called\n";
}

editor_rep::editor_rep (server_rep* sv2, buffer_document_state* buf2):
  simple_widget_rep (), sv (sv2), cvw (NULL), mvw (NULL),
  runtime_view_id (ATHENA_NO_VIEW), ui_endpoint (nullptr),
  buf (buf2),
  et (buf2->document), rp (buf2->root_path),
  progressive_typeset_pending (false), progressive_typeset_continue (false) {}

void
editor_rep::schedule_progressive_typeset () {
  if (!progressive_typeset_pending || progressive_typeset_continue) return;
  progressive_typeset_continue= true;
  notify_change (THE_TREE | THE_EXTENTS | THE_FREEZE);
}

void
editor_rep::remember_cursor_display (path bp) {
  cursor_display_root= is_nil (bp) ? box () : eb;
  cursor_display_path= bp;
  cursor_display_source= tp;
}

cursor
editor_rep::current_display_cursor (athena::text::caret_affinity affinity) {
  if (cursor_display_root == eb && cursor_display_source == tp &&
      !is_nil (cursor_display_path)) {
    cursor_display_path= eb->with_cursor_affinity (cursor_display_path, affinity);
    return eb->find_cursor (cursor_display_path);
  }
  bool found;
  path bp;
  if (!is_nil (cursor_display_root) && cursor_display_source == tp &&
      !is_nil (cursor_display_path)) {
    const auto boundary= cursor_display_root->find_source_boundary (cursor_display_path);
    if (boundary && cursor_occurrence_admitted (tp, boundary))
      bp= eb->find_occurrence_box_path (tp, boundary, found);
  }
  if (is_nil (bp)) bp= eb->find_box_path (tp, found);
  bp= eb->with_cursor_affinity (bp, affinity);
  cursor result= eb->find_cursor (bp);
  result->valid= found;
  remember_cursor_display (found ? bp : path ());
  return result;
}

athena::contracts::evaluation_boundary_handle
editor_rep::cursor_source_boundary () {
  if (cursor_display_root != eb || cursor_display_source != tp ||
      is_nil (cursor_display_path)) return {};
  return cursor_display_root->find_source_boundary (cursor_display_path);
}

std::shared_ptr<const evaluation_environment_checkpoint>
editor_rep::cursor_context_at (path source) {
  if (cursor_display_root == eb && cursor_display_source == tp &&
      !is_nil (cursor_display_path))
    return display_context_at (source, cursor_display_path);
  return cursor_context_at (source,
    path_up (source) <= path_up (tp) ? cursor_source_boundary () : nullptr);
}

std::shared_ptr<const evaluation_environment_checkpoint>
editor_rep::display_context_at (path source, path display) {
  const path anchor= eb->find_tree_path (display);
  box displayed= eb;
  path remaining= display;
  while (true) {
    const auto& boundary= displayed->source_boundary;
    if (boundary && cursor_occurrence_admitted (source, boundary))
      return occurrence_context_at (source, anchor, boundary);
    if (is_nil (remaining) || displayed->subnr () == 0) break;
    displayed= displayed[remaining->item];
    remaining= remaining->next;
  }
  return occurrence_context_at (source, anchor, eb->find_source_boundary (display));
}

bool
editor_rep::is_current_editor () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  return context != nullptr && context->editor == this &&
         context->view_id == runtime_view_id;
}

actor_viewport_snapshot
editor_rep::ui_viewport () const {
  ASSERT (ui_endpoint != nullptr, "editor has no UI endpoint");
  return ui_endpoint->viewport ();
}

bool
editor_rep::publish_ui (
  actor_command_kind kind, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3) {
  return ui_endpoint != nullptr &&
    ui_endpoint->publish (kind, ATHENA_NO_BLOB, argument0, argument1,
                          argument2, argument3);
}

bool
editor_rep::publish_ui_text (
  actor_command_kind kind, string text, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3) {
  return ui_endpoint != nullptr &&
    ui_endpoint->publish_text (kind, std::move (text), argument0, argument1,
                               argument2, argument3);
}

bool
editor_rep::publish_ui_text_pair (
  actor_command_kind kind, string first, string second,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  return ui_endpoint != nullptr &&
    ui_endpoint->publish_text_pair (kind, std::move (first), std::move (second),
                                    argument0, argument1, argument2, argument3);
}

edit_main_rep::edit_main_rep (server_rep* sv, buffer_document_state* buf):
  editor_rep (sv, buf), props (UNKNOWN), ed_obs (edit_observer (this))
{
#ifdef EXPERIMENTAL
  cct= copy (subtree (et, rp));
  copy_ip (subtree (et, rp), cct);
#endif
  attach_observer (subtree (et, rp), ed_obs);
  notify_change (THE_TREE);
  const cursor_navigation structural ({athena::contracts::builtin_contracts ()});
  tp= rp * structural.correct_cursor (subtree (et, rp), path (0));
}

edit_main_rep::~edit_main_rep () {
  detach_observer (subtree (et, rp), ed_obs);
#ifdef EXPERIMENTAL
  mem= memorizer ();
#endif
}

editor
new_editor (server_rep* sv, buffer_document_state* buf) {
  return tm_new<edit_main_rep> (sv, buf);
}

#ifdef NO_FAST_ALLOC
template<> void
tm_delete<editor_rep> (editor_rep* ptr) {
  if (ptr == NULL) return;
  delete ptr;
}
#else
template<> void
tm_delete<editor_rep> (editor_rep* ptr) {
  if (ptr == NULL) return;
  void *mem= ptr->derived_this ();
  ptr -> ~editor_rep ();
  fast_delete (mem);
}
#endif

/******************************************************************************
* Properties
******************************************************************************/

void
edit_main_rep::set_property (scheme_tree what, scheme_tree val) {
  props (what)= val;
}

void
edit_main_rep::set_bool_property (string what, bool val) {
  props (what)= (val? string ("true"): string ("false"));
}

void
edit_main_rep::set_int_property (string what, int val) {
  props (what)= as_tree (val);
}

void
edit_main_rep::set_string_property (string what, string val) {
  props (what)= val;
}

scheme_tree
edit_main_rep::get_property (scheme_tree what) {
  return props [what];
}

bool
edit_main_rep::get_bool_property (string what) {
  return as_bool (props [what]);
}

int
edit_main_rep::get_int_property (string what) {
  return as_int (props [what]);
}

string
edit_main_rep::get_string_property (string what) {
  return as_string (props [what]);
}

/******************************************************************************
* Global routines
******************************************************************************/

void
edit_main_rep::clear_buffer () {
  assign (rp, tree (DOCUMENT, tree ("")));
}

void
edit_main_rep::new_window () {
}

void
edit_main_rep::clone_window () {
}

void
edit_main_rep::tex_buffer () {
}

url
edit_main_rep::get_name () {
  return buf->name;
}

void
edit_main_rep::focus_on_this_editor () {
  (void) publish_ui (actor_command_kind::ui_focus_view);
}

void
edit_main_rep::notify_page_change () {
  if (ui_endpoint != nullptr && ui_viewport ().attached)
    (void) publish_ui (actor_command_kind::ui_invalidate_all);
}

string
edit_main_rep::get_metadata (string kind) {
  string var= "global-" * kind;
  string val= get_init_string (var);
  if (val != "") return val;
  const auto metadata= search_metadata (subtree (et, rp), kind, [this] (path source)
      -> std::shared_ptr<const athena::contracts::contract_scope> {
    const auto entry= editor_rep::cursor_context_at (rp * source * 0);
    return entry ? std::shared_ptr<const athena::contracts::contract_scope> (
      entry, &entry->contract_context ()) : nullptr;
  });
  val= metadata ? *metadata : string ("");
  if (val != "") return val;
  if (kind == "title") return as_string (tail (get_name ()));
#ifndef OS_MINGW
  if (kind == "author" &&
      !is_none (resolve_in_path ("finger")) &&
      !is_none (resolve_in_path ("sed"))) {
    string val= var_eval_system ("finger `whoami` | sed -e '/Name/!d' -e 's/.*Name: //'");
    if (N(val) > 1) return val;
  }
#endif
  return "";
}

/******************************************************************************
* Printing
******************************************************************************/

string printing_dpi ("600");
string printing_on ("a4");

int
edit_main_rep::nr_pages () {
  string medium = env->get_string (PAGE_MEDIUM);
  if (medium == "paper") return N (eb[0]);
  typeset_prepare ();
  env->write (PAGE_MEDIUM, "paper");
  box the_box= typeset_as_document (env, subtree (et, rp), reverse (rp));
  env->write (PAGE_MEDIUM, medium);
  return N (the_box[0]);
}

box
edit_main_rep::layout_print_document (bool& conform, int first_page) {
  athena::node_reference::export_node_destination_scope destinations;
  string medium = env->get_string (PAGE_MEDIUM);
  if (conform && (medium != "paper")) conform= false;
    // FIXME: better command for conform printing

  typeset_preamble ();
    // FIXME: when printing several files via aux buffers,
    // it seems that the style can be corrupted.  Why?

  // Set environment variables for printing

  typeset_prepare ();
  if (first_page >= 0) {
    env->write (PAGE_FIRST, as_string (first_page));
    env->style_init_env ();
  }
  env->write (DPI, printing_dpi);
  env->write (PAGE_SHOW_HF, "true");
  env->write (PAGE_SCREEN_MARGIN, "false");
  env->write (PAGE_BORDER, "none");
  if (is_func (env->read (BG_COLOR), _PATTERN))
    env->write (BG_COLOR, env->exec (env->read (BG_COLOR)));
  if (!conform) {
    env->write (PAGE_MEDIUM, "paper");
    env->write (PAGE_PRINTED, "true");
  }

  // Typeset pages for printing

  return typeset_as_document (env, subtree (et, rp), reverse (rp));
}

void
edit_main_rep::probe_print_references () {
  if (!athena::node_reference::current_export_references ())
    throw std::logic_error ("Reference probing requires a frozen export scope");
  athena::node_reference::verify_export_origin ();
  bool conform= false;
  // Layout writes derived labels, auxiliary records and attachments. A probe
  // must not mutate the source revision that the coordinator just captured.
  auto detached= [] (hashmap<string,tree> values) {
    hashmap<string,tree> result (UNINIT);
    iterator<string> keys= iterate (values);
    while (keys->busy ()) {
      const string key= keys->next ();
      result (key)= copy (values[key]);
    }
    return result;
  };
  const auto refs= get_ref (), aux= get_aux (), attachments= get_att ();
  const auto probe_refs= detached (refs), probe_aux= detached (aux), probe_att= detached (attachments);
  set_ref (probe_refs); set_aux (probe_aux); set_att (probe_att);
  auto restore= [&] { set_ref (refs); set_aux (aux); set_att (attachments); typeset_prepare (); };
  try { (void) layout_print_document (conform); }
  catch (...) { restore (); throw; }
  restore ();
}

void
edit_main_rep::print_doc (url name, bool conform, int first, int last) {
  if (suffix (name) == "ps" || suffix (name) == "eps")
    throw std::runtime_error ("PostScript export is not supported; use PDF");
  athena::node_reference::verify_export_origin ();
  athena::node_reference::export_reference_scope references;
  box the_box= layout_print_document (conform);

  references.require_ready ();

  print_box (name, the_box, conform, first, last);
}

int
edit_main_rep::print_doc_numbered (url name, int first_page,
  std::function<void(string, int, double, double)> destination_observer,
  bool defer_node_links) {
  athena::node_reference::verify_export_origin ();
  athena::node_reference::export_reference_scope references;
  bool conform= false;
  box pages= layout_print_document (conform, first_page);
  references.require_ready ();
  const int next_page= as_int (env->read (PAGE_NR)) + 1;
  print_box (name, pages, conform, 1, N (pages[0]),
             std::move (destination_observer), defer_node_links);
  return next_page;
}

void
edit_main_rep::print_box (url name, box the_box, bool conform, int first, int last,
  std::function<void(string, int, double, double)> destination_observer,
  bool defer_node_links) {

  // Determine parameters for printer

  string page_type = env->page_real_type;
  double w         = env->page_real_width;
  double h         = env->page_real_height;
  double cm        = env->as_real_length (string ("1cm"));
  bool   landsc    = env->page_landscape;
  int    dpi       = as_int (printing_dpi);
  int    start     = max (0, first-1);
  int    end       = min (N(the_box[0]), last);
  int    pages     = end-start;
  if (conform) {
    page_type= "user";
    SI bw= the_box[0][0]->w();
    SI bh= the_box[0][0]->h();
    string bws= as_string (bw) * "tmpt";
    string bhs= as_string (bh) * "tmpt";
    w= env->as_length (bws);
    h= env->as_length (bhs);
  }

  // Print pages
  renderer ren= printer (name, dpi, pages, page_type, landsc, w/cm, h/cm);
  if (!defer_node_links) {
    hashmap<string,tree> destinations (UNINIT);
    for (int i=start; i<end; ++i)
      the_box[0][i]->collect_page_numbers (destinations, "?");
    pdf_hummus_set_link_resolver (ren, [destinations] (string target) {
      return athena::node_reference::export_node_link (target, destinations);
    });
  }
  pdf_hummus_set_destination_observer (ren, std::move (destination_observer));

  if (ren->is_started ()) {
    int i;
    ren->set_metadata ("title", get_metadata ("title"));
    ren->set_metadata ("author", get_metadata ("author"));
    ren->set_metadata ("subject", get_metadata ("subject"));
    for (i=start; i<end; i++) {
      tree bg= env->read (BG_COLOR);
      ren->set_background (bg);
      if (bg != "white" && bg != "#ffffff")
        ren->clear_pattern (0, (SI) -h, (SI) w, 0);

      rectangles rs;
      the_box[0]->sx(i)= 0;
      the_box[0]->sy(i)= 0;
      the_box[0][i]->redraw (ren, path (0), rs);
      if (i<end-1) ren->next_page ();
    }
  }
  tm_delete (ren);

}

void
edit_main_rep::print_to_file (url name, string first, string last) {
  bool wait= suffix (name) == "pdf";
  if (wait)
    system_wait ("Exporting PDF", as_string (tail (name)));
  try {
    print_doc (name, false, as_int (first), as_int (last));
  }
  catch (...) {
    if (wait) system_wait ("");
    throw;
  }
  if (wait) system_wait ("");
  set_message ("Done printing", "print to file");
}

void
edit_main_rep::print_buffer (string first, string last) {
  url target;
  target= url_temp (".pdf");
  print_doc (target, false, as_int (first), as_int (last));
  system (get_printing_cmd (), target);  // Send the document to the printer
  set_message ("Done printing", "print buffer");
  ::remove (target);
}

array<SI>
edit_main_rep::print_snippet (url name, tree t, bool conserve_preamble) {
  if (suffix (name) == "ps" || suffix (name) == "eps")
    throw std::runtime_error ("PostScript export is not supported; use PDF or SVG");
  athena::node_reference::verify_export_origin ();
  athena::node_reference::export_reference_scope references;
  athena::node_reference::export_node_destination_scope destinations_scope;
  tree buft= subtree (et, rp);
  if (conserve_preamble)
    if (is_document (buft) && is_compound (buft[0], "hide-preamble"))
      t= tree (SURROUND, buft[0], "", t);

  string s= suffix (name);
  bool bitmap=
    (s == "png" || s == "jpg" || s == "jpeg" || s == "tif" || s == "tiff");
  bool pdf= s == "pdf";

  typeset_prepare ();
  int dpi= as_int (printing_dpi);
  tree old_dpi= env->read (DPI);
  tree old_info_flag= env->read (INFO_FLAG);
  env->write (DPI, printing_dpi);
  if (is_compound (old_info_flag) || !ends (old_info_flag->label, "paper"))
    env->write (INFO_FLAG, "none");
  env->style_init_env ();
  env->update ();
  box b= typeset_as_box (env, t, path ());
  env->write (DPI, old_dpi);
  env->write (INFO_FLAG, old_info_flag);
  env->style_init_env ();
  env->update ();

  references.require_ready ();
  hashmap<string,tree> destinations (UNINIT);
  b->collect_page_numbers (destinations, "?");
  auto resolve_link= [destinations] (string target) {
    return athena::node_reference::export_node_link (target, destinations);
  };
  if (b->x4 - b->x3 >= 5*PIXEL && b->y4 - b->y3 >= 5*PIXEL) {
    if (bitmap) make_raster_image (name, b, 5.0);
    else if (pdf) make_eps (name, b, dpi, resolve_link);
    else {
      url temp= url_temp (".pdf");
      make_eps (temp, b, dpi, resolve_link);
      try {
        if (!conversion_image_file (temp, name))
          throw std::runtime_error ("No native image conversion route for snippet output");
      }
      catch (...) { ::remove (temp); throw; }
      ::remove (temp);
    }
  }
  array<SI> a;
  a << b->x3 << b->y3 << b->x4 << b->y4 << b->x1 << b->y1 << b->x2 << b->y2;
  a << ((SI) env->get_int (FONT_BASE_SIZE)) << ((SI) dpi);
  return a;
}

bool
edit_main_rep::graphics_file_to_clipboard (url name) {
#ifdef QTTEXMACS
  the_gui->put_graphics_on_clipboard (name);
  return true;
#else 
  return false;
#endif
}

tree
edit_main_rep::the_line () {
  path p= search_parent_upwards (DOCUMENT);
  return copy (subtree (et, p));
}

tree
edit_main_rep::the_root () {
  return et;
}

tree
edit_main_rep::the_buffer () {
  return subtree (et, rp);
}

bool
edit_main_rep::test_subtree (path p) {
  return has_subtree (et, p);
}

tree
edit_main_rep::the_subtree (path p) {
  return subtree (et, p);
}

path
edit_main_rep::the_buffer_path () {
  return copy (rp);
}

path
edit_main_rep::the_path () {
  return copy (tp);
}

path
edit_main_rep::the_shifted_path () {
  path result= tp;
  const auto context= editor_rep::cursor_context_at (tp);
  if (context) with_admitted_navigation (*context, [&] (const cursor_navigation& navigation) {
    result= rp * navigation.shift (subtree (et, rp), tp / rp, 1);
  });
  return result;
}

void
edit_main_rep::show_selection () {
  selection sel; selection_get (sel);
  cout << "physical  selection: " << cur_sel << "\n";
  cout << "logical   selection: " << sel->start << " --- " << sel->end << "\n";
}

void
edit_main_rep::edit_special () {
}
