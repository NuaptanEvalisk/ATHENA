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
#include "tm_server.hpp"
#include "QTMToast.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMVaultBackupDispatcher.hpp"
#include "QTMVaultExplorer.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QStyle>
#include <QTimer>
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
  refresh_viewport ();
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::viewport_changed, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (width),
    static_cast<std::uint64_t> (height));
}

void
qt_actor_widget_rep::handle_keypress (string key, time_t time) {
  submit_text (actor_command_kind::key_press, std::move (key),
               static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_text_input (string text, time_t time) {
  submit_text (actor_command_kind::text_input, std::move (text),
               static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_keyboard_focus (bool focused, time_t time) {
  refresh_viewport ();
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::keyboard_focus, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    focused ? 1 : 0, static_cast<std::uint64_t> (time));
}

void
qt_actor_widget_rep::handle_cursor_blink (bool visible) {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::cursor_blink, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    visible ? 1 : 0);
}

void
qt_actor_widget_rep::handle_user_scroll (time_t time) {
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
qt_actor_widget_rep::handle_wheel_capture () {
  return endpoint_ != nullptr && endpoint_->wheel_capture ();
}

void
qt_actor_widget_rep::handle_zoom_by (bool zoom_in, double amount) {
  (void) buffer_actor::submit_to (
    actor_id_, actor_command_kind::zoom_by, view_id_,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    zoom_in ? 1 : 0, double_bits (amount));
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
  if (!submitted) invalidate_rect (x1, y2, x2, y1);
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
  actor_viewport_snapshot snapshot;
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
    for (int i= 0; i < 2; ++i) {
      if (view->win->get_side_tools_flag (i))
        snapshot.side_tools_mask |= static_cast<std::uint32_t> (1U << i);
      if (view->win->get_bottom_tools_flag (i))
        snapshot.bottom_tools_mask |= static_cast<std::uint32_t> (1U << i);
    }
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
      invalidate_rect (
        static_cast<int> (record.argument[0]),
        static_cast<int> (record.argument[1]),
        static_cast<int> (record.argument[2]),
        static_cast<int> (record.argument[3]));
      break;
    case actor_command_kind::ui_invalidate_all:
      invalidate_all ();
      break;
    case actor_command_kind::ui_scroll_to:
      ::set_scroll_position (
        widget (this), static_cast<SI> (record.argument[0]),
        static_cast<SI> (record.argument[1]));
      refresh_viewport ();
      break;
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
      get_server ()->dialogue_start (title, chooser);
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
    case actor_command_kind::ui_side_tools: {
      widget tools= actor_ui_take_widget (record.argument[0]);
      tm_view view= concrete_runtime_view (view_id_);
      int which= static_cast<int> (record.argument[1]);
      if (is_nil (tools) || view == nullptr || view->win == nullptr) break;
      if (which == 0) ::set_side_tools (view->win->wid, tools);
      else if (which == 1) ::set_left_tools (view->win->wid, tools);
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
    case actor_command_kind::ui_show_side_tools: {
      tm_view view= concrete_runtime_view (view_id_);
      int which= static_cast<int> (record.argument[0]);
      if (view != nullptr && view->win != nullptr && which >= 0 && which < 2)
        view->win->set_side_tools_flag (which, record.argument[1] != 0);
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
      if (view != nullptr && view->win != nullptr)
        view->win->set_modified (record.argument[0] != 0);
      break;
    }
    case actor_command_kind::ui_schedule_scheme:
      schedule_delayed_scheme_handle (
        record.argument[0], actor_id_, view_id_, record.argument[1] != 0);
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
