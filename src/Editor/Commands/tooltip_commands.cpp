// Copyright (C) 2020 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Editor/Commands/tooltip_commands.hpp"
#include "native_interfaces.hpp"
#include "Editor/Commands/document_commands.hpp"
#include "Editor/editor.hpp"
#include "App/Runtime/server.hpp"
#include "analyze.hpp"

namespace {
SI horizontal_position (SI x1, SI x2, SI wx, SI bw, SI sw, SI mx, string align) {
  if (!starts (align, "raw-"))
    return max (0, min (sw-bw, horizontal_position (x1,x2,wx,bw,sw,mx,"raw-"*align)));
  if (align == "raw-Left") return wx+x1-bw;
  if (align == "raw-left") return wx+x1;
  if (align == "raw-center") return wx+(x1+x2-bw)/2;
  if (align == "raw-right") return wx+x2-bw;
  if (align == "raw-Right") return wx+x2;
  if (align == "raw-mouse-Left") return wx+mx-bw;
  if (align == "raw-mouse-left") return wx+mx-bw+12*256;
  if (align == "raw-mouse-center") return wx+mx+(12*256-bw)/2;
  if (align == "raw-mouse-right") return wx+mx;
  if (align == "raw-mouse-Right") return wx+mx+12*256;
  return wx+x1;
}
SI vertical_position (SI y1, SI y2, SI wy, SI bh, SI sh, SI my, string align) {
  if (starts (align, "prefer-")) {
    string preferred= align (7,N(align)), other= preferred;
    if (preferred == "Bottom") other= "Top";
    else if (preferred == "Top") other= "Bottom";
    else if (preferred == "mouse-Bottom") other= "mouse-Top";
    else if (preferred == "mouse-Top") other= "mouse-Bottom";
    SI y= vertical_position (y1,y2,wy,bh,sh,my,"raw-"*preferred);
    if (y >= bh-sh && y <= 0) return y;
    return vertical_position (y1,y2,wy,bh,sh,my,other);
  }
  if (!starts (align, "raw-"))
    return max (bh-sh, min (0, vertical_position (y1,y2,wy,bh,sh,my,"raw-"*align)));
  if (align == "raw-Bottom") return wy+y1-5*256;
  if (align == "raw-bottom") return wy+y1+bh;
  if (align == "raw-center") return wy+(y1+y2+bh)/2;
  if (align == "raw-top") return wy+y2;
  if (align == "raw-Top") return wy+y2+bh+5*256;
  if (align == "raw-mouse-Bottom") return wy+my-20*256;
  if (align == "raw-mouse-bottom") return wy+my;
  if (align == "raw-mouse-center") return wy+my+(bh-20*256)/2;
  if (align == "raw-mouse-top") return wy+my+bh-20*256;
  if (align == "raw-mouse-Top") return wy+my+bh;
  return wy+y1-5*256;
}
}

void tooltip_show (string id, tree reference, tree content, string ha,
                   string va, string type, double magnification) {
  editor ed= get_current_editor ();
  const auto bounds= get_bounding_rectangle (reference);
  const auto mouse= ed->get_mouse_position ();
  const SI x1= bounds[0], y1= bounds[1], x2= bounds[2], y2= bounds[3];
  const SI mx= mouse[0], my= mouse[1];
  if (type != "keyboard" &&
      (mx < x1-5*256 || mx > x2+5*256 || my < y1-5*256 || my > y2+5*256)) return;
  double zoom= get_server ()->get_window_zoom_factor ();
  array<double> settings;
  settings << double(x1-ed->get_canvas_x ()) << double(y1-ed->get_canvas_y ())
           << double(x2-ed->get_canvas_x ()) << double(y2-ed->get_canvas_y ())
           << double(mx-ed->get_canvas_x ()) << double(my-ed->get_canvas_y ())
           << double(ed->get_scroll_x ()) << double(ed->get_scroll_y ()) << zoom;
  if (athena_native_tooltip_confirm (id, settings, type)) return;
  tree document (WITH), inits= ed->get_init_all ();
  for (int i=0; i<N(inits); ++i) {
    tree entry= inits[i];
    if (!is_func (entry, ASSOCIATE, 2) || !is_atomic (entry[0])) continue;
    string key= entry[0]->label;
    if (starts (key,"page-") || key == "zoom-factor" || key == "full-screen-mode") continue;
    document << entry[0] << entry[1];
  }
  document << "magnification" << as_string (zoom*magnification)
           << "project" << as_string (ed->get_name ())
           << tree (SURROUND, compound ("hide-preamble", document_get_preamble (ed->the_buffer ())), "", content);
  tree packages= ed->get_style ();
  if (is_atomic (packages)) packages= tree (TUPLE, packages);
  tree style= compound ("style", packages);
  auto size= athena_native_tooltip_size (document, style);
  if (N(size) != 2) return; // Headless runtimes do not create tooltip windows.
  auto screen= get_screen_size ();
  if (type == "mouse" || type == "mouse*") { ha= "mouse-"*ha; va= "mouse-"*va; }
  if (ha == "auto" && va == "auto") {
    bool mouse_relative= type != "keyboard" && (size[0] < x2-x1 || size[1] < y2-y1);
    ha= mouse_relative ? "mouse-right" : "left";
    va= mouse_relative ? "prefer-mouse-Bottom" : "prefer-Bottom";
  }
  SI x= horizontal_position (x1,x2,ed->get_window_x (),size[0],screen[0],mx,ha);
  SI y= vertical_position (y1,y2,ed->get_window_y (),size[1],screen[1],my,va);
  athena_native_tooltip_show (document, style, x/256, y/256, id, settings, type);
}
