// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "document_print.hpp"
#include "presentation_commands.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "tm_window.hpp"
#include "tm_buffer.hpp"
#include "buffer_state.hpp"
#include "file.hpp"
#include "buffer_actor.hpp"
#include "guile_tm.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "Data/Convert/Generic/conversion_registry.hpp"
#include "Pdf/pdf_hummus_make_attachment.hpp"
#include "Pdf/pdf_hummus_extract_attachment.hpp"
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
#include <QTemporaryDir>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <limits>
#include <memory>
#include <map>
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
  export_wait wait (athena_data_art_pdf_target (target));
  url source= get_current_buffer_safe ();
  auto prepared= athena_data_art_prepare_export (source, target);
  if (!prepared) {
    get_current_editor ()->print_to_file (target);
    return;
  }
  tree payload= *prepared;
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
                   int first, int last, bool export_pdf) {
  auto target= filename.toUtf8 ().toStdString ();
  athena::avd::submit_source_task (request.actor, request.view,
    [target= std::move (target), first, last, export_pdf] (editor_rep&) {
      if (export_pdf) { document_export_pdf (url_system (text (target))); return; }
      athena_node_reference_with_native_export (
        [target, first, last] (editor_rep& editor) {
          if (target.empty ()) editor.print_buffer (as_string (first), as_string (last));
          else editor.print_to_file (url_system (text (target)),
                                     as_string (first), as_string (last));
        }, report_error);
    }, report_error);
}

void choose_print_file (print_dialog_request request, int first, int last,
                       bool export_pdf= false) {
  auto* dialog= new QFileDialog (QApplication::activeWindow (),
    export_pdf ? QObject::tr ("Export PDF") : QObject::tr ("Print to PDF"));
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setAcceptMode (QFileDialog::AcceptSave);
  dialog->setNameFilter (QObject::tr ("PDF documents (*.pdf)"));
  dialog->setDefaultSuffix ("pdf");
  QString name= QString::fromUtf8 (request.name.data (), request.name.size ());
  dialog->selectFile (QFileInfo (name).completeBaseName () + ".pdf");
  QObject::connect (dialog, &QFileDialog::fileSelected, dialog,
    [request, first, last, export_pdf] (const QString& filename) {
      submit_print (request, filename, first, last, export_pdf);
    });
  dialog->open ();
}

void print_dialog (bool select_pages, bool to_file, bool export_pdf= false) {
  const auto* owner= current_scheme_execution_context ();
  if (!owner || !owner->actor || !owner->editor)
    throw std::runtime_error ("Printing requires an owning editor");
  print_dialog_request request {owner->actor_id, owner->view_id,
    std::max (1, owner->editor->get_page_count ()),
    bytes (as_string (tail (owner->actor->current_buffer_url ())))};
  qt_post_to_main_thread ([request, select_pages, to_file, export_pdf] {
    if (!select_pages) { choose_print_file (request, 1, 1000000, export_pdf); return; }
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
      else submit_print (request, QString (), first->value (), last->value (), false);
    });
    dialog->open ();
  });
}
}

void document_print_file_dialog () { print_dialog (false, true); }
void document_print_pages_dialog () { print_dialog (true, false); }
void document_print_pages_file_dialog () { print_dialog (true, true); }
void document_export_pdf_dialog () { print_dialog (false, true, true); }

namespace {
void export_pdf_ready (url target, bool attach_source) {
  document_print_with_cover (target);
  if (attach_source && !document_attach_source (target))
    throw std::runtime_error ("Could not attach native document source to PDF");
}

struct export_tree_payload {
  athena_blob_id id;
  explicit export_tree_payload (tree document):
    id (actor_tree_registry::instance ().store (std::move (document))) {}
  ~export_tree_payload () { actor_tree_registry::instance ().discard (id); }
};

void export_pdf (url target, bool attach_source) {
  if (suffix (target) != "pdf")
    throw std::runtime_error ("PDF export requires a .pdf destination");
  auto destination= bytes (as_string (target));
  athena_node_reference_with_native_export (
    [destination, attach_source] (editor_rep&) {
      const auto* owner= current_scheme_execution_context ();
      tree body= get_buffer_body (owner->actor->current_buffer_url ());
      if (!is_document (body) || N(body) == 0 ||
          !is_compound (body[N(body)-1], "screens")) {
        export_pdf_ready (url (text (destination)), attach_source);
        return;
      }
      auto payload= std::make_shared<export_tree_payload> (
        copy (owner->actor->current_source (owner->view_id)));
      auto source= bytes (as_string (owner->actor->current_buffer_url ()));
      auto version= owner->actor->current_state ()->storage_version;
      auto references= athena::node_reference::current_export_references ();
      qt_post_to_main_thread ([payload, source, destination, version, references, attach_source] {
        url transient= url_none ();
        try {
          transient= make_new_buffer ();
          url view= get_new_view (transient);
          if (!replace_buffer_tree (transient,
                actor_tree_registry::instance ().take (payload->id), version))
            throw std::runtime_error ("Slide export snapshot replacement rejected");
          set_master_buffer (transient, url (text (source)));
          auto name= bytes (as_string (transient));
          athena::avd::submit_source_task (
            concrete_buffer (transient)->actor->id (), concrete_view (view)->runtime_id,
            [destination, references, attach_source] (editor_rep&) {
              athena::node_reference::export_reference_scope frozen (references);
              presentation_make_slides ();
              export_pdf_ready (url (text (destination)), attach_source);
            },
            [name] (std::string error) {
              qt_post_to_main_thread ([name, error= std::move (error)] {
                kill_buffer (url (text (name)));
                system_wait ("", "");
                report_error (error);
              });
            });
        }
        catch (const std::exception& error) {
          if (!is_none (transient)) kill_buffer (transient);
          report_error (error.what ());
        }
      });
    }, report_error);
}
}

