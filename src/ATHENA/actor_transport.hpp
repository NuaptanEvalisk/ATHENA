/******************************************************************************
* MODULE     : actor_transport.hpp
* DESCRIPTION: ID-only zero-copy actor command transport
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ACTOR_TRANSPORT_H
#define ACTOR_TRANSPORT_H

#include "string.hpp"
#include "tree.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

using athena_actor_id= std::uint64_t;
using athena_view_id= std::uint64_t;
using athena_slot_id= std::uint32_t;
using athena_blob_id= std::uint64_t;
using athena_scheme_handle_id= std::uint64_t;
using athena_resource_id= std::uint64_t;
using athena_response_id= std::uint64_t;

constexpr athena_actor_id ATHENA_NO_ACTOR= 0;
constexpr athena_view_id ATHENA_NO_VIEW= 0;
constexpr athena_blob_id ATHENA_NO_BLOB= 0;
constexpr athena_scheme_handle_id ATHENA_NO_SCHEME_HANDLE= 0;
constexpr athena_response_id ATHENA_NO_RESPONSE= 0;

enum class actor_command_kind: std::uint32_t {
  none= 0,
  barrier,
  create_view,
  destroy_view,
  initialize_view,
  suspend_view,
  resume_view,
  apply_changes,
  typeset_document,
  animate,
  progressive_typeset,
  init_style,
  typeset_invalidate_all,
  key_press,
  text_input,
  keyboard_focus,
  cursor_blink,
  user_scroll,
  mouse,
  set_zoom,
  viewport_changed,
  device_pixel_ratio_changed,
  center_message_state,
  render_view,
  set_render_connection,
  run_scheme_handle,
  invoke_scheme_handle,
  evaluate_widget_handle,
  release_scheme_handle,
  rename_buffer,
  save_buffer,
  autosave_buffer,
  replace_document,
  replace_body,
  set_buffer_read_only,
  set_buffer_title,
  snapshot_document,
  snapshot_body,
  request_outline,
  activate_outline_entry,
  set_master_buffer,
  notify_environment,
  query_modified,
  query_autosaved,
  mark_modified,
  mark_saved,
  mark_autosaved,
  attach_notifier,
  export_buffer,
  latex_expand_buffer,
  ui_invalidate,
  ui_invalidate_all,
  ui_scroll_to,
  ui_set_extents,
  ui_set_cursor,
  ui_set_pointer,
  ui_focus_view,
  ui_refresh_chrome,
  ui_global_action,
  ui_vault_explorer_track_file,
  ui_outline_snapshot,
  ui_keyboard_focus_field,
  ui_mouse_grab,
  ui_menu_main,
  ui_menu_icons,
  ui_side_tools,
  ui_bottom_tools,
  ui_footer_left,
  ui_footer_center,
  ui_footer_right,
  ui_show_toast,
  ui_switch_to_buffer,
  ui_show_popup,
  ui_close_popup,
  ui_set_scrollbars,
  ui_show_header,
  ui_show_icon_bar,
  ui_show_side_tools,
  ui_show_bottom_tools,
  ui_show_footer,
  ui_set_modified,
  ui_schedule_scheme,
  ui_scheme_completed
};

// Records remain in shared arena storage while being consumed.  All fields are
// numeric; variable data is represented only by a transferable blob id.
struct alignas(64) actor_command_record {
  std::uint64_t command_id= 0;
  athena_response_id response_id= ATHENA_NO_RESPONSE;
  athena_view_id view_id= ATHENA_NO_VIEW;
  athena_blob_id payload0= ATHENA_NO_BLOB;
  athena_blob_id payload1= ATHENA_NO_BLOB;
  std::uint64_t argument[16] {};
  actor_command_kind kind= actor_command_kind::none;
  std::uint32_t capabilities= 0;
};

static_assert (std::is_trivially_copyable<actor_command_record>::value,
               "actor command records must remain POD");
static_assert (alignof (actor_command_record) == 64,
               "actor command records must stay cache-line aligned");

class actor_command_transport {
public:
  struct writable_command {
    athena_slot_id slot= 0;
    actor_command_record* record= nullptr;
  };

  struct readable_command {
    athena_slot_id slot= 0;
    actor_command_record* record= nullptr;
  };

  explicit actor_command_transport (std::size_t slot_count= 64);
  ~actor_command_transport ();

  actor_command_transport (const actor_command_transport&)= delete;
  actor_command_transport& operator = (const actor_command_transport&)= delete;

  // Single producer API.
  bool acquire (writable_command& command);
  bool try_acquire (writable_command& command);
  bool discard (athena_slot_id slot) noexcept;
  bool publish (athena_slot_id slot);

  // Single consumer API.
  bool try_command (readable_command& command);
  bool wait_command (readable_command& command);
  bool complete (athena_slot_id slot);

  void close () noexcept;
  bool is_closed () const noexcept;
  std::size_t slot_count () const noexcept;
  const actor_command_record* record (athena_slot_id slot) const noexcept;

private:
  struct alignas(64) sequence_counter {
    std::atomic<std::uint64_t> value {0};
  };

  const std::size_t slot_count_;
  std::unique_ptr<actor_command_record[]> records_;
  std::vector<athena_slot_id> submissions_;
  std::vector<athena_slot_id> completions_;
  std::vector<athena_slot_id> producer_free_slots_;

  sequence_counter submission_write_;
  sequence_counter submission_read_;
  sequence_counter completion_write_;
  sequence_counter completion_read_;

  std::atomic<bool> closed_ {false};
  mutable std::mutex wait_lock_;
  std::condition_variable command_ready_;
  std::condition_variable command_space_;
  std::condition_variable completion_ready_;
  std::condition_variable completion_space_;

  bool reclaim_completion ();
  bool valid_slot (athena_slot_id slot) const noexcept;
};

class owned_actor_blob {
public:
  owned_actor_blob () noexcept;
  owned_actor_blob (owned_actor_blob&&) noexcept;
  owned_actor_blob& operator = (owned_actor_blob&&) noexcept;
  ~owned_actor_blob ();

  owned_actor_blob (const owned_actor_blob&)= delete;
  owned_actor_blob& operator = (const owned_actor_blob&)= delete;

  std::byte* data () noexcept;
  const std::byte* data () const noexcept;
  std::size_t size () const noexcept;
  explicit operator bool () const noexcept;

private:
  struct storage;
  std::unique_ptr<storage> storage_;

  explicit owned_actor_blob (std::unique_ptr<storage> storage) noexcept;
  friend class actor_blob_registry;
  friend class actor_blob_reservation;
};

class actor_blob_reservation {
public:
  actor_blob_reservation () noexcept;
  actor_blob_reservation (actor_blob_reservation&&) noexcept;
  actor_blob_reservation& operator = (actor_blob_reservation&&) noexcept;
  ~actor_blob_reservation ();

  actor_blob_reservation (const actor_blob_reservation&)= delete;
  actor_blob_reservation& operator = (const actor_blob_reservation&)= delete;

  athena_blob_id id () const noexcept;
  std::byte* data () noexcept;
  std::size_t size () const noexcept;
  athena_blob_id publish () noexcept;
  explicit operator bool () const noexcept;

private:
  athena_blob_id id_;
  std::byte* data_;
  std::size_t size_;

  actor_blob_reservation (athena_blob_id id, std::byte* data,
                          std::size_t size) noexcept;
  void reset () noexcept;
  friend class actor_blob_registry;
};

class actor_blob_registry {
public:
  static actor_blob_registry& instance ();

  actor_blob_reservation allocate (std::size_t size);
  owned_actor_blob take (athena_blob_id id) noexcept;
  bool discard (athena_blob_id id) noexcept;
  std::size_t outstanding () const noexcept;

private:
  actor_blob_registry ();
  ~actor_blob_registry ();
  actor_blob_registry (const actor_blob_registry&)= delete;
  actor_blob_registry& operator = (const actor_blob_registry&)= delete;

  struct implementation;
  std::unique_ptr<implementation> impl_;
};

class actor_text_reservation {
public:
  actor_text_reservation () noexcept;
  actor_text_reservation (actor_text_reservation&&) noexcept;
  actor_text_reservation& operator = (actor_text_reservation&&) noexcept;
  ~actor_text_reservation ();

  actor_text_reservation (const actor_text_reservation&)= delete;
  actor_text_reservation& operator = (const actor_text_reservation&)= delete;

  athena_blob_id id () const noexcept;
  char* data () noexcept;
  std::size_t size () const noexcept;
  athena_blob_id publish () noexcept;
  explicit operator bool () const noexcept;

private:
  athena_blob_id id_;
  char* data_;
  std::size_t size_;

  actor_text_reservation (athena_blob_id id, char* data,
                          std::size_t size) noexcept;
  void reset () noexcept;
  friend class actor_text_registry;
};

class actor_text_registry {
public:
  static actor_text_registry& instance ();

  actor_text_reservation allocate (std::size_t size);
  athena_blob_id store (string text);
  string take (athena_blob_id id) noexcept;
  bool discard (athena_blob_id id) noexcept;

private:
  actor_text_registry ();
  ~actor_text_registry ();
  actor_text_registry (const actor_text_registry&)= delete;
  actor_text_registry& operator = (const actor_text_registry&)= delete;

  struct implementation;
  std::unique_ptr<implementation> impl_;
};

// Trees use unique ownership while in transit.  Command rings carry only the
// returned id.  A receiver that needs to retain a tree past the hand-off makes
// its actor-local copy before releasing the transferred value.
class actor_tree_registry {
public:
  static actor_tree_registry& instance ();

  athena_blob_id store (tree value);
  tree take (athena_blob_id id);
  bool discard (athena_blob_id id) noexcept;

private:
  actor_tree_registry ();
  ~actor_tree_registry ();
  actor_tree_registry (const actor_tree_registry&)= delete;
  actor_tree_registry& operator = (const actor_tree_registry&)= delete;

  struct implementation;
  std::unique_ptr<implementation> impl_;
};

#endif // defined ACTOR_TRANSPORT_H
