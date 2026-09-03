/******************************************************************************
* MODULE     : qt_actor_widget.hpp
* DESCRIPTION: ID-only Qt canvas proxy for a BufferActor-owned editor
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QT_ACTOR_WIDGET_HPP
#define QT_ACTOR_WIDGET_HPP

#include "actor_transport.hpp"
#include "actor_ui_bridge.hpp"
#include "qt_simple_widget.hpp"

class qt_actor_widget_rep final: public qt_simple_widget_rep {
public:
  qt_actor_widget_rep (athena_actor_id actor_id, athena_view_id view_id,
                       bool embedded);
  ~qt_actor_widget_rep () override;

  athena_actor_id actor_id () const noexcept;
  athena_view_id view_id () const noexcept;

  bool is_editor_widget () override;
  bool is_embedded_widget () override;
  void handle_notify_resize (SI width, SI height) override;
  void handle_keypress (string key, time_t time) override;
  void handle_text_input (string text, time_t time) override;
  void handle_keyboard_focus (bool focused, time_t time) override;
  void handle_cursor_blink (bool visible) override;
  void handle_user_scroll (time_t time) override;
  void handle_mouse (string kind, SI x, SI y, int modifiers, time_t time,
                     array<double> data) override;
  bool handle_wheel_capture () override;
  void handle_zoom_by (bool zoom_in, double amount) override;
  void handle_set_zoom_factor (double zoom) override;
  void handle_device_pixel_ratio_changed () override;
  bool handle_activate_owning_view () override;
  void handle_repaint (renderer renderer, SI x1, SI y1, SI x2, SI y2) override;
  void drain_external_effects () override;
  void handle_render_connection_ready (
    athena_resource_id connection_id) override;

private:
  const athena_actor_id actor_id_;
  const athena_view_id view_id_;
  const bool embedded_;
  actor_ui_endpoint* const endpoint_;
  widget popup_window_;
  widget popup_content_;

  void submit_text (actor_command_kind kind, string text,
                    std::uint64_t argument0= 0);
  void refresh_viewport ();
};

widget actor_editor_widget (athena_actor_id actor_id,
                            athena_view_id view_id, bool embedded);

#endif // defined QT_ACTOR_WIDGET_HPP