void document_export_pdf (url target) { export_pdf (target, false); }
void document_export_pdf_with_source (url target) { export_pdf (target, true); }

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

bool document_attach_source (url target) {
  const auto* owner= current_scheme_execution_context ();
  if (!owner || !owner->actor || !owner->editor)
    throw std::runtime_error ("Attaching document source requires an owning editor");
  tree document= copy (owner->actor->current_source (owner->view_id));
  array<url> linked= get_linked_file_paths (document, owner->actor->current_buffer_url ());
  QTemporaryDir temporary;
  if (!temporary.isValid ())
    throw std::runtime_error ("Could not create PDF attachment workspace");
  string source_name= basename (target) * ".ath";
  url source= url_system (text (temporary.filePath (
    QString::fromUtf8 (source_name.data (), N(source_name))).toUtf8 ().toStdString ()));
  url attached= url_system (text (temporary.filePath ("attached.pdf").toUtf8 ().toStdString ()));
  array<url> attachments;
  attachments << source;
  std::map<std::string, std::string> names {{bytes (source_name), bytes (as_string (source))}};
  for (int i=0; i<N(linked); ++i) {
    auto name= bytes (as_string (tail (linked[i])));
    auto path= bytes (concretize (linked[i]));
    auto inserted= names.emplace (name, path);
    if (!inserted.second && inserted.first->second != path)
      throw std::runtime_error ("PDF attachments have conflicting filenames: " + name);
    if (inserted.second) attachments << linked[i];
  }
  document= replace_with_relative_path (document, url ());
  if (save_string (source, conversion_write_document (document), false))
    throw std::runtime_error ("Could not write native PDF source attachment");
  if (!pdf_hummus_make_attachments (target, attachments, attached)) return false;

  QFile input (temporary.filePath ("attached.pdf"));
  string destination= concretize (target);
  QSaveFile output (QString::fromUtf8 (destination.data (), N(destination)));
  if (!input.open (QIODevice::ReadOnly) || !output.open (QIODevice::WriteOnly))
    throw std::runtime_error ("Could not replace PDF with attached source");
  while (!input.atEnd ()) {
    QByteArray data= input.read (1024 * 1024);
    if (input.error () != QFileDevice::NoError || output.write (data) != data.size ())
      throw std::runtime_error ("Could not copy PDF with attached source");
  }
  if (!output.commit ())
    throw std::runtime_error ("Could not commit PDF with attached source");
  return true;
}

void document_import_pdf_source (url source) {
  auto name= bytes (as_string (source));
  auto open_source= [name= std::move (name)] {
    url pdf= url (text (name));
    string template_path= concretize (url_temp_dir ()) * "/pdf-source-XXXXXX";
    QTemporaryDir directory (QString::fromUtf8 (template_path.data (), N(template_path)));
    if (!directory.isValid ())
      throw std::runtime_error ("Could not create PDF source workspace");
    url root= url_system (text (directory.path ().toUtf8 ().toStdString ()));
    list<url> attachments;
    if (!extract_attachments_from_pdf (pdf, root, attachments))
      throw std::runtime_error ("Could not extract PDF source attachments");
    url main= url_none ();
    url expected= root * (basename (pdf) * ".ath");
    int candidates= 0;
    for (list<url> item= attachments; !is_nil (item); item= item->next) {
      if (suffix (item->item) != "ath") continue;
      ++candidates;
      main= item->item;
      if (main == expected) break;
    }
    if (is_none (main))
      throw std::runtime_error ("PDF has no native .ath source attachment");
    if (main != expected && candidates != 1)
      throw std::runtime_error ("PDF contains multiple possible source documents");
    if (buffer_load (main))
      throw std::runtime_error ("Could not load PDF source attachment");
    directory.setAutoRemove (false);
    switch_to_buffer (main);
  };
  if (current_scheme_execution_context ())
    qt_post_to_main_thread ([open_source= std::move (open_source)] {
      try { open_source (); }
      catch (const std::exception& error) { report_error (error.what ()); }
    });
  else open_source ();
}
