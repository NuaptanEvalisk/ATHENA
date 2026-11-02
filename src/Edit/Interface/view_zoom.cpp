// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "view_zoom.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "new_window.hpp"
#include "renderer.hpp"
#include "analyze.hpp"
#include <QString>
#include <algorithm>
#include <cmath>
#include <stdexcept>

void view_change_zoom (double zoom) {
  if (!std::isfinite (zoom)) throw std::runtime_error ("Zoom factor must be finite");
  zoom= std::clamp (zoom, 0.04, 25.0);
  editor ed= get_current_editor ();
  string packet= ed->get_init_string ("page-packet");
  if (get_nr_windows () == 1 && (packet == "1" || packet == "2"))
    set_preference ("zoom factor", as_string (zoom));
  get_server ()->set_window_zoom_factor (zoom);
  ed->notify_page_change ();
  ed->notify_change (THE_CURSOR);
}

void view_other_zoom (string value) {
  bool percent= ends (value, "%");
  if (percent) value= value (0, N(value)-1);
  bool valid= false;
  double number= QString::fromUtf8 (value.data (), N(value)).toDouble (&valid);
  if (!valid)
    throw std::runtime_error ("Invalid zoom factor");
  view_change_zoom (number * (percent? 0.01: 1.0));
}

namespace {
string zoom_key (string mode) {
  return (get_server ()->in_full_screen_mode ()? "fullscreen:": "window:") * mode;
}
double screen_scale () {
  return std::max (0.0001, get_retina_zoom () * get_retina_scale ());
}
void change_fit_zoom (double canvas_zoom) {
  double target= (canvas_zoom-0.0001) / screen_scale ();
  double current= get_server ()->get_window_zoom_factor ();
  if (std::abs (target-current) > 0.001 * std::max (1.0, std::abs (target)))
    view_change_zoom (target);
}
void fit (bool total, bool width, bool height) {
  editor ed= get_current_editor ();
  double zoom= get_server ()->get_window_zoom_factor () * screen_scale ();
  double horizontal= 0, vertical= 0;
  if (width) {
    double extent= total? ed->get_total_width (false): ed->get_pages_width (false);
    if (extent <= 0) return;
    double decorated= total? ed->get_total_width (true): ed->get_pages_width (true);
    horizontal= (ed->get_window_width ()-zoom*(decorated-extent))/extent;
  }
  if (height) {
    double extent= total? ed->get_total_height (false): ed->get_page_height (false);
    if (extent <= 0) return;
    double decorated= total? ed->get_total_height (true): ed->get_page_height (true);
    vertical= (ed->get_window_height ()-zoom*(decorated-extent))/extent;
  }
  change_fit_zoom (width && height? std::min (horizontal, vertical):
                   width? horizontal: vertical);
}
}

void view_save_zoom (string mode) {
  get_current_editor ()->saved_view_zooms (zoom_key (mode))=
    get_server ()->get_window_zoom_factor ();
}
void view_restore_zoom (string mode) {
  editor ed= get_current_editor ();
  string key= zoom_key (mode);
  if (!ed->saved_view_zooms->contains (key)) return;
  double saved= ed->saved_view_zooms[key];
  if (saved != get_server ()->get_window_zoom_factor ()) view_change_zoom (saved);
}
void view_zoom_in (double factor) {
  double zoom= factor * get_server ()->get_window_zoom_factor ();
  for (int i=-10; i<10; ++i) {
    double standard= std::exp (i * std::log (2.0)/4.0);
    if (std::abs (zoom-standard) < 0.02*zoom) { zoom= standard; break; }
  }
  view_change_zoom (zoom);
}
void view_zoom_out (double factor) { view_zoom_in (1.0/factor); }
void view_fit_all () { fit (true, true, true); }
void view_fit_screen () { fit (false, true, true); }
void view_fit_width () { fit (false, true, false); }
void view_fit_height () { fit (false, false, true); }
