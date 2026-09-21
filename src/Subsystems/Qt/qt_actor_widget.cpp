/******************************************************************************
* MODULE     : qt_actor_widget.cpp
* DESCRIPTION: ID-only Qt canvas proxy for a BufferActor-owned editor
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "qt_actor_widget.hpp"

#include "buffer_actor.hpp"
#include "Data/new_buffer.hpp"
#include "Data/new_window.hpp"
#include "Data/new_view.hpp"
#include "message.hpp"
#include "scheme.hpp"
#include "gui_text.hpp"
#include "tm_server.hpp"
#include "tm_window.hpp"
#include "QTMToast.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMDocumentSearchBar.hpp"
#include "QTMCompletionPopup.hpp"
#include "QTMVaultBackupDispatcher.hpp"
#include "QTMVaultExplorer.hpp"
#include "qt_utilities.hpp"
#include "qt_tm_widget.hpp"
#include "renderer.hpp"

#include <QApplication>
#include <QStyle>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

std::uint64_t
double_bits (double value) noexcept {
  std::uint64_t result;
  static_assert (sizeof (result) == sizeof (value),
                 "double command argument has unexpected size");
  std::memcpy (&result, &value, sizeof (result));
  return result;
}

void
discard_unsubmitted (athena_blob_id first, athena_blob_id second,
                     actor_command_ticket ticket) {
  if (ticket) return;
  if (first != ATHENA_NO_BLOB)
    (void) actor_text_registry::instance ().discard (first);
  if (second != ATHENA_NO_BLOB)
    (void) actor_blob_registry::instance ().discard (second);
}

} // namespace

qt_actor_widget_rep::qt_actor_widget_rep (
  athena_actor_id actor_id, athena_view_id view_id, bool embedded):
  actor_id_ (actor_id), view_id_ (view_id), embedded_ (embedded),
  endpoint_ (register_actor_ui_endpoint (view_id)), popup_window_ (),
  popup_content_ () {}

qt_actor_widget_rep::~qt_actor_widget_rep () {
  delete completion_popup_.data ();
  unregister_actor_ui_endpoint (view_id_);
}

athena_actor_id
qt_actor_widget_rep::actor_id () const noexcept {
  return actor_id_;
}

athena_view_id
qt_actor_widget_rep::view_id () const noexcept {
  return view_id_;
}

bool
qt_actor_widget_rep::is_editor_widget () {
  return true;
}

bool
qt_actor_widget_rep::is_embedded_widget () {
  return embedded_;
}

void
qt_actor_widget_rep::submit_text (
  actor_command_kind kind, string text, std::uint64_t argument0) {
  athena_blob_id payload= actor_text_from_string (std::move (text));
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, kind, view_id_, payload, ATHENA_NO_BLOB,
    SCHEME_CAPABILITY_BUFFER, argument0);
  if (!ticket)
    (void) actor_text_registry::instance ().discard (payload);
}

void
qt_actor_widget_rep::handle_notify_resize (SI width, SI height) {
  actor_viewport_snapshot old_viewport= endpoint_->viewport ();
  bool old_viewport_valid=
    old_viewport.attached &&
    old_viewport.visible_y2 > old_viewport.visible_y1;
  std::uint64_t old_programmatic_scroll_generation=
    endpoint_->applied_programmatic_scroll_generation ();
  std::uint64_t old_user_scroll_generation=
    endpoint_->user_scroll_generation ();
  refresh_viewport ();
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::viewport_changed, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (width),
    static_cast<std::uint64_t> (height),
    static_cast<std::uint64_t> (old_viewport.visible_y2),
    old_viewport_valid ? 1 : 0,
    old_programmatic_scroll_generation,
    old_user_scroll_generation);
}

void
qt_actor_widget_rep::handle_keypress (string key, time_t time) {
  submit_text (actor_command_kind::key_press, std::move (key),
               static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_text_input (string text, time_t time) {
  if (completion_popup_) completion_popup_->cancel ();
  submit_text (actor_command_kind::text_input, std::move (text),
               static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_keyboard_focus (bool focused, time_t time) {
  if (!focused && completion_popup_) completion_popup_->cancel ();
  refresh_viewport ();
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::keyboard_focus, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    focused ? 1 : 0, static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_cursor_blink (bool visible) {
  (void) buffer_actor::try_submit_coalesced_to (
    actor_id_, actor_command_kind::cursor_blink, view_id_,
    visible ? 1 : 0);
}

void
qt_actor_widget_rep::handle_user_scroll (time_t time) {
  if (completion_popup_) completion_popup_->cancel ();
  endpoint_->mark_user_scroll ();
  refresh_viewport ();
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::user_scroll, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_mouse (
  string kind, SI x, SI y, int modifiers, time_t time, array<double> data) {
  athena_blob_id kind_payload= actor_text_from_string (std::move (kind));
  athena_blob_id data_payload= ATHENA_NO_BLOB;
  if (N (data) != 0) {
    std::size_t bytes= static_cast<std::size_t> (N (data)) * sizeof (double);
    actor_blob_reservation reservation=
      actor_blob_registry::instance ().allocate (bytes);
    std::memcpy (reservation.data (), A (data), bytes);
    data_payload= reservation.publish ();
  }
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::mouse, view_id_, kind_payload,
    data_payload, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (x), static_cast<std::uint64_t> (y),
    static_cast<std::uint64_t> (modifiers),
    static_cast<std::uint64_t> (time),
    static_cast<std::uint64_t> (N (data)));
  discard_unsubmitted (kind_payload, data_payload, ticket);
}

bool
qt_actor_widget_rep::handle_native_ink_hit (
  SI x, SI y, native_ink_preview_style& style) {
  if (endpoint_ == nullptr) return false;
  double zoom= endpoint_->zoom_factor ();
  if (!(zoom > 0.0) || !std::isfinite (zoom)) zoom= 1.0;
  double input_scale= static_cast<double> (std_shrinkf) / zoom;
  SI ux= static_cast<SI> (
    std::llround (static_cast<double> (x) * input_scale));
  SI uy= static_cast<SI> (
    std::llround (static_cast<double> (y) * input_scale));
  std::vector<native_ink_interaction_snapshot> regions=
    endpoint_->native_ink_regions ();
  const native_ink_interaction_snapshot* best= nullptr;
  long double best_area= 0.0;
  for (const native_ink_interaction_snapshot& region: regions) {
    if (!region.pen_enabled || ux < region.x1 || ux > region.x2 ||
        uy < region.y1 || uy > region.y2)
      continue;
    long double width= static_cast<long double> (region.x2) - region.x1;
    long double height= static_cast<long double> (region.y2) - region.y1;
    long double area= std::max ((long double) 0.0, width) *
                      std::max ((long double) 0.0, height);
    if (best == nullptr || area < best_area) {
      best= &region;
      best_area= area;
    }
  }
  if (best == nullptr) return false;
  style.rgba= best->rgba;
  style.line_width_pixels=
    std::max (1.0, best->line_width_pixels * zoom);
  style.eraser_radius_pixels=
    std::max (1.0, best->eraser_radius_pixels * zoom);
  style.pressure_enabled= best->pressure_enabled;
  style.tool= best->tool;
  style.shape= best->shape;
  return true;
}

bool
qt_actor_widget_rep::handle_native_drawing_available () {
  return endpoint_ != nullptr && !endpoint_->native_ink_regions ().empty ();
}

native_drawing_tool
qt_actor_widget_rep::handle_native_drawing_tool () {
  if (endpoint_ == nullptr) return native_drawing_tool::pen;
  std::vector<native_ink_interaction_snapshot> regions=
    endpoint_->native_ink_regions ();
  return regions.empty () ? native_drawing_tool::pen : regions.front ().tool;
}

native_drawing_properties_snapshot
qt_actor_widget_rep::handle_native_drawing_properties () {
  if (endpoint_ == nullptr) return native_drawing_properties_snapshot ();
  return endpoint_->native_drawing_properties ();
}

bool
qt_actor_widget_rep::handle_set_native_drawing_tool (native_drawing_tool tool) {
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::set_native_drawing_tool, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (tool));
  return static_cast<bool> (ticket);
}

bool
qt_actor_widget_rep::handle_set_native_drawing_property (
  native_drawing_property property, std::uint64_t value) {
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::set_native_drawing_property, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (property), value);
  return static_cast<bool> (ticket);
}

std::vector<native_drawing_selection_box>
qt_actor_widget_rep::handle_native_drawing_selection () {
  if (endpoint_ == nullptr) return {};
  double zoom= endpoint_->zoom_factor ();
  if (!(zoom > 0.0) || !std::isfinite (zoom)) zoom= 1.0;
  double output_scale= zoom / static_cast<double> (std_shrinkf);
  std::vector<native_drawing_selection_box> result=
    endpoint_->native_drawing_selection ();
  for (auto& box: result) {
    box.x1= static_cast<SI> (std::llround (box.x1 * output_scale));
    box.y1= static_cast<SI> (std::llround (box.y1 * output_scale));
    box.x2= static_cast<SI> (std::llround (box.x2 * output_scale));
    box.y2= static_cast<SI> (std::llround (box.y2 * output_scale));
  }
  return result;
}

bool
qt_actor_widget_rep::handle_native_drawing_gesture (
  native_drawing_tool tool, native_drawing_shape shape,
  const native_ink_sample* samples,
  std::size_t count) {
  if (samples == nullptr || count == 0 || endpoint_ == nullptr) return false;
  if (count > (16U * 1024U * 1024U) / sizeof (native_ink_sample)) return false;
  std::size_t bytes= count * sizeof (native_ink_sample);
  actor_blob_reservation reservation=
    actor_blob_registry::instance ().allocate (bytes);
  native_ink_sample* output=
    reinterpret_cast<native_ink_sample*> (reservation.data ());
  double zoom= endpoint_->zoom_factor ();
  if (!(zoom > 0.0) || !std::isfinite (zoom)) zoom= 1.0;
  double input_scale= static_cast<double> (std_shrinkf) / zoom;
  for (std::size_t i= 0; i < count; ++i) {
    output[i]= samples[i];
    output[i].x= static_cast<SI> (
      std::llround (static_cast<double> (samples[i].x) * input_scale));
    output[i].y= static_cast<SI> (
      std::llround (static_cast<double> (samples[i].y) * input_scale));
  }
  athena_blob_id payload= reservation.publish ();
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::native_ink_stroke, view_id_, payload,
    ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (count),
    static_cast<std::uint64_t> (tool),
    static_cast<std::uint64_t> (shape));
  if (!ticket) (void) actor_blob_registry::instance ().discard (payload);
  return static_cast<bool> (ticket);
}

bool
qt_actor_widget_rep::handle_native_drawing_transform (
  native_drawing_transform transform,
  const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count < 2 || endpoint_ == nullptr) return false;
  if (count > (16U * 1024U * 1024U) / sizeof (native_ink_sample)) return false;
  std::size_t bytes= count * sizeof (native_ink_sample);
  actor_blob_reservation reservation=
    actor_blob_registry::instance ().allocate (bytes);
  native_ink_sample* output=
    reinterpret_cast<native_ink_sample*> (reservation.data ());
  double zoom= endpoint_->zoom_factor ();
  if (!(zoom > 0.0) || !std::isfinite (zoom)) zoom= 1.0;
  double input_scale= static_cast<double> (std_shrinkf) / zoom;
  for (std::size_t i= 0; i < count; ++i) {
    output[i]= samples[i];
    output[i].x= static_cast<SI> (
      std::llround (static_cast<double> (samples[i].x) * input_scale));
    output[i].y= static_cast<SI> (
      std::llround (static_cast<double> (samples[i].y) * input_scale));
  }
  athena_blob_id payload= reservation.publish ();
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::native_drawing_transform, view_id_, payload,
    ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (transform),
    static_cast<std::uint64_t> (count));
  if (!ticket) (void) actor_blob_registry::instance ().discard (payload);
  return static_cast<bool> (ticket);
}

bool
qt_actor_widget_rep::handle_native_drawing_insert_space (
  bool horizontal, const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count < 2 || endpoint_ == nullptr) return false;
  std::size_t bytes= count * sizeof (native_ink_sample);
  actor_blob_reservation reservation=
    actor_blob_registry::instance ().allocate (bytes);
  native_ink_sample* output=
    reinterpret_cast<native_ink_sample*> (reservation.data ());
  double zoom= endpoint_->zoom_factor ();
  if (!(zoom > 0.0) || !std::isfinite (zoom)) zoom= 1.0;
  double input_scale= static_cast<double> (std_shrinkf) / zoom;
  for (std::size_t i=0; i<count; ++i) {
    output[i]= samples[i];
    output[i].x= static_cast<SI> (
      std::llround (static_cast<double> (samples[i].x) * input_scale));
    output[i].y= static_cast<SI> (
      std::llround (static_cast<double> (samples[i].y) * input_scale));
  }
  athena_blob_id payload= reservation.publish ();
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::native_drawing_insert_space, view_id_, payload,
    ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    horizontal ? 1U : 0U, static_cast<std::uint64_t> (count));
  if (!ticket) (void) actor_blob_registry::instance ().discard (payload);
  return static_cast<bool> (ticket);
}

bool
qt_actor_widget_rep::handle_native_drawing_trim () {
  actor_command_ticket ticket= buffer_actor::submit_to (
    actor_id_, actor_command_kind::native_drawing_trim, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER);
  return static_cast<bool> (ticket);
}

bool
qt_actor_widget_rep::handle_native_ink_stroke (
  const native_ink_sample* samples, std::size_t count) {
  return handle_native_drawing_gesture (
    native_drawing_tool::pen, native_drawing_shape::line, samples, count);
}

bool
qt_actor_widget_rep::handle_wheel_capture () {
  return endpoint_ != nullptr && endpoint_->wheel_capture ();
}

bool
qt_actor_widget_rep::handle_overlay_wheel_capture () {
  return endpoint_ != nullptr && endpoint_->overlay_wheel_capture ();
}

double
qt_actor_widget_rep::handle_get_zoom_factor () {
  return endpoint_ == nullptr ? 1.0 : endpoint_->zoom_factor ();
}

void
qt_actor_widget_rep::handle_zoom_by (bool zoom_in, double amount) {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::zoom_by, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    zoom_in ? 1 : 0, double_bits (amount));
}

void
qt_actor_widget_rep::handle_change_zoom_factor (double zoom) {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::change_zoom, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    double_bits (zoom));
}

void
qt_actor_widget_rep::handle_set_zoom_factor (double zoom) {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::set_zoom, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    double_bits (zoom));
}

void
qt_actor_widget_rep::handle_device_pixel_ratio_changed () {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::device_pixel_ratio_changed, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER);
}

bool
qt_actor_widget_rep::handle_activate_owning_view () {
  tm_view view= concrete_runtime_view (view_id_);
  if (view == nullptr) return false;
  set_current_view (abstract_view (view));
  return true;
}

void
qt_actor_widget_rep::handle_repaint (
  renderer renderer, SI x1, SI y1, SI x2, SI y2) {
  actor_viewport_snapshot snapshot= endpoint_->viewport ();
  snapshot.render_origin_x= renderer->ox;
  snapshot.render_origin_y= renderer->oy;
  renderer->get_extents (snapshot.render_width, snapshot.render_height);
  snapshot.render_pixel_ratio= renderer->pixel_ratio;
  endpoint_->update_viewport (snapshot);
  bool submitted= buffer_actor::try_submit_coalesced_to (
    actor_id_, actor_command_kind::render_view, view_id_,
    static_cast<std::uint64_t> (x1), static_cast<std::uint64_t> (y1),
    static_cast<std::uint64_t> (x2), static_cast<std::uint64_t> (y2));
  if (!submitted) invalidate_render_rect (renderer, x1, y1, x2, y2);
}

void
qt_actor_widget_rep::handle_render_connection_ready (
  athena_resource_id connection_id) {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::set_render_connection, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    connection_id);
}

void
qt_actor_widget_rep::refresh_viewport () {
  if (qwid == nullptr) return;
  // Geometry refreshes must not erase the render target published by the
  // repaint path while its BufferActor is preparing the corresponding frame.
  actor_viewport_snapshot snapshot= endpoint_->viewport ();
  ::get_visible_part (widget (this), snapshot.visible_x1,
                      snapshot.visible_y1, snapshot.visible_x2,
                      snapshot.visible_y2);
  ::get_size (widget (this), snapshot.window_width, snapshot.window_height);
  widget top= ::get_window (widget (this));
  if (!is_nil (top))
    ::get_position (top, snapshot.window_x, snapshot.window_y);
  widget cv= ::get_canvas (widget (this));
  if (!is_nil (cv))
    ::get_position (cv, snapshot.canvas_x, snapshot.canvas_y);
  ::get_scroll_position (widget (this), snapshot.scroll_x, snapshot.scroll_y);
  snapshot.scrollbar_width=
    (qApp->style ()->pixelMetric (QStyle::PM_ScrollBarExtent) + 2) * PIXEL;
  snapshot.attached= ::is_attached (widget (this));
  snapshot.focused= canvas () != nullptr && canvas ()->hasFocus ();
  snapshot.full_screen= get_server ()->in_full_screen_mode ();
  snapshot.invalid= is_invalid ();
  tm_view view= concrete_runtime_view (view_id_);
  if (view != nullptr && view->win != nullptr) {
    snapshot.window_serial= view->win->serial;
    string id= as_string (abstract_window (view->win));
    string prefix= "tmfs://window/";
    if (starts (id, prefix))
      snapshot.window_id= static_cast<std::uint64_t> (
        as_int (id (N (prefix), N (id))));
    snapshot.header_visible= view->win->get_header_flag ();
    snapshot.footer_visible= view->win->get_footer_flag ();
    for (int i= 0; i < 4; ++i)
      if (view->win->get_icon_bar_flag (i))
        snapshot.icon_bar_mask |= static_cast<std::uint32_t> (1U << i);
    for (int i= 0; i < 2; ++i)
      if (view->win->get_bottom_tools_flag (i))
        snapshot.bottom_tools_mask |= static_cast<std::uint32_t> (1U << i);
  }
  endpoint_->update_viewport (snapshot);
}

void
qt_actor_widget_rep::drain_external_effects () {
  actor_command_transport::readable_command effect;
  while (endpoint_->try_effect (effect)) {
    const actor_command_record& record= *effect.record;
    switch (record.kind) {
    case actor_command_kind::ui_invalidate:
      send (SLOT_INVALIDATE, close_box<coord4> (coord4 (
        static_cast<SI> (record.argument[0]),
        static_cast<SI> (record.argument[1]),
        static_cast<SI> (record.argument[2]),
        static_cast<SI> (record.argument[3]))));
      break;
    case actor_command_kind::ui_invalidate_all:
      invalidate_all ();
      break;
    case actor_command_kind::ui_scroll_to: {
      std::uint64_t user_guard= record.argument[3];
      bool user_generation_matches=
        user_guard == 0 ||
        endpoint_->user_scroll_generation () + 1 == user_guard;
      if (user_generation_matches) {
        ::set_scroll_position (
          widget (this), static_cast<SI> (record.argument[0]),
          static_cast<SI> (record.argument[1]));
        refresh_viewport ();
      }
      endpoint_->mark_programmatic_scroll_applied (record.argument[2]);
      break;
    }
    case actor_command_kind::ui_set_extents:
      ::set_extents (
        widget (this), static_cast<SI> (record.argument[0]),
        static_cast<SI> (record.argument[1]),
        static_cast<SI> (record.argument[2]),
        static_cast<SI> (record.argument[3]));
      refresh_viewport ();
      break;
    case actor_command_kind::ui_set_cursor:
      ::send_cursor (
        widget (this), static_cast<SI> (record.argument[0]),
        static_cast<SI> (record.argument[1]));
      break;
    case actor_command_kind::ui_set_pointer: {
      string pointer=
        actor_text_registry::instance ().take (record.payload0);
      if (record.argument[0] == 0)
        ::send_mouse_pointer (widget (this), pointer);
      else {
        int split= 0;
        while (split < N (pointer) && pointer[split] != '\0') ++split;
        string cursor= pointer (0, split);
        string mask= split < N (pointer) ? pointer (split + 1, N (pointer)) :
          string ();
        ::send_mouse_pointer (widget (this), cursor, mask);
      }
      break;
    }
    case actor_command_kind::ui_focus_view: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr) set_current_view (abstract_view (view));
      break;
    }
    case actor_command_kind::ui_refresh_chrome:
      windows_delayed_refresh (static_cast<int> (record.argument[0]));
      break;
    case actor_command_kind::ui_global_action: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr) set_current_view (abstract_view (view));
      (void) actor_ui_invoke_action (record.argument[0]);
      break;
    }
    case actor_command_kind::ui_new_buffer: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view == nullptr) break;
      set_current_view (abstract_view (view));
      (void) create_buffer ();
      break;
    }
    case actor_command_kind::ui_open_document_window: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr) set_current_view (abstract_view (view));
      open_document_window (record.argument[0] != 0);
      break;
    }
    case actor_command_kind::ui_close_buffer: {
      const athena_actor_id closing_actor_id= record.argument[0];
      QTimer::singleShot (0, qApp, [closing_actor_id] {
        kill_buffer_by_actor_id (closing_actor_id);
      });
      break;
    }
    case actor_command_kind::ui_choose_file: {
      string title= actor_text_registry::instance ().take (record.payload0);
      string type= actor_text_registry::instance ().take (record.payload1);
      widget chooser= actor_ui_take_widget (record.argument[0]);
      tm_view view= concrete_runtime_view (view_id_);
      if (is_nil (chooser) || view == nullptr) break;
      set_current_view (abstract_view (view));
      (void) ::plain_window_widget (chooser, ui_text (title));
      if (type == "directory")
        ::send_keyboard_focus (::get_directory (chooser));
      else
        ::send_keyboard_focus (::get_file (chooser));
      break;
    }
    case actor_command_kind::ui_vault_backup_dispatch_realtime: {
      string saved_file=
        actor_text_registry::instance ().take (record.payload0);
      qtm_vault_backup_dispatch_realtime (to_qstring (saved_file));
      break;
    }
    case actor_command_kind::ui_vault_explorer_track_file: {
      string file= actor_text_registry::instance ().take (record.payload0);
      vault_explorer_track_file (url (std::move (file)));
      break;
    }
    case actor_command_kind::ui_outline_snapshot:
      outline_pane_accept_snapshot (
        record.view_id, record.payload0, record.argument[0]);
      break;
    case actor_command_kind::ui_document_search_state:
      QTMDocumentSearchBar::acceptState (
        canvas (), record.view_id, record.argument[0],
        static_cast<int> (record.argument[1]),
        static_cast<int> (record.argument[2]),
        static_cast<int> (record.argument[3]));
      break;
    case actor_command_kind::ui_keyboard_focus_field: {
      string field= actor_text_registry::instance ().take (record.payload0);
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr && view->win != nullptr)
        ::send_keyboard_focus_on (view->win->wid, field);
      break;
    }
    case actor_command_kind::ui_mouse_grab:
      ::send_mouse_grab (widget (this), record.argument[0] != 0);
      break;
    case actor_command_kind::ui_native_drawing_focus_refresh: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr && view->win != nullptr)
        refresh_native_drawing_focus_actions (view->win->wid);
      break;
    }
    case actor_command_kind::ui_menu_main: {
      widget menu= actor_ui_take_widget (record.argument[0]);
      tm_view view= concrete_runtime_view (view_id_);
      if (!is_nil (menu) && view != nullptr && view->win != nullptr)
        ::set_main_menu (view->win->wid, menu);
      break;
    }
    case actor_command_kind::ui_menu_icons: {
      widget icons= actor_ui_take_widget (record.argument[0]);
      tm_view view= concrete_runtime_view (view_id_);
      int which= static_cast<int> (record.argument[1]);
      if (is_nil (icons) || view == nullptr || view->win == nullptr) break;
      if (which == 0) ::set_main_icons (view->win->wid, icons);
      else if (which == 1) ::set_mode_icons (view->win->wid, icons);
      else if (which == 2) ::set_focus_icons (view->win->wid, icons);
      else if (which == 3) ::set_user_icons (view->win->wid, icons);
      break;
    }
    case actor_command_kind::ui_bottom_tools: {
      widget tools= actor_ui_take_widget (record.argument[0]);
      tm_view view= concrete_runtime_view (view_id_);
      int which= static_cast<int> (record.argument[1]);
      if (is_nil (tools) || view == nullptr || view->win == nullptr) break;
      if (which == 0) ::set_bottom_tools (view->win->wid, tools);
      else if (which == 1) ::set_extra_tools (view->win->wid, tools);
      break;
    }
    case actor_command_kind::ui_footer_left:
    case actor_command_kind::ui_footer_center:
    case actor_command_kind::ui_footer_right: {
      string text= actor_text_registry::instance ().take (record.payload0);
      if (record.kind == actor_command_kind::ui_footer_left)
        get_server ()->set_left_footer (text);
      else if (record.kind == actor_command_kind::ui_footer_center)
        get_server ()->set_center_footer (text);
      else get_server ()->set_right_footer (text);
      break;
    }
    case actor_command_kind::ui_show_toast: {
      string left= actor_text_registry::instance ().take (record.payload0);
      string right= actor_text_registry::instance ().take (record.payload1);
      (void) qtm_show_toast (std::move (left), std::move (right));
      break;
    }
    case actor_command_kind::ui_switch_to_buffer: {
      string encoded_name=
        actor_text_registry::instance ().take (record.payload0);
      switch_to_buffer_from_actor (std::move (encoded_name));
      break;
    }
    case actor_command_kind::ui_rename_buffer: {
      string new_name= actor_text_registry::instance ().take (record.payload0);
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr)
        rename_buffer_from_actor (view->buf, url (std::move (new_name)));
      break;
    }
    case actor_command_kind::ui_set_buffer_title: {
      string title= actor_text_registry::instance ().take (record.payload0);
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr)
        set_proposed_title_buffer (view->buf->buf->name, std::move (title));
      break;
    }
    case actor_command_kind::ui_show_popup: {
      if (!is_nil (popup_window_)) {
        set_visibility (popup_window_, false);
        destroy_window_widget (popup_window_);
      }
      widget contents= actor_ui_take_widget (record.argument[0]);
      if (is_nil (contents)) break;
      popup_content_= ::popup_widget (contents);
      popup_window_= ::popup_window_widget (popup_content_, "Popup menu");
      SI x= static_cast<SI> (record.argument[1]);
      SI y= static_cast<SI> (record.argument[2]);
      SI px, py;
      if (qt_widget_global_position (this, x, y, px, py))
        set_position (popup_window_, px, py);
      set_visibility (popup_window_, true);
      send_keyboard_focus (widget (this));
      send_mouse_grab (popup_content_, true);
      break;
    }
    case actor_command_kind::ui_show_completion: {
      string encoded= actor_text_registry::instance ().take (record.payload0);
      auto reject= [&] {
        (void) buffer_actor::submit_to (actor_id_, actor_command_kind::completion_choice,
          view_id_, 0, 0, SCHEME_CAPABILITY_BUFFER, record.argument[0],
          static_cast<std::uint64_t> (-1));
      };
      if (!canvas () || !canvas ()->isVisible () ||
          !(canvas ()->hasFocus () || canvas ()->surface ()->hasFocus ())) {
        reject ();
        break;
      }
      QStringList items;
      for (const auto& item: QJsonDocument::fromJson (
             QByteArray (as_charp (encoded), N(encoded))).array ())
        items.append (item.toString ());
      SI gx, gy;
      if (items.isEmpty () || !qt_widget_global_position (this,
            static_cast<SI> (record.argument[1]), static_cast<SI> (record.argument[2]), gx, gy)) {
        reject ();
        break;
      }
      if (!completion_popup_) {
        auto actor= actor_id_;
        auto view= view_id_;
        completion_popup_= new QTMCompletionPopup (canvas (),
          [actor, view] (std::uint64_t session, int index) {
            (void) buffer_actor::submit_to (actor, actor_command_kind::completion_choice,
              view, 0, 0, SCHEME_CAPABILITY_BUFFER, session,
              static_cast<std::uint64_t> (index));
          });
      }
      completion_popup_->present (record.argument[0], items,
        to_qpoint (coord2 (gx, gy)), static_cast<int> (record.argument[3]));
      break;
    }
    case actor_command_kind::ui_select_completion:
      if (completion_popup_) completion_popup_->select (record.argument[0], record.argument[1]);
      break;
    case actor_command_kind::ui_close_completion:
      if (completion_popup_) completion_popup_->dismiss (record.argument[0]);
      break;
    case actor_command_kind::ui_close_popup:
      if (!is_nil (popup_window_)) {
        set_visibility (popup_window_, false);
        destroy_window_widget (popup_window_);
        popup_window_= widget ();
        popup_content_= widget ();
      }
      break;
    case actor_command_kind::ui_set_scrollbars: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr && view->win != nullptr)
        view->win->set_scrollbars (static_cast<int> (record.argument[0]));
      break;
    }
    case actor_command_kind::ui_show_header: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr && view->win != nullptr)
        view->win->set_header_flag (record.argument[0] != 0);
      break;
    }
    case actor_command_kind::ui_show_icon_bar: {
      tm_view view= concrete_runtime_view (view_id_);
      int which= static_cast<int> (record.argument[0]);
      if (view != nullptr && view->win != nullptr && which >= 0 && which < 4)
        view->win->set_icon_bar_flag (which, record.argument[1] != 0);
      break;
    }
    case actor_command_kind::ui_show_bottom_tools: {
      tm_view view= concrete_runtime_view (view_id_);
      int which= static_cast<int> (record.argument[0]);
      if (view != nullptr && view->win != nullptr && which >= 0 && which < 2)
        view->win->set_bottom_tools_flag (which, record.argument[1] != 0);
      break;
    }
    case actor_command_kind::ui_show_footer: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr && view->win != nullptr)
        view->win->set_footer_flag (record.argument[0] != 0);
      break;
    }
    case actor_command_kind::ui_set_modified: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr)
        publish_buffer_menu_modified (view->buf, record.argument[0] != 0);
      if (view != nullptr && view->win != nullptr)
        view->win->set_modified (record.argument[0] != 0);
      break;
    }
    case actor_command_kind::ui_mark_buffer_saved: {
      tm_view view= concrete_runtime_view (view_id_);
      if (view != nullptr) {
        publish_buffer_menu_modified (view->buf, false);
        view->buf->buf->last_save= static_cast<int> (record.argument[0]);
        array<url> windows= buffer_to_windows (view->buf->buf->name);
        for (int i=0; i<N(windows); i++)
          concrete_window (windows[i])->set_modified (false);
      }
      break;
    }
    case actor_command_kind::ui_schedule_scheme:
      schedule_delayed_scheme_handle (
        record.argument[0], actor_id_, view_id_, record.argument[1] != 0);
      break;
    case actor_command_kind::ui_schedule_global_scheme:
      schedule_delayed_scheme_handle (
        record.argument[0], ATHENA_NO_ACTOR, view_id_, false, true);
      break;
    case actor_command_kind::ui_scheme_completed:
      complete_delayed_scheme_handle (
        record.argument[0], actor_id_, view_id_, record.argument[1] != 0,
        static_cast<std::int64_t> (record.argument[2]));
      break;
    default:
      break;
    }
    endpoint_->complete_effect (effect.slot);
  }
  refresh_viewport ();
}

widget
actor_editor_widget (
  athena_actor_id actor_id, athena_view_id view_id, bool embedded) {
  return tm_new<qt_actor_widget_rep> (actor_id, view_id, embedded);
}
