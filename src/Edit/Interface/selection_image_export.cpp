/******************************************************************************
* Native owner-bound selection image export. GNU GPL version 3 or later.
******************************************************************************/
#include "selection_image_export.hpp"
#include "editor.hpp"
#include "buffer_actor.hpp"
#include "guile_tm.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "Data/Convert/Generic/conversion_registry.hpp"
#include "Data/Convert/Generic/conversion_graph.hpp"
#include "Pdf/pdf_hummus_make_attachment.hpp"
#include "node_metadata.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "tree_traverse.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "Qt/qt_utilities.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QMessageBox>
#include <stdexcept>

namespace {
std::string bytes (string s) { return {s.data (), std::size_t (N(s))}; }
string text (const std::string& s) { return string (s.data (), s.size ()); }
struct temporary_file {
  url name;
  explicit temporary_file (string extension): name (url_temp (extension)) {}
  ~temporary_file () { remove (name); }
};
void report_error (std::string error) {
  if (error.empty ()) return;
  qt_post_to_main_thread ([error= std::move (error)] {
    QMessageBox::warning (QApplication::activeWindow (), QObject::tr ("Export selection"),
      QString::fromUtf8 (error.data (), error.size ()));
  });
}

tree math_fragment (editor_rep& editor) {
  tree fragment= copy (editor.selection_get ());
  if (is_compound (fragment, "math") || is_func (fragment, MATH_BLOCK) ||
      is_func (fragment, MATH_GRID)) return fragment;
  path start= editor.selection_get_start ();
  const auto context= editor.cursor_context_at (start);
  if (!context)
    throw std::runtime_error ("Selection context is awaiting layout");
  if (context->value ("mode") != "math") return fragment;
  if (context->value ("math-display") != "true")
    return compound ("math", fragment);
  tree block (MATH_BLOCK, fragment);
  athena::node::metadata metadata;
  metadata.properties["math:block-numbered"]= athena::node::property (false);
  athena::node::set (block, metadata);
  return block;
}

void attach_source (url output, tree fragment) {
  const auto* owner= current_scheme_execution_context ();
  tree document= copy (owner->actor->current_source (owner->view_id));
  for (int i=0; i<N(document); ++i) {
    if (is_compound (document[i], "body", 1))
      document[i][0]= is_document (fragment) ? fragment : tree (DOCUMENT, fragment);
    if (is_compound (document[i], "initial", 1)) {
      tree settings (COLLECTION);
      for (int j=0; j<N(document[i][0]); ++j) {
        tree entry= document[i][0][j];
        if (is_func (entry, ASSOCIATE, 2) &&
            (entry[0] == "global-title" || entry[0] == "global-author" ||
             entry[0] == "global-subject" || entry[0] == "save-aux")) continue;
        settings << entry;
      }
      settings << tree (ASSOCIATE, "save-aux", "false");
      document[i][0]= settings;
    }
  }
  temporary_file source (".ath"), attached (".pdf");
  if (save_string (source.name, conversion_write_document (document), false))
    throw std::runtime_error ("Could not write selection source attachment");
  array<url> attachments; attachments << source.name;
  if (!pdf_hummus_make_attachments (output, attachments, attached.name))
    throw std::runtime_error ("Could not attach selection source to PDF");
  copy (attached.name, output);
}

url export_ready (editor_rep& editor, url destination) {
  if (!editor.selection_active_any ())
    throw std::runtime_error ("No selection to export");
  string extension= suffix (destination);
  if (extension == "") {
    extension= conversion_format_default_suffix (get_preference ("texmacs->image:format", "svg"));
    destination= glue (destination, "." * extension);
  }
  string format= conversion_format_from_suffix (extension);
  bool bitmap= format == "png" || format == "jpeg" || format == "tif";
  if (format != "pdf" && !bitmap &&
      !conversion_graph_path ("pdf-file", bytes (format * "-file")))
    throw std::runtime_error ("No PDF to " + bytes (format) + " conversion is available");

  // Trim only the detached selection, leaving the user's selection untouched.
  tree fragment= math_fragment (editor);
  if (is_document (fragment))
    while (N(fragment) > 1 && is_empty (fragment[N(fragment)-1])) {
      tree trimmed= fragment (0, N(fragment)-1);
      athena::node::copy_metadata (fragment, trimmed);
      fragment= trimmed;
    }
  SI max_width= editor.as_length ("1par");
  string width= editor.add_lengths ("0cm", as_string (max_width) * "tmpt");
  temporary_file measure (".png");
  array<SI> extents= editor.print_snippet (measure.name, fragment, true);
  tree grid= is_func (fragment, MATH_BLOCK, 1) ? fragment[0] : fragment;
  bool math= is_compound (fragment, "math") || is_func (fragment, MATH_BLOCK) ||
             is_func (fragment, MATH_GRID);
  bool single_line= !is_document (fragment) && extents[2]-extents[0] < max_width &&
                   (!is_func (grid, MATH_GRID) || N(grid) == 1);
  tree formatted;
  if (single_line) {
    tree settings (TFORMAT);
    settings << tree (TWITH, "table-width", width)
             << tree (TWITH, "table-hmode", "min")
             << tree (TWITH, "table-valign", "B")
             << tree (CWITH, "1", "1", "1", "1", "cell-background", "#ffffff00");
    for (const char* side: {"cell-lsep", "cell-rsep", "cell-tsep", "cell-bsep"})
      settings << tree (CWITH, "1", "1", "1", "1", side, "0spc");
    settings << tree (TABLE, tree (ROW, tree (CELL, fragment)));
    formatted= compound ("tabular", settings);
  }
  else {
    formatted= tree (WITH);
    formatted << "fill-color" << "#ffffff00" << "doc-at-width" << width
              << "doc-at-hmode" << (math ? "min" : "exact")
              << "doc-at-padding" << "0spc"
              << compound ("document-at", is_document (fragment) ? fragment : tree (DOCUMENT, fragment),
                           tree (_POINT, "0par", "0"));
  }
  editor.print_snippet (destination, formatted, true);
  if (!exists (destination)) throw std::runtime_error ("Selection rendering produced no image");
  if (format == "pdf") attach_source (destination, fragment);
  return destination;
}
} // namespace

