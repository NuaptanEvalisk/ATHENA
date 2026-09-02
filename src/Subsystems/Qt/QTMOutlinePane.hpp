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

#include "actor_transport.hpp"
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
    QVector<int> treePath;
  };

  QTMOutlinePane (QWidget* parent = nullptr);
  QSize sizeHint () const override;
  void applySnapshot (athena_view_id viewId, owned_actor_blob snapshot,
                      std::uint64_t signature);

private:
  void refresh ();
  void activateItem (QTreeWidgetItem* item);

  QTreeWidget* tree;
  QTimer*      timer;
  QVector<Entry> entries;
  athena_actor_id lastActorId;
  athena_view_id lastViewId;
  std::uint64_t lastSignature;
  bool          hasSignature;
};

void outline_pane_show ();
void outline_pane_accept_snapshot (athena_view_id viewId,
                                   athena_blob_id payload,
                                   std::uint64_t signature);

#endif // QTMOUTLINEPANE_HPP
