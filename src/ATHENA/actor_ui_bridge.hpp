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
#include "native_ink.hpp"
#include "renderer.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

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
  int window_serial= 0;
  std::uint32_t icon_bar_mask= 0;
  std::uint32_t bottom_tools_mask= 0;
  bool attached= false;
  bool focused= false;
  bool full_screen= false;
  bool invalid= false;
  bool header_visible= false;
  bool footer_visible= false;
};

enum actor_editor_command_state_flag: std::uint32_t {
  ACTOR_EDITOR_COMMAND_STATE_VALID= 1U << 0,
  ACTOR_EDITOR_COMMAND_STATE_READ_ONLY= 1U << 1,
  ACTOR_EDITOR_COMMAND_STATE_SELECTION= 1U << 2,
  ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_SELECTION= 1U << 3,
  ACTOR_EDITOR_COMMAND_STATE_FOCUS_NODE= 1U << 4,
  ACTOR_EDITOR_COMMAND_STATE_MATH_MODE= 1U << 5,
  ACTOR_EDITOR_COMMAND_STATE_PRESENTATION_MODE= 1U << 6,
  ACTOR_EDITOR_COMMAND_STATE_SCREENS_MODE= 1U << 7,
  ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE= 1U << 8,
  ACTOR_EDITOR_COMMAND_STATE_PROG_MODE= 1U << 9,
  ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE= 1U << 10,
  ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_MODE= 1U << 11,
  ACTOR_EDITOR_COMMAND_STATE_POSTER_STYLE= 1U << 12,
  ACTOR_EDITOR_COMMAND_STATE_MANUAL_STYLE= 1U << 13,
  ACTOR_EDITOR_COMMAND_STATE_HEADER_LETTER= 1U << 14,
  ACTOR_EDITOR_COMMAND_STATE_BOOK_STYLE= 1U << 15,
  ACTOR_EDITOR_COMMAND_STATE_SECTION_BASE= 1U << 16,
  ACTOR_EDITOR_COMMAND_STATE_ENV_THEOREM= 1U << 17,
  ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP= 1U << 18,
  ACTOR_EDITOR_COMMAND_STATE_STD_LIST= 1U << 19,
  ACTOR_EDITOR_COMMAND_STATE_ENV_FLOAT= 1U << 20,
  ACTOR_EDITOR_COMMAND_STATE_STD_FOLD= 1U << 21,
  ACTOR_EDITOR_COMMAND_STATE_STD_DTD= 1U << 22,
  ACTOR_EDITOR_COMMAND_STATE_NON_SMALL_SELECTION= 1U << 23,
  ACTOR_EDITOR_COMMAND_STATE_INSIDE_LETTER_HEADER= 1U << 24,
  ACTOR_EDITOR_COMMAND_STATE_INSIDE_FLOAT_OR_FOOTNOTE= 1U << 25,
  ACTOR_EDITOR_COMMAND_STATE_MAIN_FLOW= 1U << 26,
  ACTOR_EDITOR_COMMAND_STATE_ENV_MATH= 1U << 27,
  ACTOR_EDITOR_COMMAND_STATE_TMDOC_TRAVERSE= 1U << 28,
  ACTOR_EDITOR_COMMAND_STATE_TMDOC_EXPLAIN= 1U << 29,
  ACTOR_EDITOR_COMMAND_STATE_OVERLAYS_CONTEXT= 1U << 30,
  ACTOR_EDITOR_COMMAND_STATE_SCREENS_BUFFER= 1U << 31
};

struct actor_editor_command_snapshot {
  std::uint32_t flags= 0;
  std::uint16_t undo_count= 0;
  std::uint16_t redo_count= 0;

