// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "document_print.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "file.hpp"
#include "ATHENA/Data/data_art.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "Qt/qt_utilities.hpp"
#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>
#include <stdexcept>

namespace {
std::string bytes (string value) { return {value.data (), std::size_t (N(value))}; }
string text (const std::string& value) { return string (value.data (), value.size ()); }
void report_error (std::string error) {
  if (error.empty ()) return;
  qt_post_to_main_thread ([error= std::move (error)] {
    QMessageBox::warning (QApplication::activeWindow (), QObject::tr ("Print document"),
      QString::fromUtf8 (error.data (), error.size ()));
  });
}
struct export_wait {
  bool active;
  explicit export_wait (bool enabled): active (enabled) {
    if (active) system_wait ("Exporting PDF", "please wait");
  }
  ~export_wait () { if (active) system_wait ("", ""); }
};
struct temporary_cover {
  url name;
  ~temporary_cover () { if (exists (name)) remove (name); }
};
}

void document_print_with_cover (url target) {
  export_wait wait (athena_data_art_pdf_target (object (target)));
  url source= get_current_buffer_safe ();
  object prepared= athena_data_art_prepare_export (source, object (target));
  if (is_bool (prepared)) {
    get_current_editor ()->print_to_file (target);
    return;
  }
  tree payload= as_tree (prepared);
  temporary_cover cover {url (as_string (payload[2]))};
  string error= athena_data_art_export_snapshot (
    source, target, copy (payload[0]), copy (payload[1]), as_int (payload[3]));
  if (error != "") throw std::runtime_error (bytes (error));
}

void document_preview_file (url target) {
  auto name= bytes (concretize (target));
  auto command= bytes (get_preference ("preview command", "default"));
  qt_post_to_main_thread ([name= std::move (name), command= std::move (command)] {
    if (command != "default") {
      system (text (command) * " " * escape_sh (text (name)) * " &");
      return;
    }
    if (!QDesktopServices::openUrl (QUrl::fromLocalFile (
          QString::fromUtf8 (name.data (), name.size ()))))
      report_error ("Could not open the document in the default viewer");
  });
}

void document_preview () {
  athena_node_reference_with_native_export ([] (editor_rep&) {
    url target= url_temp (".pdf");
    document_print_with_cover (target);
    document_preview_file (target);
  }, report_error);
}

void document_prepared_print (url target) {
  auto name= bytes (as_string (target));
  athena_node_reference_with_native_export (
    [name= std::move (name)] (editor_rep& editor) {
      editor.print_to_file (url (text (name)));
    }, report_error);
}

void document_prepared_print_pages (string first, string last) {
  auto start= bytes (first), end= bytes (last);
  athena_node_reference_with_native_export (
    [start= std::move (start), end= std::move (end)] (editor_rep& editor) {
      editor.print_buffer (text (start), text (end));
    }, report_error);
}
