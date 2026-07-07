/******************************************************************************
* MODULE     : QTMNeighborhoodsPane.hpp
* DESCRIPTION: Native ADS pane for ATHENA document neighborhoods
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMNEIGHBORHOODSPANE_HPP
#define QTMNEIGHBORHOODSPANE_HPP

#include "neighborhoods.hpp"

#include <QSize>
#include <QWidget>

class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

class QTMNeighborhoodsPane : public QWidget {
public:
  QTMNeighborhoodsPane (QWidget* parent = nullptr);
  QSize sizeHint () const override;

  void refreshFromCurrentBuffer (bool force = false);

private:
  void buildUi ();
  void renderSet (const athena_neighborhood_set& set);
  void activateItem (QTableWidgetItem* item);
  void selectionChanged ();

  QTableWidget* table;
  QLabel*       message;
  QTimer*       timer;
  bool          refreshing;
  QString       lastCurrentPath;
  url           targetBuffer;
};

void neighborhoods_pane_show ();
void neighborhoods_pane_refresh ();
bool neighborhoods_open_neighbor (int direction);
bool neighborhoods_cycle_selected ();

#endif // QTMNEIGHBORHOODSPANE_HPP
