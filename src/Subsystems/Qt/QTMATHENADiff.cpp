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