void export_selection_as_graphics (url destination) {
  auto target= bytes (as_string (destination));
  athena_node_reference_with_native_export (
    [target= std::move (target)] (editor_rep& editor) {
      export_ready (editor, url (text (target)));
    }, report_error);
}

void clipboard_copy_image () {
  auto format= bytes (get_preference ("texmacs->image:format", "svg"));
  athena_node_reference_with_native_export (
    [format= std::move (format)] (editor_rep& editor) {
      temporary_file output ("." * conversion_format_default_suffix (text (format)));
      url result= export_ready (editor, output.name);
      string content;
      if (load_string (result, content, false))
        throw std::runtime_error ("Could not read exported clipboard image");
      QByteArray data (content.data (), N(content));
      qt_post_to_main_thread ([data= std::move (data), format] {
        auto* clipboard= QApplication::clipboard ();
        if (format == "svg" || format == "pdf") {
          auto* mime= new QMimeData;
          mime->setData (format == "svg" ? "image/svg+xml" : "application/pdf", data);
          clipboard->setMimeData (mime);
        }
        else clipboard->setImage (QImage::fromData (data));
      });
    }, report_error);
}

void native_export_selection_as_image_dialog (string format) {
  const auto* owner= current_scheme_execution_context ();
  if (!owner || !owner->actor || !owner->editor)
    throw std::runtime_error ("Selection export requires an owning editor");
  const auto actor= owner->actor_id, view= owner->view_id;
  auto extension= bytes (conversion_format_default_suffix (format));
  qt_post_to_main_thread ([actor, view, extension= std::move (extension)] {
    QString suffix= QString::fromUtf8 (extension.data (), extension.size ());
    auto name= QFileDialog::getSaveFileName (QApplication::activeWindow (),
      QObject::tr ("Export selection as image"), QString (), "*." + suffix);
    if (name.isEmpty ()) return;
    if (QFileInfo (name).suffix ().isEmpty ()) name += "." + suffix;
    auto target= name.toUtf8 ().toStdString ();
    athena::avd::submit_source_task (actor, view,
      [target= std::move (target)] (editor_rep&) {
        export_selection_as_graphics (url_system (text (target)));
      }, report_error);
  });
}
