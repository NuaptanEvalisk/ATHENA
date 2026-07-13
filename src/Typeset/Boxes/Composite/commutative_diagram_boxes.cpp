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
    x1= x3= 0;
    x2= x4= width;
    y1= y3= -(height >> 1);
    y2= y4= y1 + height;
    finalize ();
  }

  operator tree () { return "commutative-diagram"; }
  frame get_frame () { return fr; }

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