  bool valid () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_VALID) != 0;
  }
  bool read_only () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_READ_ONLY) != 0;
  }
  bool selection_active () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_SELECTION) != 0;
  }
  bool graphics_selection_active () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_SELECTION) != 0;
  }
  bool focus_node_available () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_FOCUS_NODE) != 0;
  }
  bool math_mode () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_MATH_MODE) != 0;
  }
  bool presentation_mode () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_PRESENTATION_MODE) != 0;
  }
  bool screens_mode () const noexcept {
    return (flags & ACTOR_EDITOR_COMMAND_STATE_SCREENS_MODE) != 0;
  }
  bool has (actor_editor_command_state_flag flag) const noexcept {
    return (flags & static_cast<std::uint32_t> (flag)) != 0;
  }
};

struct actor_dynamic_menu_item_snapshot {
  std::string group;
  std::string label;
  std::string key;
};

struct actor_dynamic_menu_snapshot {
  bool ready= false;
  std::vector<actor_dynamic_menu_item_snapshot> items;
};

enum actor_focus_toolbar_flag: std::uint32_t {
  ACTOR_FOCUS_TOOLBAR_VALID= 1U << 0,
  ACTOR_FOCUS_TOOLBAR_BUFFER= 1U << 1,
  ACTOR_FOCUS_TOOLBAR_CAN_MOVE= 1U << 2,
  ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE= 1U << 3,
  ACTOR_FOCUS_TOOLBAR_HORIZONTAL= 1U << 4,
  ACTOR_FOCUS_TOOLBAR_VERTICAL= 1U << 5,
  ACTOR_FOCUS_TOOLBAR_CAN_INSERT= 1U << 6,
  ACTOR_FOCUS_TOOLBAR_CAN_REMOVE= 1U << 7,
  ACTOR_FOCUS_TOOLBAR_CURSOR_INSIDE= 1U << 8,
  ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS= 1U << 9,
  ACTOR_FOCUS_TOOLBAR_HAS_PREFERENCES= 1U << 10,
  ACTOR_FOCUS_TOOLBAR_HAS_PARAMETERS= 1U << 11,
  ACTOR_FOCUS_TOOLBAR_CAN_SEARCH= 1U << 12,
  ACTOR_FOCUS_TOOLBAR_HAS_SEARCH_MENU= 1U << 13,
  ACTOR_FOCUS_TOOLBAR_HAS_LABEL= 1U << 14,
  ACTOR_FOCUS_TOOLBAR_HAS_HIDDEN_CHILDREN= 1U << 15,
  ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT= 1U << 16,
  ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT= 1U << 17,
  ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT= 1U << 18,
  ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT= 1U << 19,
  ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT= 1U << 20,
  ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT= 1U << 21,
  ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT= 1U << 22,
  ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT= 1U << 23,
  ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT= 1U << 24,
  ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT= 1U << 25,
  ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT= 1U << 26,
  ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT= 1U << 27,
  ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT= 1U << 28,
  ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT= 1U << 29,
  ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT= 1U << 30,
  ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT= 1U << 31
};

struct actor_focus_toolbar_snapshot {
  std::uint32_t flags= 0;
  std::string tag_label;
  std::string tag_name;
  std::vector<std::string> variants;
  std::vector<std::string> variant_names;
  std::string code_language;
  std::string document_style;
  std::string page_type;
  std::string document_font;
  std::string font_base_size;
  std::string document_language;
  std::string focus_label_value;
  std::string alternate_label;
  std::string alternate_icon;
  bool numbered_available= false;
  bool numbered_checked= false;
  bool alternate_available= false;
  bool alternate_checked= false;
  bool hidden_toggle_available= false;
  bool hidden_checked= false;
  bool algorithm_numbered= false;
  bool algorithm_named= false;
  bool algorithm_specified= false;
  bool detached_note_custom= false;
  bool figure_context= false;
  bool titled_named= false;
  bool frame_titled= false;

  bool valid () const noexcept {
    return (flags & ACTOR_FOCUS_TOOLBAR_VALID) != 0;
  }
  bool has (actor_focus_toolbar_flag flag) const noexcept {
    return (flags & static_cast<std::uint32_t> (flag)) != 0;
  }
};

