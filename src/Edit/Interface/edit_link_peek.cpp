/******************************************************************************
* MODULE     : edit_link_peek.cpp
* DESCRIPTION: Actor-owned, noninteractive link overlay in the document renderer
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "Interface/edit_interface.hpp"
#include "ATHENA/Data/link_peek.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "formatter.hpp"
#include "Format/format.hpp"

void edit_interface_rep::clear_link_peek () {
  if (link_peek_target == "") return;
  link_peek_target= "";
  link_peek_box= box ();
  invalidate_all ();
}

void edit_interface_rep::update_link_peek (SI x, SI y, int modifiers) {
  if (!(modifiers & 256) || (modifiers & 31) || is_nil (eb)) {
    clear_link_peek ();
    return;
  }
  rectangles ignored;
  tree hit= eb->message ("link-target", x, y, ignored);
  if (!is_tuple (hit, "link-target", 1) || !is_atomic (hit[1]) ||
      !athena_link_peek_target (hit[1]->label)) {
    clear_link_peek ();
    return;
  }
  if (link_peek_target == hit[1]->label) return;
  clear_link_peek ();
  url source;
  tree document= athena_link_peek_document (hit[1]->label, source);
  if (document == tree (UNINIT)) return;

  SI pad= 10*pixel;
  SI width= min (560*pixel, vx2-vx1-4*pad);
  if (width <= 4*pad || vy2-vy1 <= 8*pad) return;
  // Independent environment and reference tables keep preview assignments,
  // labels and page numbers out of the edited document's typesetter state.
  drd_info preview_drd ("link-peek", std_drd);
  hashmap<string,tree> r1 (UNINIT), r2 (UNINIT), a1 (UNINIT), a2 (UNINIT);
  hashmap<string,tree> t1 (UNINIT), t2 (UNINIT);
  edit_env preview (preview_drd, source, r1, r2, a1, a2, t1, t2);
  preview->write_default_env ();
  tree style= extract (document, "style");
  tree packages (USE_PACKAGE);
  if (is_tuple (style)) packages << A(style);
  else if (is_atomic (style) && style != "") packages << style;
  else packages << "generic";
  preview->exec (packages);
  tree initial= extract (document, "initial");
  if (is_func (initial, COLLECTION))
    preview->patch_env (hashmap<string,tree> (UNINIT, initial));
  preview->write (MODE, "text");
  preview->write (PAR_WIDTH, as_string (width-2*pad) * "tmpt");
  preview->write (PAR_LEFT, "0tmpt");
  preview->write (PAR_RIGHT, "0tmpt");
  preview->write (PAGE_MEDIUM, "papyrus");
  preview->write (INFO_FLAG, "none");
  preview->read_only= true;
  preview->update ();
  lazy content= make_lazy (preview, extract (document, "body"), path ());
  lazy lines= content->produce (LAZY_VSTREAM,
                               make_format_vstream (width-2*pad, 0, 0));
  box rendered= (box) lines->produce (LAZY_BOX, make_format_none ());
  if (is_nil (rendered)) return;
  link_peek_box= rendered;
  link_peek_target= copy (hit[1]->label);
  SI height= min (rendered->h ()+2*pad, min (400*pixel, (vy2-vy1)/2));
  SI left= max (vx1+pad, min (x+pad, vx2-width-pad));
  SI top= y-2*pad;
  if (top-height < vy1+pad) top= min (vy2-pad, y+height+2*pad);
  top= max (vy1+height+pad, min (top, vy2-pad));
  link_peek_rect= rectangle (left, top-height, left+width, top);
  notify_change (THE_DECORATIONS | THE_FREEZE);
  invalidate_all ();
}

void edit_interface_rep::draw_link_peek (renderer ren) {
  if (is_nil (link_peek_box)) return;
  rectangle r= link_peek_rect;
  brush background= ren->get_background ();
  pencil pen= ren->get_pencil ();
  ren->set_background (rgb_color (248, 250, 252));
  ren->clear (r->x1, r->y1, r->x2, r->y2);
  ren->set_pencil (pencil (rgb_color (100, 110, 120), pixel));
  ren->line (r->x1, r->y1, r->x2, r->y1);
  ren->line (r->x2, r->y1, r->x2, r->y2);
  ren->line (r->x2, r->y2, r->x1, r->y2);
  ren->line (r->x1, r->y2, r->x1, r->y1);
  SI pad= 10*pixel;
  ren->clip (r->x1+pad, r->y1+pad, r->x2-pad, r->y2-pad);
  rectangles painted;
  link_peek_box->redraw (ren, path (), painted,
    r->x1+pad-link_peek_box->x1, r->y2-pad-link_peek_box->y2);
  ren->unclip ();
  ren->set_background (background);
  ren->set_pencil (pen);
}
