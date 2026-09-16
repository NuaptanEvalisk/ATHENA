/******************************************************************************
* MODULE     : vault_bugcheck.cpp
* DESCRIPTION: Native asynchronous vault document bugcheck
*******************************************************************************/

#include "ATHENA/Data/vault_bugcheck.hpp"

#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "QTMNativeDialogs.hpp"
#include "basic.hpp"
#include "buffer_actor.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"

#include <QCoreApplication>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs= std::filesystem;

namespace {

constexpr int bugcheck_load_attempts= 120;
constexpr int bugcheck_settle_ms= 2000;
std::atomic<std::uint64_t> bugcheck_generation {0};

std::string
std_string (string value) {
  return std::string (value.data (), N(value));
}

std::string
vault_root_path () {
  string value= as_string (concretize (vault_get_root ()), URL_SYSTEM);
  return std_string (value);
}

std::vector<std::string>
ath_files_recursive (const fs::path& root) {
  std::vector<std::string> files;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment (ec)) {
    const fs::directory_entry& entry= *it;
    std::string name= entry.path ().filename ().string ();
    if (entry.is_directory (ec)) {
      if (!name.empty () && name[0] == '.') it.disable_recursion_pending ();
      continue;
    }
    if (entry.is_regular_file (ec) && entry.path ().extension () == ".ath")
      files.push_back (entry.path ().string ());
  }
  std::sort (files.begin (), files.end ());
  return files;
}

struct VaultBugcheckRun: std::enable_shared_from_this<VaultBugcheckRun> {
  std::uint64_t generation= 0;
  athena_actor_id actor_id= ATHENA_NO_ACTOR;
  athena_view_id view_id= ATHENA_NO_VIEW;
  SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_NONE;
  std::string root;
  std::vector<std::string> files;
  std::vector<std::string> errors;
  size_t index= 0;
  int attempts_left= bugcheck_load_attempts;

  bool current () const {
    return generation == bugcheck_generation.load (std::memory_order_acquire);
  }

  std::string relative (const std::string& file) const {
    std::error_code ec;
    fs::path rel= fs::relative (fs::path (file), fs::path (root), ec);
    return ec ? file : rel.generic_string ();
  }

  void add_error (const std::string& file, const std::string& kind,
                  const std::string& detail) {
    errors.push_back (relative (file) + "\n  " + kind + ": " + detail);
  }

