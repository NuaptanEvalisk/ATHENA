/******************************************************************************
* MODULE     : QTMCustomStylesManager.hpp
* DESCRIPTION: Qt ADS pane for managing custom ATHENA styles
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMCUSTOMSTYLESMANAGER_HPP
#define QTMCUSTOMSTYLESMANAGER_HPP

#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QSizeGrip;

class QTMCustomStylesManager : public QWidget {
public:
  QTMCustomStylesManager (QWidget* parent = nullptr);

  QSize sizeHint () const override;
  void  setFloatingResizeGripVisible (bool visible);
  void  refresh ();

private:
  QString stylesDirectory () const;
  QString selectedStyleName () const;
  QString selectedStylePath () const;
  void    installStyle ();
  void    uninstallSelectedStyle ();
  void    activateSelectedStyle ();
  void    openStylesDirectory ();
  void    showContextMenu (const QPoint& pos);
  void    showError (const QString& message) const;
  void    showInfo (const QString& message) const;

  QListWidget* list;
  QLabel*      pathLabel;
  QLabel*      statusLabel;
  QSizeGrip*   floatingSizeGrip;
};

void custom_styles_manager_show ();

#endif // QTMCUSTOMSTYLESMANAGER_HPP