class actor_ui_endpoint {
public:
  explicit actor_ui_endpoint (athena_view_id view_id);

  actor_ui_endpoint (const actor_ui_endpoint&)= delete;
  actor_ui_endpoint& operator = (const actor_ui_endpoint&)= delete;

  athena_view_id view_id () const noexcept;
  void update_viewport (const actor_viewport_snapshot& snapshot) noexcept;
  actor_viewport_snapshot viewport () const noexcept;
  void set_wheel_capture (bool capture) noexcept;
  void update_native_ink_regions (
    std::vector<native_ink_interaction_snapshot> regions) noexcept;
  std::vector<native_ink_interaction_snapshot> native_ink_regions () const;
  void update_native_drawing_selection (
    std::vector<native_drawing_selection_box> boxes) noexcept;
  std::vector<native_drawing_selection_box> native_drawing_selection () const;
  void update_native_drawing_properties (
    native_drawing_properties_snapshot snapshot) noexcept;
  native_drawing_properties_snapshot native_drawing_properties () const;
  void set_overlay_wheel_capture (bool capture) noexcept;
  bool overlay_wheel_capture () const noexcept {
    return overlay_wheel_capture_.load (std::memory_order_acquire);
  }
  bool wheel_capture () const noexcept;
  void set_zoom_factor (double zoom) noexcept;
  double zoom_factor () const noexcept;
  void mark_programmatic_scroll_applied (std::uint64_t generation) noexcept;
  std::uint64_t applied_programmatic_scroll_generation () const noexcept;
  void mark_user_scroll () noexcept;
  std::uint64_t user_scroll_generation () const noexcept;
  void update_editor_command_state (
    const actor_editor_command_snapshot& snapshot) noexcept;
  actor_editor_command_snapshot editor_command_state () const noexcept;
  void update_focus_toolbar_state (
    actor_focus_toolbar_snapshot snapshot) noexcept;
  actor_focus_toolbar_snapshot focus_toolbar_state () const;
  void set_prominent_spacing_available (bool available) noexcept;
  bool prominent_spacing_available () const noexcept;
  void set_inside_table (bool inside) noexcept;
  bool inside_table () const noexcept;
  bool begin_personal_macro_request () noexcept;
  void cancel_personal_macro_request () noexcept;
  void invalidate_personal_macro_items () noexcept;
  void update_personal_macro_items (
    std::vector<actor_dynamic_menu_item_snapshot> items) noexcept;
  actor_dynamic_menu_snapshot personal_macro_items () const;

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
    std::atomic<int> window_serial {0};
    std::atomic<std::uint32_t> icon_bar_mask {0};
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
  mutable std::mutex native_ink_lock_;
  std::vector<native_ink_interaction_snapshot> native_ink_regions_;
  mutable std::mutex native_drawing_selection_lock_;
  std::vector<native_drawing_selection_box> native_drawing_selection_;
  mutable std::mutex native_drawing_properties_lock_;
  native_drawing_properties_snapshot native_drawing_properties_;
  std::atomic<bool> wheel_capture_ {false};
  std::atomic<bool> overlay_wheel_capture_ {false};
  std::atomic<std::uint64_t> zoom_factor_bits_ {0};
  std::atomic<std::uint64_t> applied_programmatic_scroll_generation_ {0};
  std::atomic<std::uint64_t> user_scroll_generation_ {0};
  std::atomic<std::uint64_t> editor_command_state_ {0};
  std::atomic<bool> prominent_spacing_available_ {false};
  std::atomic<bool> inside_table_ {false};
  mutable std::mutex focus_toolbar_lock_;
  actor_focus_toolbar_snapshot focus_toolbar_state_;
  std::atomic<bool> personal_macro_request_pending_ {false};
  std::atomic<bool> personal_macro_ready_ {false};
  mutable std::mutex personal_macro_lock_;
  std::vector<actor_dynamic_menu_item_snapshot> personal_macro_items_;
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
