/******************************************************************************
* MODULE     : QTMATHENADiff.cpp
* DESCRIPTION: Side-by-side structured comparison of ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMATHENADiff.hpp"
#include "QTMNodePropertiesDialog.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/convert.hpp"

#include "ATHENA/Data/athena_diff.hpp"
#include "QTMMainTabWindow.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "qt_utilities.hpp"
#include "qt_widget.hpp"
#include "tm_window.hpp"
#include "tm_buffer.hpp"
#include "buffer_actor.hpp"
#include "scheme_execution_context.hpp"
#include "file.hpp"
#include <vector>

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QPushButton>

namespace {

url
athenaDiffUrl (const QString& fileName) {
  return url_system (from_qstring (QDir::cleanPath (fileName)));
}

bool
ensureAthenaDiffBuffer (url name, QWidget* parent) {
  if (!is_nil (concrete_buffer (name))) return true;

  if (buffer_import (name, name, "texmacs")) {
    QMessageBox::warning (
      parent, "Compare two files",
      "ATHENA could not load:\n" + to_qstring (as_string (name)));
    return false;
  }
  return true;
}

QWidget*
athenaDiffDocumentWidget (url windowName) {
  tm_window window= concrete_window (windowName);
  if (window == nullptr || is_nil (window->wid)) return nullptr;
  return concrete (window->wid)->qwid.data ();
}

athena::document::native_xml_read_result snapshotDocument (url name) {
  using namespace athena::document;
  auto buffer= concrete_buffer (name);
  actor_command_record snapshot;
  if (is_nil (buffer) || !buffer->actor->invoke (actor_command_kind::snapshot_document,
      ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &snapshot))
    throw std::runtime_error ("Cannot snapshot comparison document");
  tree document= actor_tree_registry::instance ().take (snapshot.payload0);
  switch (snapshot.argument[0]) {
  case 1: return {xml_format_version::v1, document};
  case 2: return {xml_format_version::v2, document};
  case 3: return {xml_format_version::v3, document};
  default: throw std::runtime_error ("Unknown comparison storage version");
  }
}

void setDiffHighlights (url view_name, string key, range_set ranges) {
  auto* view= concrete_view (view_name);
  auto buffer= concrete_buffer (view_to_buffer (view_name));
  if (!view || is_nil (buffer)) throw std::runtime_error ("Comparison view has closed");
  const auto continuation= actor_continuation_registry::instance ().store (
    [key, ranges] {
      const auto* context= current_scheme_execution_context ();
      if (!context || !context->editor) throw std::runtime_error ("Comparison editor has closed");
      range_set absolute;
      for (int i=0; i<N(ranges); ++i)
        absolute << context->editor->the_buffer_path () * ranges[i];
      context->editor->set_alt_selection (key, absolute);
    });
  if (!buffer->actor->invoke (actor_command_kind::run_native_continuation,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation);
    throw std::runtime_error ("Cannot highlight comparison document");
  }
}

void showMetadataDifferences (tree left, tree right,
  athena::document::xml_format_version left_version,
  athena::document::xml_format_version right_version,
  const AthenaTreeDiff& diff, QWidget* host) {
  if (diff.metadata.empty ()) return;
  auto* dialog= new QDialog (host);
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setWindowTitle (QObject::tr ("Revision property differences")); dialog->resize (650, 380);
  auto* layout= new QVBoxLayout (dialog);
  auto* entries= new QTreeWidget (dialog);
  entries->setHeaderLabels ({QObject::tr ("Left node"), QObject::tr ("Right node")});
  entries->header ()->setSectionResizeMode (QHeaderView::ResizeToContents);
  layout->addWidget (entries);
  auto describe= [] (path p) {
    QString text= "/";
    for (int i= 0; i < N(p); ++i) { if (i) text+= '/'; text+= QString::number (p[i]); }
    return text;
  };
  for (const auto& positions: diff.metadata)
    new QTreeWidgetItem (entries, {describe (positions.first), describe (positions.second)});
  auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, dialog);
  auto* inspect= buttons->addButton (QObject::tr ("Inspect both nodes"), QDialogButtonBox::ActionRole);
  inspect->setEnabled (false); layout->addWidget (buttons);
  QObject::connect (entries, &QTreeWidget::itemSelectionChanged, dialog, [entries, inspect] {
    inspect->setEnabled (entries->currentItem () != nullptr);
  });
  auto positions= diff.metadata;
  QObject::connect (inspect, &QPushButton::clicked, dialog,
    [entries, dialog, left, right, left_version, right_version, positions] {
    const int index= entries->indexOfTopLevelItem (entries->currentItem ());
    if (index < 0) return;
    auto open= [dialog] (tree t, path p, athena::document::xml_format_version version,
                        const QString& title) {
      for (int i= 0; i < N(p); ++i) t= t[p[i]];
      const bool canonical= athena::enunciation::is_canonical (t) &&
        athena::document_node::validate_node_properties (t).empty ();
      auto* inspector= make_node_properties_dialog (encode_node_property_snapshot (t, version),
        canonical, false, {}, dialog);
      inspector->setAttribute (Qt::WA_DeleteOnClose); inspector->setWindowTitle (title); inspector->show ();
    };
    try {
      open (left, positions[index].first, left_version, QObject::tr ("Left revision properties"));
      open (right, positions[index].second, right_version, QObject::tr ("Right revision properties"));
    }
    catch (const std::exception& error) {
      QMessageBox::warning (dialog, QObject::tr ("Revision properties"), QString::fromUtf8 (error.what ()));
    }
  });
  QObject::connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  dialog->show ();
}

} // namespace

void
athena_diff_show_snapshots (athena::document::native_xml_read_result left_snapshot,
  athena::document::native_xml_read_result right_snapshot, url left_source,
  url right_source, string left_title, string right_title) {
  using namespace athena::document;
  const tree left= left_snapshot.document, right= right_snapshot.document;
  // Validate both immutable inputs before opening either comparison buffer.
  encode_native_xml_bytes (left, left_snapshot.version);
  encode_native_xml_bytes (right, right_snapshot.version);
  auto* host= QTMMainTabWindow::topTabWindow ();
  if (host == nullptr) throw std::runtime_error ("No ATHENA window for revision comparison");
  const auto diff= athena_diff_trees (left, right);
  auto open= [&] (tree source, xml_format_version version, url master,
                 string title, const range_set& ranges) {
    int body_index= -1;
    for (int i= 0; i < N(source); ++i)
      if (is_compound (source[i], "body", 1)) body_index= i;
    if (body_index < 0) throw std::invalid_argument ("Revision has no document body");
    std::vector<std::vector<int>> positions;
    for (int i= 0; i+1 < N(ranges); i+= 2) {
      path bounds[2]= {ranges[i], ranges[i+1]};
      if (N(bounds[0]) < 2 || N(bounds[1]) < 2 || bounds[0][0] != body_index ||
          bounds[1][0] != body_index || bounds[0][1] != 0 || bounds[1][1] != 0) continue;
      for (int j= 0; j < 2; ++j) {
        std::vector<int> position;
        for (int k= 2; k < N(bounds[j]); ++k) position.push_back (bounds[j][k]);
        positions.push_back (std::move (position));
      }
    }
    url name;
    for (int i= 1; ; ++i) {
      name= url_scratch ("hodarium_revision_", ".ath", i);
      if (is_nil (concrete_buffer (name))) break;
    }
    if (!initialize_buffer_tree (name, source, version))
      throw std::runtime_error ("Cannot initialize revision storage version");
    set_master_buffer (name, master);
    auto window= new_buffer_in_new_window (name, source);
    auto* buffer= concrete_buffer (name);
    auto* view= concrete_view (window_to_view (window));
    if (!buffer || !view) throw std::runtime_error ("Cannot create Hodarium revision view");
    buffer->buf->read_only= true;
    publish_buffer_realtime_save_paused (buffer, true);
    buffer->actor->invoke (actor_command_kind::set_buffer_read_only, view->runtime_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 1);
    buffer->actor->invoke (actor_command_kind::set_realtime_save_paused, view->runtime_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 1);
    const auto continuation= actor_continuation_registry::instance ().store (
      [positions= std::move (positions), id= view->runtime_id] {
        const auto* context= current_scheme_execution_context ();
        if (!context || !context->actor) return;
        auto* editor= context->actor->current_editor (id);
        if (!editor) return;
        range_set highlights;
        for (const auto& coordinates: positions) {
          path position;
          for (auto it= coordinates.rbegin (); it != coordinates.rend (); ++it) position= path (*it, position);
          highlights << editor->the_buffer_path () * position;
        }
        editor->set_alt_selection ("hodarium-diff", highlights);
      });
    if (!buffer_actor::try_submit_to (buffer->actor->id (), actor_command_kind::run_native_continuation,
        view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation)) {
      actor_continuation_registry::instance ().discard (continuation);
      throw std::runtime_error ("Cannot queue Hodarium comparison highlights");
    }
    set_title_buffer (name, title);
    return window;
  };
  auto left_window= open (left, left_snapshot.version, left_source, left_title, diff.left);
  auto right_window= open (right, right_snapshot.version, right_source, right_title, diff.right);
  auto* left_widget= athenaDiffDocumentWidget (left_window);
  auto* right_widget= athenaDiffDocumentWidget (right_window);
  if (!left_widget || !right_widget || !host->placeDocumentWidgetsSideBySide (left_widget, right_widget))
    throw std::runtime_error ("Cannot arrange Hodarium comparison views");
  host->activateDocumentWidget (left_widget);
  showMetadataDifferences (left, right, left_snapshot.version, right_snapshot.version, diff, host);
  if (diff.hunks == 0) QMessageBox::information (host, QObject::tr ("Compare revisions"),
    QObject::tr ("The revisions have identical document trees."));
}

void athena_diff_show_snapshots (const std::string& left, const std::string& right,
  url left_source, url right_source, string left_title, string right_title) {
  using namespace athena::document;
  auto left_snapshot= decode_native_xml_bytes (left);
  auto right_snapshot= decode_native_xml_bytes (right);
  athena_diff_show_snapshots (std::move (left_snapshot), std::move (right_snapshot),
    left_source, right_source, left_title, right_title);
}

void athena_diff_show_snapshots (tree left, tree right, url left_source,
  url right_source, string left_title, string right_title) {
  using namespace athena::document;
  athena_diff_show_snapshots (native_xml_read_result {xml_format_version::v2, left},
    native_xml_read_result {xml_format_version::v2, right}, left_source, right_source,
    left_title, right_title);
}

void
athena_diff_show () {
  if (qt_defer_to_main_thread (athena_diff_show)) return;

  QWidget* parent= QApplication::activeWindow ();
  QTMMainTabWindow* host= QTMMainTabWindow::topTabWindow ();
  if (host == nullptr) {
    QMessageBox::warning (parent, "Compare two files",
                          "No active ATHENA window.");
    return;
  }

  QFileDialog dialog (parent, "Compare two ATHENA files", QDir::homePath (),
                      "ATHENA documents (*.ath)");
  dialog.setFileMode (QFileDialog::ExistingFiles);
  dialog.setAcceptMode (QFileDialog::AcceptOpen);
  if (dialog.exec () != QDialog::Accepted) return;

  QStringList files= dialog.selectedFiles ();
  if (files.size () != 2) {
    QMessageBox::warning (parent, "Compare two files",
                          "Select exactly two ATHENA documents.");
    return;
  }

  QFileInfo leftInfo (files[0]);
  QFileInfo rightInfo (files[1]);
  QString leftPath= leftInfo.canonicalFilePath ();
  QString rightPath= rightInfo.canonicalFilePath ();
  if (leftPath.isEmpty ()) leftPath= leftInfo.absoluteFilePath ();
  if (rightPath.isEmpty ()) rightPath= rightInfo.absoluteFilePath ();
  if (leftPath == rightPath) {
    QMessageBox::warning (parent, "Compare two files",
                          "Select two different ATHENA documents.");
    return;
  }

  url leftName= athenaDiffUrl (leftPath);
  url rightName= athenaDiffUrl (rightPath);
  if (!ensureAthenaDiffBuffer (leftName, parent) ||
      !ensureAthenaDiffBuffer (rightName, parent))
    return;

  try {
    const auto left= snapshotDocument (leftName), right= snapshotDocument (rightName);
    tree leftBody= extract (left.document, "body");
    tree rightBody= extract (right.document, "body");
    AthenaTreeDiff diff= athena_diff_trees (leftBody, rightBody);

    url leftWindow= new_buffer_in_new_window (leftName, left.document);
    url rightWindow= new_buffer_in_new_window (rightName, right.document);
    url leftView= window_to_view (leftWindow);
    url rightView= window_to_view (rightWindow);
    setDiffHighlights (leftView, "athena-diff-left", diff.left);
    setDiffHighlights (rightView, "athena-diff-right", diff.right);

    QWidget* leftWidget= athenaDiffDocumentWidget (leftWindow);
    QWidget* rightWidget= athenaDiffDocumentWidget (rightWindow);
    if (leftWidget == nullptr || rightWidget == nullptr ||
        !host->placeDocumentWidgetsSideBySide (leftWidget, rightWidget)) {
      QMessageBox::warning (parent, "Compare two files",
                            "ATHENA could not arrange the comparison views "
                            "side by side.");
      return;
    }

    set_current_view (leftView);
    host->activateDocumentWidget (leftWidget);
    showMetadataDifferences (leftBody, rightBody, left.version, right.version, diff, host);
    if (diff.hunks == 0)
      QMessageBox::information (host, "Compare two files",
                                "The two ATHENA documents are structurally "
                                "identical.");
  }
  catch (const std::exception& error) {
    QMessageBox::warning (parent, "Compare two files", QString::fromUtf8 (error.what ()));
  }
}
