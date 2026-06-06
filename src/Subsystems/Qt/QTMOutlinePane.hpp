/******************************************************************************
* MODULE     : QTMOutlinePane.hpp
* DESCRIPTION: Live document outline pane
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMOUTLINEPANE_HPP
#define QTMOUTLINEPANE_HPP

#include "path.hpp"
#include <QSize>
#include <QVector>
#include <QWidget>

class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class QTMOutlinePane : public QWidget {
public:
  struct Entry {
    int     level;
    QString title;
    int     words;
    path    treePath;
  };

  QTMOutlinePane (QWidget* parent = nullptr);
  QSize sizeHint () const override;

private:
  void refresh ();
  void activateItem (QTreeWidgetItem* item);

  QTreeWidget* tree;
  QTimer*      timer;
  QVector<Entry> entries;
  QString      lastSignature;
};

void outline_pane_show ();

#endif // QTMOUTLINEPANE_HPP
