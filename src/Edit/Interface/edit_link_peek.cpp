/******************************************************************************
* MODULE     : edit_link_peek.cpp
* DESCRIPTION: Actor-owned interactive link overlays in the document renderer
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
#include <QUrl>

void edit_interface_rep::clear_link_peek () {
  if (link_peeks.empty ()) return;
  link_peeks.clear ();
  link_peek_pressed_target= "";
  invalidate_all ();
}

bool edit_interface_rep::dismiss_link_peek () {
  if (link_peeks.empty ()) return false;
  link_peeks.pop_back ();
  link_peek_pressed_target= "";
  invalidate_all ();
  return true;
}

int edit_interface_rep::link_peek_at (SI x, SI y, SI margin) {
  for (int i= (int) link_peeks.size ()-1; i>=0; --i) {
    rectangle r= link_peeks[i].bounds;
    if (x >= r->x1-margin && x < r->x2+margin &&
        y >= r->y1-margin && y < r->y2+margin) return i;
  }
  return -1;
}

string edit_interface_rep::link_peek_hit (int layer, SI x, SI y) {
  rectangles ignored;
  tree hit;
  if (layer < 0) hit= eb->message ("link-target", x, y, ignored);
  else {
    auto& peek= link_peeks[layer];
    rectangle r= peek.bounds;
    SI pad= 10*pixel;
    // Only visible content can be clicked, not clipped text or padding.
    if (x < r->x1+pad || x >= r->x2-pad ||
        y < r->y1+pad || y >= r->y2-pad) return "";
    hit= peek.content->message ("link-target",
      x-r->x1-pad+peek.content->x1,
      y-r->y2+pad+peek.content->y2, ignored);
  }
  if (!is_tuple (hit, "link-target", 1) || !is_atomic (hit[1])) return "";
  string target= hit[1]->label;
  QUrl destination (QString::fromUtf8 (as_charp (target), N(target)));
  if (layer >= 0 && destination.isRelative () &&
      !is_none (link_peeks[layer].source)) {
    string source= concretize (link_peeks[layer].source);
    QUrl base= QUrl::fromLocalFile (QString::fromUtf8 (as_charp (source), N(source)));
    QByteArray encoded= base.resolved (destination).toEncoded ();
    target= string (encoded.constData (), encoded.size ());
  }
  return target;
}

bool edit_interface_rep::mouse_link_peek (string type, SI x, SI y, int modifiers) {
  if (type == "move" && !(modifiers & 1)) {
    link_peek_pressed= false;
    link_peek_pressed_target= "";
  }
  if (type == "peek-leave" || type == "leave") {
    clear_link_peek ();
    set_pointer ("XC_top_left_arrow");
    return type == "peek-leave";
  }
  if (type == "move" || type == "peek-modifier")
    update_link_peek (x, y, modifiers);
  int layer= link_peek_at (x, y);
  bool inside= link_peek_at (x, y, 24*pixel) >= 0;
  string target= layer < 0 ? string ("") : link_peek_hit (layer, x, y);
  if (type == "press-left" && inside) {
    link_peek_pressed= true;
    link_peek_pressed_target= target;
  }
  if (type == "release-left" && link_peek_pressed) {
    link_peek_pressed= false;
    bool follow= target != "" && target == link_peek_pressed_target;
    link_peek_pressed_target= "";
    if (follow) {
      clear_link_peek ();
      call ("go-to-url", object (target));
    }
    return true;
  }
  if (inside) {
    set_pointer (target == "" ? "XC_top_left_arrow" : "XC_hand2");
    return true; // Overlay input must never edit the document underneath it.
  }
  if (type == "peek-modifier") return true;
  if (link_peek_pressed) return true;
  return false;
}

void edit_interface_rep::update_link_peek (SI x, SI y, int modifiers) {
  if (is_nil (eb)) {
    clear_link_peek ();
    return;
  }
  // The halo also bridges the short gap from the initiating link to its peek.
  if (link_peek_at (x, y, 24*pixel) < 0)
    clear_link_peek ();
  if (!(modifiers & 256) || (modifiers & 31)) return;
  int parent= link_peek_at (x, y);
  string target= link_peek_hit (parent, x, y);
  if (!athena_link_peek_target (target)) return;
  size_t next= (size_t) (parent+1);
  if (next < link_peeks.size () && link_peeks[next].target == target) return;
  // Hovering another link replaces only that branch, never its ancestors.
  link_peeks.resize (next);
  invalidate_all ();
  url source;
  tree document= athena_link_peek_document (target, source);
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
  SI height= min (rendered->h ()+2*pad, min (400*pixel, (vy2-vy1)/2));
  SI left= max (vx1+pad, min (x+pad, vx2-width-pad));
  SI top= y-2*pad;
  if (top-height < vy1+pad) top= min (vy2-pad, y+height+2*pad);
  top= max (vy1+height+pad, min (top, vy2-pad));
  link_peeks.push_back ({copy (target), source, rendered,
                        rectangle (left, top-height, left+width, top)});
  notify_change (THE_DECORATIONS | THE_FREEZE);
  invalidate_all ();
}

void edit_interface_rep::draw_link_peek (renderer ren) {
  if (link_peeks.empty ()) return;
  brush background= ren->get_background ();
  pencil pen= ren->get_pencil ();
  for (auto& peek: link_peeks) {
    rectangle r= peek.bounds;
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
    peek.content->redraw (ren, path (), painted,
      r->x1+pad-peek.content->x1, r->y2-pad-peek.content->y2);
    ren->unclip ();
  }
  ren->set_background (background);
  ren->set_pencil (pen);
}
