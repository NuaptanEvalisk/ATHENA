// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "codex_completion.hpp"
#include "editor.hpp"
#include "scheme.hpp"
#include "native_interfaces.hpp"
#include "object.hpp"
#include "scheme_native_context.hpp"
#include "ATHENA/Data/vault.hpp"
#include "Data/Convert/Generic/conversion_executor.hpp"
#include "analyze.hpp"
#include "file.hpp"
#include "boot.hpp"
#include "tree_traverse.hpp"
#include "guile_tm.hpp"
#include "scheme_execution_context.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "buffer_name_catalog.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"
#include "QTMCodexCompletion.hpp"
#include "qt_utilities.hpp"
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace {
struct selection_figures {
  editor_rep& owner;
  string uuid= vault_generate_uuid ();
  array<url> files;
  std::vector<std::pair<string, string>> markers;
  bool transferred= false;

  static void cleanup (void* raw) noexcept {
    auto& self= *static_cast<selection_figures*> (raw);
    if (!self.transferred)
      for (int i= 0; i < N(self.files); ++i) remove (self.files[i]);
    self.files= array<url> ();
    std::vector<std::pair<string, string>> ().swap (self.markers);
    self.uuid= "";
  }

  explicit selection_figures (editor ed): owner (*ed.operator-> ()) {
    scheme_begin_native_scope (&cleanup, this);
  }
  ~selection_figures () { scheme_end_native_scope (); }

  tree visit (tree node) {
    if (is_atomic (node)) return copy (node);
    if (is_func (node, IMAGE) || is_func (node, GRAPHICS) ||
        is_compound (node, "commutative-diagram")) {
      const string number= as_string (markers.size () + 1);
      const string filename= "codex-fig-" * uuid * "-" * number * ".png";
      const url file= url_temp_dir () * filename;
      files << file;
      try {
        owner.print_snippet (file, node, true);
        if (exists (file)) {
          const string marker= "ATHENACODEXFIG" * replace (uuid, "-", "") * "N" * number * "PNG";
          markers.emplace_back (marker, "<" * filename * ">");
          return marker;
        }
      }
      catch (...) { std_warning << "Codex figure rendering failed" << LF; }
      remove (file);
      files= range (files, 0, N(files)-1);
    }
    tree result (L(node), N(node));
    for (int i= 0; i < N(node); ++i) result[i]= visit (node[i]);
    return result;
  }
};
}

static tree native_codex_prepare_selection (tree selection) {
  selection_figures figures (get_current_editor ());
  const tree source= figures.visit (selection);
  const object options= list_object (cons (object ("texmacs->latex:encoding"), object ("utf-8")));
  string latex= as_string (conversion_dispatch (object (source), "texmacs-tree", "latex-snippet", options));
  for (const auto& marker: figures.markers) latex= replace (latex, marker.first, marker.second);
  string prompt= "Continue the following ATHENA document selection. It was exported as "
    "a LaTeX snippet. Return only the LaTeX snippet that should be inserted "
    "immediately after it: no Markdown fence, explanation, or repetition of "
    "the supplied text. Preserve its language, mathematical notation, style, "
    "and level of formality.";
  if (N(figures.files) != 0)
    prompt << " Figure placeholders of the form <codex-fig-UUID-N.png> refer "
      "to the attached images with matching labels. Use their visual "
      "content when continuing the document.";
  prompt << "\n\n" << latex;
  tree images (TUPLE);
  for (int i= 0; i < N(figures.files); ++i)
    images << as_string (figures.files[i], URL_SYSTEM);
  tree result (TUPLE, prompt, images);
  figures.transferred= true;
  return result;
}

namespace {
std::string text (string value) { return std::string (value.data (), N(value)); }
string native_text (const std::string& value) { return string (value.data (), value.size ()); }

struct completion_request: std::enable_shared_from_this<completion_request> {
  std::string bridge, home, input, output, model, effort, tier, search;
  std::vector<std::string> images;
  athena_actor_id actor= 0;
  athena_view_id view= 0;
  athena_scheme_handle_id placeholder= ATHENA_NO_SCHEME_HANDLE;

  ~completion_request () {
    scheme_command_handle_release (placeholder);
    std::error_code ignored;
    if (!input.empty ()) std::filesystem::remove (input, ignored);
    if (!output.empty ()) std::filesystem::remove (output, ignored);
    for (const auto& image: images) std::filesystem::remove (image, ignored);
  }

  void attach (tree node) {
    const auto* owner= current_scheme_execution_context ();
    actor= owner->actor_id;
    view= owner->view_id;
    placeholder= scheme_command_handle_acquire (object_to_tmscm (object (node)));
  }

