/******************************************************************************
* MODULE     : actor_ui_bridge.cpp
* DESCRIPTION: Shared viewport snapshot and ID-only actor-to-Qt effects
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "actor_ui_bridge.hpp"
#include "widget.hpp"

#include <memory>
#include <mutex>
#include <cstring>
#include <unordered_map>

namespace {

std::uint64_t
double_bits (double value) noexcept {
  std::uint64_t result;
  std::memcpy (&result, &value, sizeof (result));
  return result;
}

double
bits_double (std::uint64_t value) noexcept {
  double result;
  std::memcpy (&result, &value, sizeof (result));
  return result;
}

std::uint32_t
coalesced_command_mask (actor_command_kind kind) noexcept {
  switch (kind) {
  case actor_command_kind::apply_changes: return 1U << 0;
  case actor_command_kind::animate: return 1U << 1;
  case actor_command_kind::progressive_typeset: return 1U << 2;
  case actor_command_kind::render_view: return 1U << 3;
  case actor_command_kind::request_outline: return 1U << 4;
  default: return 0;
  }
}

std::mutex endpoint_registry_lock;
std::unordered_map<athena_view_id, std::unique_ptr<actor_ui_endpoint>>
  endpoint_registry;

std::mutex widget_registry_lock;
std::unordered_map<athena_resource_id, std::unique_ptr<widget>>
  widget_registry;
athena_resource_id next_widget_id= 1;

std::mutex action_registry_lock;
std::unordered_map<actor_ui_action, athena_resource_id> action_ids;
std::unordered_map<athena_resource_id, actor_ui_action> action_registry;
athena_resource_id next_action_id= 1;

} // namespace

actor_ui_endpoint::actor_ui_endpoint (athena_view_id view_id):
  view_id_ (view_id), zoom_factor_bits_ (double_bits (1.0)),
  effects_ (128), next_effect_id_ (1) {}

athena_view_id
actor_ui_endpoint::view_id () const noexcept {
  return view_id_;
}

void
actor_ui_endpoint::update_viewport (
  const actor_viewport_snapshot& snapshot) noexcept {
  std::uint64_t sequence=
    viewport_sequence_.load (std::memory_order_relaxed);
  viewport_sequence_.store (sequence + 1, std::memory_order_release);
  viewport_.visible_x1.store (snapshot.visible_x1, std::memory_order_relaxed);
  viewport_.visible_y1.store (snapshot.visible_y1, std::memory_order_relaxed);
  viewport_.visible_x2.store (snapshot.visible_x2, std::memory_order_relaxed);
  viewport_.visible_y2.store (snapshot.visible_y2, std::memory_order_relaxed);
  viewport_.window_width.store (
    snapshot.window_width, std::memory_order_relaxed);
  viewport_.window_height.store (
    snapshot.window_height, std::memory_order_relaxed);
  viewport_.window_x.store (snapshot.window_x, std::memory_order_relaxed);
  viewport_.window_y.store (snapshot.window_y, std::memory_order_relaxed);
  viewport_.canvas_x.store (snapshot.canvas_x, std::memory_order_relaxed);
  viewport_.canvas_y.store (snapshot.canvas_y, std::memory_order_relaxed);
  viewport_.scroll_x.store (snapshot.scroll_x, std::memory_order_relaxed);
  viewport_.scroll_y.store (snapshot.scroll_y, std::memory_order_relaxed);
  viewport_.scrollbar_width.store (
    snapshot.scrollbar_width, std::memory_order_relaxed);
  viewport_.render_origin_x.store (
    snapshot.render_origin_x, std::memory_order_relaxed);
  viewport_.render_origin_y.store (
    snapshot.render_origin_y, std::memory_order_relaxed);
  viewport_.render_width.store (
    snapshot.render_width, std::memory_order_relaxed);
  viewport_.render_height.store (
    snapshot.render_height, std::memory_order_relaxed);
  viewport_.render_pixel_ratio_bits.store (
    double_bits (snapshot.render_pixel_ratio), std::memory_order_relaxed);
  viewport_.window_id.store (snapshot.window_id, std::memory_order_relaxed);
  viewport_.icon_bar_mask.store (
    snapshot.icon_bar_mask, std::memory_order_relaxed);
  viewport_.side_tools_mask.store (
    snapshot.side_tools_mask, std::memory_order_relaxed);
  viewport_.bottom_tools_mask.store (
    snapshot.bottom_tools_mask, std::memory_order_relaxed);
  viewport_.attached.store (snapshot.attached, std::memory_order_relaxed);
  viewport_.focused.store (snapshot.focused, std::memory_order_relaxed);
  viewport_.full_screen.store (
    snapshot.full_screen, std::memory_order_relaxed);
  viewport_.invalid.store (snapshot.invalid, std::memory_order_relaxed);
  viewport_.header_visible.store (
    snapshot.header_visible, std::memory_order_relaxed);
  viewport_.footer_visible.store (
    snapshot.footer_visible, std::memory_order_relaxed);
  viewport_sequence_.store (sequence + 2, std::memory_order_release);
}

actor_viewport_snapshot
actor_ui_endpoint::viewport () const noexcept {
  actor_viewport_snapshot result;
  while (true) {
    std::uint64_t before=
      viewport_sequence_.load (std::memory_order_acquire);
    if ((before & 1U) != 0) continue;
    result.visible_x1= viewport_.visible_x1.load (std::memory_order_relaxed);
    result.visible_y1= viewport_.visible_y1.load (std::memory_order_relaxed);
    result.visible_x2= viewport_.visible_x2.load (std::memory_order_relaxed);
    result.visible_y2= viewport_.visible_y2.load (std::memory_order_relaxed);
    result.window_width=
      viewport_.window_width.load (std::memory_order_relaxed);
    result.window_height=
      viewport_.window_height.load (std::memory_order_relaxed);
    result.window_x= viewport_.window_x.load (std::memory_order_relaxed);
    result.window_y= viewport_.window_y.load (std::memory_order_relaxed);
    result.canvas_x= viewport_.canvas_x.load (std::memory_order_relaxed);
    result.canvas_y= viewport_.canvas_y.load (std::memory_order_relaxed);
    result.scroll_x= viewport_.scroll_x.load (std::memory_order_relaxed);
    result.scroll_y= viewport_.scroll_y.load (std::memory_order_relaxed);
    result.scrollbar_width=
      viewport_.scrollbar_width.load (std::memory_order_relaxed);
    result.render_origin_x=
      viewport_.render_origin_x.load (std::memory_order_relaxed);
    result.render_origin_y=
      viewport_.render_origin_y.load (std::memory_order_relaxed);
    result.render_width=
      viewport_.render_width.load (std::memory_order_relaxed);
    result.render_height=
      viewport_.render_height.load (std::memory_order_relaxed);
    result.render_pixel_ratio= bits_double (
      viewport_.render_pixel_ratio_bits.load (std::memory_order_relaxed));
    result.window_id= viewport_.window_id.load (std::memory_order_relaxed);
    result.icon_bar_mask=
      viewport_.icon_bar_mask.load (std::memory_order_relaxed);
    result.side_tools_mask=
      viewport_.side_tools_mask.load (std::memory_order_relaxed);
    result.bottom_tools_mask=
      viewport_.bottom_tools_mask.load (std::memory_order_relaxed);
    result.attached= viewport_.attached.load (std::memory_order_relaxed);
    result.focused= viewport_.focused.load (std::memory_order_relaxed);
    result.full_screen= viewport_.full_screen.load (std::memory_order_relaxed);
    result.invalid= viewport_.invalid.load (std::memory_order_relaxed);
    result.header_visible=
      viewport_.header_visible.load (std::memory_order_relaxed);
    result.footer_visible=
      viewport_.footer_visible.load (std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_acquire);
    std::uint64_t after=
      viewport_sequence_.load (std::memory_order_relaxed);
    if (before == after) return result;
  }
}

void
actor_ui_endpoint::set_wheel_capture (bool capture) noexcept {
  wheel_capture_.store (capture, std::memory_order_release);
}

bool
actor_ui_endpoint::wheel_capture () const noexcept {
  return wheel_capture_.load (std::memory_order_acquire);
}

void
actor_ui_endpoint::set_zoom_factor (double zoom) noexcept {
  zoom_factor_bits_.store (double_bits (zoom), std::memory_order_release);
}

double
actor_ui_endpoint::zoom_factor () const noexcept {
  return bits_double (zoom_factor_bits_.load (std::memory_order_acquire));
}

bool
actor_ui_endpoint::publish (
  actor_command_kind kind, athena_blob_id payload0,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  return publish_pair (kind, payload0, ATHENA_NO_BLOB, argument0, argument1,
                       argument2, argument3);
}

bool
actor_ui_endpoint::publish_pair (
  actor_command_kind kind, athena_blob_id payload0, athena_blob_id payload1,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  actor_command_transport::writable_command effect;
  if (!effects_.acquire (effect)) return false;
  effect.record->command_id= next_effect_id_++;
  effect.record->view_id= view_id_;
  effect.record->payload0= payload0;
  effect.record->payload1= payload1;
  effect.record->kind= kind;
  effect.record->argument[0]= argument0;
  effect.record->argument[1]= argument1;
  effect.record->argument[2]= argument2;
  effect.record->argument[3]= argument3;
  if (effects_.publish (effect.slot)) return true;
  (void) effects_.discard (effect.slot);
  return false;
}

bool
actor_ui_endpoint::publish_text (
  actor_command_kind kind, string text,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  athena_blob_id payload=
    actor_text_registry::instance ().store (std::move (text));
  if (publish (kind, payload, argument0, argument1, argument2, argument3))
    return true;
  (void) actor_text_registry::instance ().discard (payload);
  return false;
}

bool
actor_ui_endpoint::publish_text_pair (
  actor_command_kind kind, string first, string second,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  athena_blob_id first_payload=
    actor_text_registry::instance ().store (std::move (first));
  athena_blob_id second_payload=
    actor_text_registry::instance ().store (std::move (second));
  if (publish_pair (kind, first_payload, second_payload, argument0, argument1,
                    argument2, argument3))
    return true;
  (void) actor_text_registry::instance ().discard (first_payload);
  (void) actor_text_registry::instance ().discard (second_payload);
  return false;
}

bool
actor_ui_endpoint::begin_coalesced_command (
  actor_command_kind kind) noexcept {
  std::uint32_t mask= coalesced_command_mask (kind);
  ASSERT (mask != 0, "command is not coalescible");
  return (pending_commands_.fetch_or (mask, std::memory_order_relaxed) & mask)
         == 0;
}

void
actor_ui_endpoint::finish_coalesced_command (
  actor_command_kind kind) noexcept {
  std::uint32_t mask= coalesced_command_mask (kind);
  ASSERT (mask != 0, "command is not coalescible");
  pending_commands_.fetch_and (~mask, std::memory_order_relaxed);
}

bool
actor_ui_endpoint::try_effect (
  actor_command_transport::readable_command& effect) {
  return effects_.try_command (effect);
}

void
actor_ui_endpoint::complete_effect (athena_slot_id slot) {
  (void) effects_.complete (slot);
}

void
actor_ui_endpoint::close () noexcept {
  effects_.close ();
}

actor_ui_endpoint*
register_actor_ui_endpoint (athena_view_id view_id) {
  ASSERT (view_id != ATHENA_NO_VIEW, "cannot register a null view endpoint");
  auto endpoint= std::make_unique<actor_ui_endpoint> (view_id);
  actor_ui_endpoint* result= endpoint.get ();
  std::lock_guard<std::mutex> guard (endpoint_registry_lock);
  auto inserted= endpoint_registry.emplace (view_id, std::move (endpoint));
  ASSERT (inserted.second, "duplicate actor UI endpoint");
  return result;
}

actor_ui_endpoint*
find_actor_ui_endpoint (athena_view_id view_id) noexcept {
  std::lock_guard<std::mutex> guard (endpoint_registry_lock);
  auto found= endpoint_registry.find (view_id);
  return found == endpoint_registry.end () ? nullptr : found->second.get ();
}

void
unregister_actor_ui_endpoint (athena_view_id view_id) noexcept {
  std::unique_ptr<actor_ui_endpoint> endpoint;
  {
    std::lock_guard<std::mutex> guard (endpoint_registry_lock);
    auto found= endpoint_registry.find (view_id);
    if (found == endpoint_registry.end ()) return;
    endpoint= std::move (found->second);
    endpoint_registry.erase (found);
  }
  endpoint->close ();
}

athena_resource_id
actor_ui_store_widget (widget&& value) {
  auto owned= std::make_unique<widget> (std::move (value));
  std::lock_guard<std::mutex> guard (widget_registry_lock);
  athena_resource_id id= next_widget_id++;
  if (id == 0) id= next_widget_id++;
  widget_registry.emplace (id, std::move (owned));
  return id;
}

widget
actor_ui_take_widget (athena_resource_id id) {
  std::unique_ptr<widget> owned;
  {
    std::lock_guard<std::mutex> guard (widget_registry_lock);
    auto found= widget_registry.find (id);
    if (found == widget_registry.end ()) return widget ();
    owned= std::move (found->second);
    widget_registry.erase (found);
  }
  return std::move (*owned);
}

bool
actor_ui_discard_widget (athena_resource_id id) noexcept {
  std::unique_ptr<widget> owned;
  {
    std::lock_guard<std::mutex> guard (widget_registry_lock);
    auto found= widget_registry.find (id);
    if (found == widget_registry.end ()) return false;
    owned= std::move (found->second);
    widget_registry.erase (found);
  }
  return true;
}

athena_resource_id
actor_ui_register_action (actor_ui_action action) {
  if (action == nullptr) return 0;
  std::lock_guard<std::mutex> guard (action_registry_lock);
  auto found= action_ids.find (action);
  if (found != action_ids.end ()) return found->second;
  athena_resource_id id= next_action_id++;
  if (id == 0) id= next_action_id++;
  action_ids.emplace (action, id);
  action_registry.emplace (id, action);
  return id;
}

bool
actor_ui_invoke_action (athena_resource_id id) {
  actor_ui_action action= nullptr;
  {
    std::lock_guard<std::mutex> guard (action_registry_lock);
    auto found= action_registry.find (id);
    if (found == action_registry.end ()) return false;
    action= found->second;
  }
  action ();
  return true;
}
