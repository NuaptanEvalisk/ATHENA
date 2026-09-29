/******************************************************************************
* MODULE     : QTMDocumentHistoryPane.hpp
* DESCRIPTION: Qt document history pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMDOCUMENTHISTORYPANE_HPP
#define QTMDOCUMENTHISTORYPANE_HPP

#include <QSize>
#include <QString>
#include <QWidget>
#include "url.hpp"

#include <cstdint>

class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

class QTMDocumentHistoryPane : public QWidget {
public:
  QTMDocumentHistoryPane (QWidget* parent = nullptr);

  void setDocument (url document, bool followCurrent= false);
  QSize sizeHint () const override;

private:
  void refresh ();
  std::int64_t selectedVersionId () const;
  void openSelectedVersion ();
  void restoreSelectedVersion ();
  void showContextMenu (const QPoint& pos);
  void followCurrentDocument ();

  QTreeWidget* tree;
  url          document;
  bool         followCurrent;
};

void document_history_pane_show ();
void document_history_pane_show_document (url document);

#endif // QTMDOCUMENTHISTORYPANE_HPP
