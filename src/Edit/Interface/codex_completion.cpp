// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "codex_completion.hpp"
#include "editor.hpp"
#include "scheme.hpp"
#include "scheme_native_context.hpp"
#include "ATHENA/Data/vault.hpp"
#include "Data/Convert/Generic/conversion_executor.hpp"
#include "analyze.hpp"
#include "file.hpp"
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

tree native_codex_prepare_selection (tree selection) {
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
