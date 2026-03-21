/******************************************************************************
* MODULE     : QTMMainTabWindow.hpp
* DESCRIPTION: A tab window that handle multiple moving tabs into windows.
* COPYRIGHT  : (C) 2025 Liza Belos
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMMAINTABWINDOW_HPP
#define QTMMAINTABWINDOW_HPP

#include "config.h"

#include <QMainWindow>
#include <QTabWidget>
#include <QMdiArea>
#include <QStackedWidget>

/**
 * @brief A multi-document window that supports both Tabs and MDI.
 */
class QTMMainTabWindow : public QMainWindow {
  Q_OBJECT

public:
  QTMMainTabWindow();
  ~QTMMainTabWindow();

  void showWidget(QWidget *widget, bool isDocument = false);
  void removeWidget(QWidget *widget);
  void tabTitleChanged(QWidget *widget, QString title);
  void closeAndSetTopTabWindow();

  static QTMMainTabWindow *topTabWindow() { 
    return gTopTabWindow; 
  }

  QTabWidget* tabWidget() { return mTabWidget; }
  QMdiArea* mdiArea() { return mMdiArea; }

  void tileSubWindows();
  void cascadeSubWindows();
  void mdi_maximize_active();
  void mdi_minimize_active();

protected:
  bool eventFilter(QObject * obj, QEvent * event) override;
  bool eventFilterWindow(QObject * obj, QEvent * event);
  bool eventFilterTabBar(QObject * obj, QEvent * event);

  void setDefaultStyle();
  void setHoverStyle();
  void onWindowActivated();
  void onDoubleClickOnEmptyTabBarSpace();

public slots:
  void closeTab(int index);
  void onSubWindowActivated(QMdiSubWindow* sub);

private:
  static QTMMainTabWindow *gTopTabWindow;
  QStackedWidget* mStackedWidget;
  QTabWidget* mTabWidget;
  QMdiArea* mMdiArea;
};

#endif // QTMMAINTABWINDOW_HPP
