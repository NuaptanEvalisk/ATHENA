/******************************************************************************
* MODULE     : actor_ui_bridge.hpp
* DESCRIPTION: Shared viewport snapshot and ID-only actor-to-Qt effects
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ACTOR_UI_BRIDGE_H
#define ACTOR_UI_BRIDGE_H

#include "actor_transport.hpp"
#include "renderer.hpp"

#include <atomic>
#include <cstdint>

class widget;

struct actor_viewport_snapshot {
  SI visible_x1= 0;
  SI visible_y1= 0;
  SI visible_x2= 0;
  SI visible_y2= 0;
  SI window_width= 0;
  SI window_height= 0;
  SI window_x= 0;
  SI window_y= 0;
  SI canvas_x= 0;
  SI canvas_y= 0;
  SI scroll_x= 0;
  SI scroll_y= 0;
  SI scrollbar_width= 0;
  SI render_origin_x= 0;
  SI render_origin_y= 0;
  SI render_width= 0;
  SI render_height= 0;
  double render_pixel_ratio= 1.0;
  std::uint64_t window_id= 0;
  std::uint32_t icon_bar_mask= 0;
  std::uint32_t side_tools_mask= 0;
  std::uint32_t bottom_tools_mask= 0;
  bool attached= false;
  bool focused= false;
  bool full_screen= false;
  bool invalid= false;
  bool header_visible= false;
  bool footer_visible= false;
};

class actor_ui_endpoint {
public:
  explicit actor_ui_endpoint (athena_view_id view_id);

  actor_ui_endpoint (const actor_ui_endpoint&)= delete;
  actor_ui_endpoint& operator = (const actor_ui_endpoint&)= delete;

  athena_view_id view_id () const noexcept;
  void update_viewport (const actor_viewport_snapshot& snapshot) noexcept;
  actor_viewport_snapshot viewport () const noexcept;

  bool publish (actor_command_kind kind,
                athena_blob_id payload0= ATHENA_NO_BLOB,
                std::uint64_t argument0= 0, std::uint64_t argument1= 0,
                std::uint64_t argument2= 0, std::uint64_t argument3= 0);
  bool publish_pair (actor_command_kind kind, athena_blob_id payload0,
                     athena_blob_id payload1,
                     std::uint64_t argument0= 0,
                     std::uint64_t argument1= 0,
                     std::uint64_t argument2= 0,
                     std::uint64_t argument3= 0);
  bool publish_text (actor_command_kind kind, string text,
                     std::uint64_t argument0= 0,
                     std::uint64_t argument1= 0,
                     std::uint64_t argument2= 0,
                     std::uint64_t argument3= 0);
  bool publish_text_pair (actor_command_kind kind, string first, string second,
                          std::uint64_t argument0= 0,
                          std::uint64_t argument1= 0,
                          std::uint64_t argument2= 0,
                          std::uint64_t argument3= 0);
  bool begin_coalesced_command (actor_command_kind kind) noexcept;
  void finish_coalesced_command (actor_command_kind kind) noexcept;
  bool try_effect (actor_command_transport::readable_command& effect);
  void complete_effect (athena_slot_id slot);
  void close () noexcept;

private:
  struct atomic_viewport {
    std::atomic<SI> visible_x1 {0};
    std::atomic<SI> visible_y1 {0};
    std::atomic<SI> visible_x2 {0};
    std::atomic<SI> visible_y2 {0};
    std::atomic<SI> window_width {0};
    std::atomic<SI> window_height {0};
    std::atomic<SI> window_x {0};
    std::atomic<SI> window_y {0};
    std::atomic<SI> canvas_x {0};
    std::atomic<SI> canvas_y {0};
    std::atomic<SI> scroll_x {0};
    std::atomic<SI> scroll_y {0};
    std::atomic<SI> scrollbar_width {0};
    std::atomic<SI> render_origin_x {0};
    std::atomic<SI> render_origin_y {0};
    std::atomic<SI> render_width {0};
    std::atomic<SI> render_height {0};
    std::atomic<std::uint64_t> render_pixel_ratio_bits {0};
    std::atomic<std::uint64_t> window_id {0};
    std::atomic<std::uint32_t> icon_bar_mask {0};
    std::atomic<std::uint32_t> side_tools_mask {0};
    std::atomic<std::uint32_t> bottom_tools_mask {0};
    std::atomic<bool> attached {false};
    std::atomic<bool> focused {false};
    std::atomic<bool> full_screen {false};
    std::atomic<bool> invalid {false};
    std::atomic<bool> header_visible {false};
    std::atomic<bool> footer_visible {false};
  };

  const athena_view_id view_id_;
  mutable std::atomic<std::uint64_t> viewport_sequence_ {0};
  atomic_viewport viewport_;
  actor_command_transport effects_;
  std::uint64_t next_effect_id_;
  std::atomic<std::uint32_t> pending_commands_ {0};
};

actor_ui_endpoint* register_actor_ui_endpoint (athena_view_id view_id);
actor_ui_endpoint* find_actor_ui_endpoint (athena_view_id view_id) noexcept;
void unregister_actor_ui_endpoint (athena_view_id view_id) noexcept;

athena_resource_id actor_ui_store_widget (widget&& value);
widget actor_ui_take_widget (athena_resource_id id);
bool actor_ui_discard_widget (athena_resource_id id) noexcept;

using actor_ui_action= void (*) ();
athena_resource_id actor_ui_register_action (actor_ui_action action);
bool actor_ui_invoke_action (athena_resource_id id);

#endif // defined ACTOR_UI_BRIDGE_H