  void set_progress_message (const std::string& text) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->editor != nullptr)
      context->editor->set_message (
        tree (string (text.c_str ())), tree ("Vault Bugcheck"));
  }

  void submit_actor (std::function<void()> action) {
    if (!current ()) return;
    athena_continuation_id id=
      actor_continuation_registry::instance ().store (std::move (action));
    actor_command_ticket ticket= buffer_actor::submit_to (
      actor_id, actor_command_kind::run_native_continuation, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, capabilities, id);
    if (!ticket)
      (void) actor_continuation_registry::instance ().discard (id);
  }

  void schedule_main (int milliseconds, std::function<void()> action) {
    if (!current ()) return;
    qt_post_to_main_thread (
      [self= shared_from_this (), milliseconds,
       action= std::move (action)] () mutable {
        QTimer::singleShot (milliseconds, [self, action= std::move (action)] () mutable {
          if (self->current ()) action ();
        });
      });
  }

  void start_next_actor () {
    if (!current ()) return;
    if (index >= files.size ()) {
      finish_actor ();
      return;
    }
    const std::string file= files[index];
    const std::string rel= relative (file);
    std::string message= "Checking " + std::to_string (index + 1) + "/" +
                         std::to_string (files.size ()) + ": " + rel;
    std_warning << "Vault bugcheck: " << string (message.c_str ()) << LF;
    set_progress_message (message);
    clear_debug_messages ();
    try {
      (void) call ("load-buffer", object (url_system (string (file.c_str ()))));
    }
    catch (...) {
      add_error (file, "load error", "exception while loading document");
      ++index;
      submit_actor ([self= shared_from_this ()] { self->start_next_actor (); });
      return;
    }
    attempts_left= bugcheck_load_attempts;
    schedule_check_main ();
  }

  void schedule_check_main () {
    schedule_main (100, [self= shared_from_this ()] { self->check_loaded_main (); });
  }

  void check_loaded_main () {
    if (!current () || index >= files.size ()) return;
    url expected= url_system (string (files[index].c_str ()));
    if (get_current_buffer () == expected) {
      schedule_main (bugcheck_settle_ms, [self= shared_from_this ()] {
        self->submit_actor ([self] { self->inspect_actor (); });
      });
      return;
    }
    if (--attempts_left <= 0) {
      std::string file= files[index];
      submit_actor ([self= shared_from_this (), file] {
        self->add_error (
          file, "load error", "Timed out waiting for file to become the current buffer");
        ++self->index;
        self->start_next_actor ();
      });
      return;
    }
    schedule_check_main ();
  }

  void inspect_actor () {
    if (!current () || index >= files.size ()) return;
    const std::string file= files[index];
    url target= url_system (string (file.c_str ()));
    try {
      (void) qt_call_in_buffer (target, "update-forced");
    }
    catch (...) {
      add_error (file, "typeset error", "exception while forcing typeset");
    }

    tree messages= get_debug_messages ("Error messages", 1000);
    for (int i=0; i<N(messages); ++i) {
      tree message= messages[i];
      if (!is_func (message, TUPLE, 3) ||
          !is_atomic (message[0]) || !is_atomic (message[1]))
        continue;
      add_error (
        file, "TeXmacs diagnostic",
        std_string (message[0]->label * ": " * message[1]->label));
    }
    ++index;
    start_next_actor ();
  }

  std::string summary () const {
    std::string result= "Vault bugcheck completed.\n" +
      std::to_string (files.size ()) + " .ath files checked.\n";
    if (errors.empty ()) return result + "No errors found.";
    result += std::to_string (errors.size ()) + " error(s) found:\n\n";
    for (size_t i=0; i<errors.size (); ++i) {
      if (i != 0) result += "\n\n";
      result += std::to_string (i + 1) + ". " + errors[i];
    }
    return result;
  }

  void finish_actor () {
    if (!current ()) return;
    std::string report= summary ();
    std_warning << string (report.c_str ()) << LF;
    (void) save_string (
      url_system (string ((fs::path (root) / "VaultBugcheck.log").string ().c_str ())),
      string (report.c_str ()), false);
    qt_post_to_main_thread ([report] {
      qtm_text_report_dialog ("Vault Bugcheck", string (report.c_str ()));
    });
  }
};

} // namespace

void
vault_bugcheck_native () {
  if (!vault_active ()) {
    qt_post_to_main_thread ([] {
      qtm_info_dialog (
        "No active vault. Please load a vault first.", "Vault Bugcheck");
    });
    return;
  }
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr || context->actor_id == ATHENA_NO_ACTOR ||
      context->view_id == ATHENA_NO_VIEW) {
    qt_post_to_main_thread ([] {
      qtm_info_dialog ("Vault bugcheck requires an active document view.",
                       "Vault Bugcheck");
    });
    return;
  }

  auto run= std::make_shared<VaultBugcheckRun> ();
  run->generation= bugcheck_generation.fetch_add (1, std::memory_order_acq_rel) + 1;
  run->actor_id= context->actor_id;
  run->view_id= context->view_id;
  run->capabilities= context->capabilities;
  run->root= vault_root_path ();
  run->files= ath_files_recursive (fs::path (run->root));
  if (run->files.empty ()) {
    qt_post_to_main_thread ([] {
      qtm_info_dialog ("No .ath files found in the current vault.", "Vault Bugcheck");
    });
    return;
  }
  clear_debug_messages ();
  std_warning << "Vault bugcheck starting with " << (int) run->files.size ()
              << " files" << LF;
  run->start_next_actor ();
}
