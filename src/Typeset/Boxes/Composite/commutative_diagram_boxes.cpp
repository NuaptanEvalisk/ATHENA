/******************************************************************************
* MODULE     : commutative_diagram_boxes.cpp
* DESCRIPTION: Native fixed-size boxes for ATHENA commutative diagrams
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Boxes/composite.hpp"
#include "Boxes/construct.hpp"
#include "frame.hpp"

struct commutative_diagram_box_rep: public composite_box_rep {
  frame fr;
  SI old_clip_x1, old_clip_y1, old_clip_x2, old_clip_y2;

  commutative_diagram_box_rep (path ip, array<box> bs,
                               array<SI> x, array<SI> y,
                               frame fr, SI width, SI height):
    composite_box_rep (ip, bs, x, y), fr (fr)
  {
    SI child_x1= min (x1, x3), child_x2= max (x2, x4);
    SI child_y1= min (y1, y3), child_y2= max (y2, y4);
    SI nominal_y1= -(height >> 1);
    SI nominal_y2= nominal_y1 + height;
    x1= x3= min ((SI) 0, child_x1);
    x2= x4= max (width, child_x2);
    y1= y3= min (nominal_y1, child_y1);
    y2= y4= max (nominal_y2, child_y2);
    finalize ();
  }

  operator tree () { return "commutative-diagram"; }
  frame get_frame () { return fr; }

  tree message (tree type, SI x, SI y, rectangles& rs) override {
    point p= fr [point (x, y)];
    if (N(p) < 2) return "";
    tree event (TUPLE);
    event << "commutative-diagram-event"
          << copy (type)
          << as_string (p[0])
          << as_string (p[1])
          << as_string (reverse (ip));
    rs << rectangle (x3, y3, x4, y4);
    return event;
  }

  void pre_display (renderer& ren) {
    ren->get_clipping (old_clip_x1, old_clip_y1,
                       old_clip_x2, old_clip_y2);
    ren->extra_clipping (x1, y1, x2, y2);
  }

  void post_display (renderer& ren) {
    ren->set_clipping (old_clip_x1, old_clip_y1,
                       old_clip_x2, old_clip_y2, true);
  }
};

box
commutative_diagram_box (path ip, array<box> bs,
                         array<SI> x, array<SI> y,
                         frame fr, SI width, SI height) {
  return tm_new<commutative_diagram_box_rep> (
    ip, bs, x, y, fr, width, height);
}
