/******************************************************************************
* MODULE     : QTMATHENADiff.cpp
* DESCRIPTION: Side-by-side structured comparison of ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMATHENADiff.hpp"

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

namespace {

url
athenaDiffUrl (const QString& fileName) {
  return url_system (from_qstring (QDir::cleanPath (fileName)));
}

bool
ensureAthenaDiffBuffer (url name, QWidget* parent) {
  if (!is_nil (concrete_buffer (name))) return true;

  tree document= import_tree (name, "texmacs");
  if (document == "error") {
    QMessageBox::warning (
      parent, "Compare two files",
      "ATHENA could not load:\n" + to_qstring (as_string (name)));
    return false;
  }
  set_buffer_tree (name, document);
  return true;
}

QWidget*
athenaDiffDocumentWidget (url windowName) {
  tm_window window= concrete_window (windowName);
  if (window == nullptr || is_nil (window->wid)) return nullptr;
  return concrete (window->wid)->qwid.data ();
}

range_set
athenaDiffEditorRanges (editor target, const range_set& relativeRanges) {
  range_set result;
  path root= target->the_buffer_path ();
  for (int i=0; i<N(relativeRanges); ++i)
    result << root * relativeRanges[i];
  return result;
}

} // namespace

void
athena_diff_show_snapshots (tree left, tree right, url left_source,
  url right_source, string left_title, string right_title) {
  auto* host= QTMMainTabWindow::topTabWindow ();
  if (host == nullptr) throw std::runtime_error ("No ATHENA window for revision comparison");
  const auto diff= athena_diff_trees (left, right);
  auto open= [&] (tree source, url master, string title, const range_set& ranges) {
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
    url name= url_scratch ("hodarium_revision_", ".ath");
    set_buffer_tree (name, source);
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
  auto left_window= open (left, left_source, left_title, diff.left);
  auto right_window= open (right, right_source, right_title, diff.right);
  auto* left_widget= athenaDiffDocumentWidget (left_window);
  auto* right_widget= athenaDiffDocumentWidget (right_window);
  if (!left_widget || !right_widget || !host->placeDocumentWidgetsSideBySide (left_widget, right_widget))
    throw std::runtime_error ("Cannot arrange Hodarium comparison views");
  host->activateDocumentWidget (left_widget);
  if (diff.hunks == 0) QMessageBox::information (host, QObject::tr ("Compare revisions"),
    QObject::tr ("The revisions have identical document trees."));
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

  tree leftBody= get_buffer_body (leftName);
  tree rightBody= get_buffer_body (rightName);
  AthenaTreeDiff diff= athena_diff_trees (leftBody, rightBody);

  url leftWindow= new_buffer_in_new_window (leftName,
                                             get_buffer_tree (leftName));
  url rightWindow= new_buffer_in_new_window (rightName,
                                              get_buffer_tree (rightName));
  url leftView= window_to_view (leftWindow);
  url rightView= window_to_view (rightWindow);
  editor leftEditor= view_to_editor (leftView);
  editor rightEditor= view_to_editor (rightView);
  if (is_nil (leftEditor) || is_nil (rightEditor)) {
    QMessageBox::warning (parent, "Compare two files",
                          "ATHENA could not create the comparison views.");
    return;
  }

  leftEditor->set_alt_selection (
    "athena-diff-left", athenaDiffEditorRanges (leftEditor, diff.left));
  rightEditor->set_alt_selection (
    "athena-diff-right", athenaDiffEditorRanges (rightEditor, diff.right));

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
  if (diff.hunks == 0)
    QMessageBox::information (host, "Compare two files",
                              "The two ATHENA documents are structurally "
                              "identical.");
}
