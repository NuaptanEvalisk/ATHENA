/******************************************************************************
* MODULE     : QTMErrorMessagesPane.hpp
* DESCRIPTION: Qt ADS pane for error and warning messages
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMERRORMESSAGESPANE_HPP
#define QTMERRORMESSAGESPANE_HPP

#include <QSize>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSizeGrip;
class QTimer;
class QTreeWidget;

class QTMErrorMessagesPane : public QWidget {
public:
  QTMErrorMessagesPane (QWidget* parent = nullptr);

  QSize sizeHint () const override;
  void refresh ();
  void setFloatingResizeGripVisible (bool visible);

private:
  void rebuildCategories ();
  void clearMessages ();
  QString selectedCategory () const;
  int messageLimit () const;

  QComboBox*   categoryBox;
  QComboBox*   limitBox;
  QCheckBox*   detailsCheck;
  QPushButton* refreshButton;
  QPushButton* clearButton;
  QSizeGrip*   floatingSizeGrip;
  QLabel*      statusLabel;
  QTreeWidget* messageTree;
  QTimer*      refreshTimer;
};

void error_messages_show ();

#endif // QTMERRORMESSAGESPANE_HPP
