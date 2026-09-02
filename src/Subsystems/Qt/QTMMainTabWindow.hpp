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
#include <QList>
#include <QPair>
#include <QPointer>
#include <DockManager.h>

namespace ads {
class CDockAreaWidget;
class CDockContainerWidget;
}

bool athena_qt_is_closing ();
bool athena_has_open_ads_panes ();
bool qtm_close_focused_ads_tool_pane (QWidget* eventReceiver= nullptr);
void qtm_apply_ads_tab_close_preferences ();

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
  ads::CDockManager* dockManager() { return mDockManager; }
  void showAdsDockWidget(ads::CDockWidget* dock, ads::DockWidgetArea area);
  void saveAdsLayoutState();
  void restoreAdsLayoutState();
  void restoreAdsVisiblePanes();
  void scheduleAdsLayoutRestore(
    ads::CDockWidget* revealDock= nullptr,
    ads::DockWidgetArea area= ads::NoDockWidgetArea);

  QList<QWidget*> documentWidgets() const;
  bool hasOpenAdsPanes() const;
  QWidget* currentDocumentWidget() const;
  QString documentWidgetTitle(QWidget* widget) const;
  void activateDocumentWidget(QWidget* widget);
  bool placeDocumentWidgetsSideBySide(QWidget* left, QWidget* right);

  void tileSubWindows();
  void cascadeSubWindows();
  void mdi_maximize_active();
  void mdi_minimize_active();
  void detachWidget(QWidget* widget);
  void attachWidget(QWidget* widget);
  void setNextWidgetFloating();

protected:
  void closeEvent (QCloseEvent* event) override;
  bool eventFilter(QObject * obj, QEvent * event) override;
  bool eventFilterWindow(QObject * obj, QEvent * event);
  bool eventFilterTabBar(QObject * obj, QEvent * event);

  void setDefaultStyle();
  void setHoverStyle();
  void onWindowActivated();
  void onDoubleClickOnEmptyTabBarSpace();
  void setMainTitle(QString title);
  void setMainTitleFromWidget(QWidget* widget);
  void showAfterContentReady(QWidget* focusWidget);
  bool adsLayoutPersistenceEnabled() const;
  QString adsLayoutStatePath() const;
  QString adsVisiblePanesStatePath() const;
  ads::CDockContainerWidget* activeAdsDockContainer() const;
  ads::CDockAreaWidget* activeAdsDockArea(
    ads::CDockContainerWidget* container) const;

public slots:
  void closeTab(int index);
  void onSubWindowActivated(QMdiSubWindow* sub);

private:
  static QTMMainTabWindow *gTopTabWindow;

  QStackedWidget* mStackedWidget;
  QTabWidget* mTabWidget;
  QMdiArea* mMdiArea;
  ads::CDockManager* mDockManager;
  QPointer<QWidget> mLastFocusedDocumentWidget;
  QList<QPair<QPointer<ads::CDockWidget>, ads::DockWidgetArea>>
    mAdsDocksToReveal;
  bool mAdsLayoutRestoreScheduled;
};

#endif // QTMMAINTABWINDOW_HPP
