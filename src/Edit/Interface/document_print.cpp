// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "document_print.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "file.hpp"
#include "buffer_actor.hpp"
#include "guile_tm.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "ATHENA/Data/data_art.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "Qt/qt_utilities.hpp"
#include "Qt/QTMNativeDialogs.hpp"
#include "boot.hpp"
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
#include <QSpinBox>
#include <QUrl>
#include <algorithm>
#include <limits>
#include <memory>
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

namespace {
struct print_dialog_request {
  athena_actor_id actor;
  athena_view_id view;
  int pages;
  std::string name;
};

void submit_print (const print_dialog_request& request, QString filename,
                   int first, int last) {
  auto target= filename.toUtf8 ().toStdString ();
  athena::avd::submit_source_task (request.actor, request.view,
    [target= std::move (target), first, last] (editor_rep&) {
      athena_node_reference_with_native_export (
        [target, first, last] (editor_rep& editor) {
          if (target.empty ()) editor.print_buffer (as_string (first), as_string (last));
          else editor.print_to_file (url_system (text (target)),
                                     as_string (first), as_string (last));
        }, report_error);
    }, report_error);
}

void choose_print_file (print_dialog_request request, int first, int last) {
  auto* dialog= new QFileDialog (QApplication::activeWindow (),
    QObject::tr ("Print to PDF"));
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setAcceptMode (QFileDialog::AcceptSave);
  dialog->setNameFilter (QObject::tr ("PDF documents (*.pdf)"));
  dialog->setDefaultSuffix ("pdf");
  QString name= QString::fromUtf8 (request.name.data (), request.name.size ());
  dialog->selectFile (QFileInfo (name).completeBaseName () + ".pdf");
  QObject::connect (dialog, &QFileDialog::fileSelected, dialog,
    [request, first, last] (const QString& filename) {
      submit_print (request, filename, first, last);
    });
  dialog->open ();
}

void print_dialog (bool select_pages, bool to_file) {
  const auto* owner= current_scheme_execution_context ();
  if (!owner || !owner->actor || !owner->editor)
    throw std::runtime_error ("Printing requires an owning editor");
  print_dialog_request request {owner->actor_id, owner->view_id,
    std::max (1, owner->editor->get_page_count ()),
    bytes (basename (owner->actor->current_buffer_url ()))};
  qt_post_to_main_thread ([request, select_pages, to_file] {
    if (!select_pages) { choose_print_file (request, 1, 1000000); return; }
    auto* dialog= new QDialog (QApplication::activeWindow ());
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (QObject::tr ("Print page selection"));
    auto* layout= new QFormLayout (dialog);
    auto* first= new QSpinBox (dialog);
    auto* last= new QSpinBox (dialog);
    first->setRange (1, std::numeric_limits<int>::max ());
    last->setRange (1, std::numeric_limits<int>::max ());
    last->setValue (request.pages);
    QObject::connect (first, &QSpinBox::valueChanged, last, &QSpinBox::setMinimum);
    QObject::connect (last, &QSpinBox::valueChanged, first, &QSpinBox::setMaximum);
    layout->addRow (QObject::tr ("First page:"), first);
    layout->addRow (QObject::tr ("Last page:"), last);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow (buttons);
    QObject::connect (buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect (dialog, &QDialog::accepted, dialog, [request, to_file, first, last] {
      if (to_file) choose_print_file (request, first->value (), last->value ());
      else submit_print (request, QString (), first->value (), last->value ());
    });
    dialog->open ();
  });
}
}

void document_print_file_dialog () { print_dialog (false, true); }
void document_print_pages_dialog () { print_dialog (true, false); }
void document_print_pages_file_dialog () { print_dialog (true, true); }

void document_print_direct () {
  athena_node_reference_with_native_export ([] (editor_rep& editor) {
    editor.print_buffer ();
  }, report_error);
}

void document_print_interactive () {
  if (headless_mode)
    throw std::runtime_error ("Interactive printing requires a graphical session");
  athena_node_reference_with_native_export ([] (editor_rep& editor) {
    url target= url_temp (".pdf");
    auto file= std::shared_ptr<std::string> (new std::string (bytes (concretize (target))),
      [] (std::string* name) {
        QFile::remove (QString::fromUtf8 (name->data (), name->size ()));
        delete name;
      });
    editor.print_to_file (target);
    qt_post_to_main_thread ([file] {
      qtm_print_file_dialog (url_system (text (*file)));
    });
  }, report_error);
}

void document_print () {
  if (!headless_mode && get_preference ("gui:print dialogue") == "on")
    document_print_interactive ();
  else document_print_direct ();
}