  void finish (editor_rep& ed) {
    tree node= as_tree (tmscm_to_object (scheme_command_handle_value (placeholder)));
    const path ip= obtain_ip (node);
    if (!ip_attached (ip) || !is_compound (node, "athena-codex-thinking", 0)) return;
    tree replacement;
    string failure;
    string answer;
    if (load_string (url_system (native_text (output)), answer, false))
      failure= "Codex completion failed";
    else if (answer == "") failure= "Codex returned no completion";
    else {
      try {
        replacement= as_tree (conversion_dispatch (
          object (answer), "latex-snippet", "texmacs-tree", null_object ()));
      }
      catch (const std::exception& error) {
        failure= "Codex completion could not be imported: " * string (error.what ());
      }
    }
    if (failure != "") {
      replacement= tree (WITH, "color", "red", failure);
      ed.set_message (failure, "AI completion");
    }
    const path location= reverse (ip);
    struct positions {
      editor_rep& ed;
      std::vector<observer> values;
      ~positions () { for (auto value: values) ed.position_delete (value); }
      observer keep (path p) { observer o= ed.position_new (p); values.push_back (o); return o; }
    } tracked {ed, {}};
    const bool restore= !(location <= ed.the_path ());
    observer saved;
    if (restore) saved= tracked.keep (ed.the_path ());
    ed.go_to (ed.command_tree_start (ed.the_root (), location));
    observer insertion= tracked.keep (ed.the_path ());
    tree_assign (node, "");
    ed.go_to (ed.position_get (insertion));
    ed.insert_tree (replacement);
    if (restore) ed.go_to (ed.position_get (saved));
  }

  void completed () {
    auto self= shared_from_this ();
    athena::avd::submit_source_task (actor, view,
      [self] (editor_rep& ed) { self->finish (ed); },
      [self] (std::string error) {
        if (!error.empty ()) std_warning << "Codex completion: " << native_text (error) << LF;
      });
  }

  void launch () {
    struct callback: command_rep {
      std::shared_ptr<completion_request> request;
      explicit callback (std::shared_ptr<completion_request> r): request (std::move (r)) {}
      void apply () override { request->completed (); }
    };
    array<string> paths;
    for (const auto& image: images) paths << native_text (image);
    qtm_codex_run_completion_async (native_text (bridge), native_text (home),
      native_text (input), native_text (output), native_text (model), native_text (effort),
      native_text (tier), native_text (search), paths,
      command (tm_new<callback> (shared_from_this ())));
  }
};

void start_completion (bool temporary, bool custom) {
  editor ed= get_current_editor ();
  if (!ed->selection_active_any ()) {
    ed->set_message ("Select text to continue with Codex", "AI completion");
    return;
  }
  url executable ("$ATHENA_PATH/bin/athena-codex-bridge");
  if (!exists (executable)) executable= resolve_in_path (url ("athena-codex-bridge"));
  const string bridge= concretize (executable);
  if (bridge == "") {
    ed->set_message ("Codex bridge is not installed", "AI completion");
    return;
  }
  string home= get_preference ("codex home", "");
  if (home == "") home= as_string (url ("$ATHENA_HOME_PATH/codex"), URL_SYSTEM);
  if (!headless_mode) qtm_codex_initialize_models (bridge, home);
  auto request= std::make_shared<completion_request> ();
  request->bridge= text (bridge);
  request->home= text (home);
  if (custom) {
    if (headless_mode) return;
    const array<string> options= qtm_codex_completion_options (bridge, home);
    if (N(options) != 6) return;
    request->model= text (options[0]);
    request->effort= text (options[1]);
    request->tier= text (options[2]);
    request->search= options[3] == "on" ? "live" : "disabled";
    temporary= options[4] == "temporary-buffer";
  }
  tree prepared= native_codex_prepare_selection (ed->selection_get ());
  for (int i= 0; i < N(prepared[1]); ++i) request->images.push_back (text (prepared[1][i]->label));
  request->input= text (as_string (url_temp (".codex-prompt"), URL_SYSTEM));
  request->output= text (as_string (url_temp (".codex-output"), URL_SYSTEM));
  if (save_string (url_system (native_text (request->input)), prepared[0]->label, false)) {
    ed->set_message ("Could not write Codex completion prompt", "AI completion");
    return;
  }
  if (!temporary) {
    ed->go_to (ed->selection_get_end ());
    ed->selection_cancel ();
    ed->make_return_after ();
    tree node= ed->the_subtree (path_up (ed->the_path ()));
    node= tree_assign (node, compound ("athena-codex-thinking"));
    request->attach (node);
    ed->go_to (ed->command_tree_end (ed->the_root (), reverse (obtain_ip (node))));
    request->launch ();
    return;
  }
  ed->selection_cancel ();
  qt_post_to_main_thread ([request] {
    get_server ()->ads_prepare_floating ();
    const url window= open_window ();
    const url name= window_to_buffer (window);
    set_title_buffer (name, "AI completion");
    switch_to_window (window);
    const auto owner= published_buffer_source (text (as_string (name)));
    athena::avd::submit_source_task (owner.first, owner.second,
      [request] (editor_rep& destination) {
        tree body= destination.the_buffer ();
        body= tree_assign (body, tree (DOCUMENT, compound ("athena-codex-thinking")));
        request->attach (body[0]);
        destination.go_to (destination.command_tree_end (destination.the_root (),
                           reverse (obtain_ip (body[0]))));
        request->launch ();
      }, [request] (std::string error) {
        if (!error.empty ()) std_warning << "Codex completion target: " << native_text (error) << LF;
      });
  });
}
}

void native_codex_completion () { start_completion (false, false); }
void native_codex_completion_new_buffer () { start_completion (true, false); }
void native_codex_completion_custom () { start_completion (false, true); }
