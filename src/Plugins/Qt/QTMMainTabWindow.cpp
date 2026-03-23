#include "QTMMainTabWindow.hpp"
#include "QTMApplication.hpp"
#include "qt_window_widget.hpp"
#include "scheme.hpp"
#include "tm_server.hpp"

#include <QMouseEvent>
#include <QTabBar>
#include <QApplication>
#include <QMdiSubWindow>
#include <QCloseEvent>
#include <QToolButton>
#include <iostream>

QTMMainTabWindow *QTMMainTabWindow::gTopTabWindow = nullptr;

bool isMovingTab = false;
bool isMovingWindow = false;
int movingTabIndex = -1;
QPoint movingTabStartPos;
QTMMainTabWindow *newTabWindow = nullptr;
QTMMainTabWindow *targetTabWindow = nullptr;

QTMMainTabWindow::QTMMainTabWindow() {
  mStackedWidget = new QStackedWidget(this);
  setCentralWidget (mStackedWidget);

  mTabWidget = new QTabWidget(mStackedWidget);
  mTabWidget->setTabsClosable(true);
  mTabWidget->setMovable(true);

  mMdiArea = new QMdiArea(mStackedWidget);
  mMdiArea->setViewMode (QMdiArea::SubWindowView);

  mDockManager = new ads::CDockManager(mStackedWidget);

  mStackedWidget->addWidget (mTabWidget);
  mStackedWidget->addWidget (mMdiArea);
  mStackedWidget->addWidget (mDockManager);

  if (tmapp()->useMdi()) mStackedWidget->setCurrentWidget (mMdiArea);
  else if (tmapp()->useAds()) mStackedWidget->setCurrentWidget (mDockManager);
  else mStackedWidget->setCurrentWidget (mTabWidget);

  // todo : keep the tab window size and position in the user preferences
#ifndef OS_ANDROID
  setMinimumSize(800, 600);
#endif

  setAttribute(Qt::WA_DeleteOnClose);

  // remove the border and padding
  setDefaultStyle();

  connect(mTabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
  connect(mMdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)), this, SLOT(onSubWindowActivated(QMdiSubWindow*)));
  
  show();

#if !defined(OS_ANDROID) && QT_VERSION >= 0x060000
  QRect screenGeometry = QApplication::screens().at(0)->geometry();
  move(screenGeometry.center() - rect().center());
#endif

#if !defined(OS_ANDROID) && QT_VERSION >= 0x060000
  installEventFilter(this);
  mTabWidget->tabBar()->installEventFilter(this);
#endif

  gTopTabWindow = this;
}

QTMMainTabWindow::~QTMMainTabWindow() {
  if (gTopTabWindow == this) {
    gTopTabWindow = nullptr;
  }
}

void QTMMainTabWindow::closeEvent(QCloseEvent *event) {
  if (is_server_started()) {
    event->ignore();
    eval("(safely-quit-ATHENA)");
  } else {
    QMainWindow::closeEvent(event);
  }
}

void QTMMainTabWindow::onWindowActivated() {
  gTopTabWindow = this;
}

void QTMMainTabWindow::onDoubleClickOnEmptyTabBarSpace() {
  eval ("new-document*");
}

bool QTMMainTabWindow::eventFilterWindow(QObject *obj, QEvent *event) {
#if QT_VERSION >= 0x060000
  // if the window is a top level window
  if (event->type() == QEvent::WindowActivate) {
    if (DEBUG_QT_WIDGETS) cout << "TabWindow: WindowActivated" << LF;
    onWindowActivated();
  }

  if (event->type() == QEvent::MouseButtonPress && !tmapp()->useMdi()) {
    if (DEBUG_QT_WIDGETS) cout << "TabWindow: MouseButtonPress" << LF;
    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    int x = mouseEvent->position().toPoint().x();
    int y = mouseEvent->position().toPoint().y();
    int tabBarWidth = mTabWidget->tabBar()->width();
    int tabBarHeight = mTabWidget->tabBar()->height();
    if(x > tabBarWidth && y < tabBarHeight)
    {
      if (DEBUG_QT_WIDGETS) cout << "Mouse on an empty tab bar space" << LF;
      onDoubleClickOnEmptyTabBarSpace();
    }
  }

  return QMainWindow::eventFilter(obj, event);
#else
  (void) obj; (void) event;
  return false;
#endif
}

bool QTMMainTabWindow::eventFilterTabBar(QObject *obj, QEvent *event) {
#if QT_VERSION >= 0x060000
  if (tmapp()->useMdi()) return QMainWindow::eventFilter(obj, event);

  if (event->type() == QEvent::MouseButtonPress) {
    if (mTabWidget->count() == 1) {
      isMovingWindow = true;
      newTabWindow = this;
      movingTabIndex = 0;
      QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
      movingTabStartPos = mouseEvent->position().toPoint();
    } 
    else 
    {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
      int x = mouseEvent->position().toPoint().x();
      int y = mouseEvent->position().toPoint().y();
      int tabBarWidth = mTabWidget->tabBar()->width();
      int tabBarHeight = mTabWidget->tabBar()->height();
      if (mouseEvent->button() == Qt::LeftButton && 
          x >= 0 && y >= 0 && x < tabBarWidth && y < tabBarHeight) {
        isMovingTab = true;
        movingTabIndex = mTabWidget->tabBar()->tabAt(QPoint(x, y));
        movingTabStartPos = mouseEvent->position().toPoint();
      }
    }
  }

  if (event->type() == QEvent::MouseMove && isMovingTab) {
    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    int x = mouseEvent->position().toPoint().x();
    int y = mouseEvent->position().toPoint().y();
    int tabBarWidth = mTabWidget->tabBar()->width();
    int tabBarHeight = mTabWidget->tabBar()->height();
    const int dist = 10;
    if (x >= tabBarWidth + dist || y >= tabBarHeight + dist ||
        x < -dist || y < -dist) {
      newTabWindow = new QTMMainTabWindow();
      QWidget *widgetToMove = mTabWidget->widget(movingTabIndex);
      mTabWidget->removeTab(movingTabIndex);
      newTabWindow->showWidget(widgetToMove);
      isMovingTab = false;
      isMovingWindow = true;
      movingTabIndex = 0;
    }
  }

  if (event->type() == QEvent::MouseMove && isMovingWindow) {
    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    int globalX = mouseEvent->globalPosition().toPoint().x();
    int globalY = mouseEvent->globalPosition().toPoint().y();
    globalX -= newTabWindow->width() / 2;
    globalY -= 10;
    newTabWindow->move(globalX, globalY);
    
    QTMMainTabWindow *tabWindow = nullptr;
    targetTabWindow = nullptr;
    for (QWidget *topWidget : QApplication::topLevelWidgets()) {
      tabWindow = qobject_cast<QTMMainTabWindow *>(topWidget);
      if (tabWindow == nullptr || tabWindow->tmapp()->useMdi()) continue;

      QPoint globalPos = mouseEvent->globalPosition().toPoint();
      QPoint localPos = tabWindow->mapFromGlobal(globalPos);
      QRect tabBarRect = tabWindow->mTabWidget->tabBar()->rect();
      tabBarRect.setWidth(tabWindow->width());

      if (tabWindow && tabWindow != newTabWindow && 
          tabBarRect.contains(localPos)) {
        targetTabWindow = tabWindow;
        tabWindow->setHoverStyle();
        break;
      }
      tabWindow->setDefaultStyle();
    }
  }

  if (event->type() == QEvent::MouseButtonRelease) {
    isMovingWindow = false;
    isMovingTab = false;
    if (targetTabWindow != nullptr) {
      QWidget *widgetToMove = mTabWidget->widget(movingTabIndex);
      mTabWidget->removeTab(movingTabIndex);
      targetTabWindow->showWidget(widgetToMove);
      targetTabWindow->setDefaultStyle();
      targetTabWindow->activateWindow();
      targetTabWindow = nullptr;
      if (mTabWidget->count() == 0) {
        closeAndSetTopTabWindow();
      }
    }
  }
  return QMainWindow::eventFilter(obj, event);
#else
  (void) obj; (void) event;
  return false;
#endif
}

bool QTMMainTabWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Close) {
    std::cout << "ATHENA ADS DEBUG: QEvent::Close received on object of type: " << obj->metaObject()->className() << std::endl;
  }

  if (obj == this) {
    return eventFilterWindow(obj, event);
  }

  if (QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(obj)) {
    if (event->type() == QEvent::MouseButtonPress) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::MiddleButton) {
        // Detect if click is in the title bar area
        int titleBarHeight = sub->style()->pixelMetric(QStyle::PM_TitleBarHeight);
#if QT_VERSION >= 0x060000
        int y = mouseEvent->position().toPoint().y();
#else
        int y = mouseEvent->pos().y();
#endif
        if (y >= 0 && y < titleBarHeight) {
          if (QWidget* inner = sub->widget()) {
            if (inner->metaObject()->indexOfSignal("closed()") != -1) {
              QMetaObject::invokeMethod(inner, "closed");
              return true;
            }
          }
        }
      }
    }
  }

  return eventFilterTabBar(obj, event);
}

void QTMMainTabWindow::showWidget(QWidget *widget, bool isDocument) {
  if (tmapp()->useAds()) {
    ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(widget->parentWidget());
    if (dockWidget) {
      mStackedWidget->setCurrentWidget (mDockManager);
      dockWidget->show();
      dockWidget->raise();
      widget->setFocus();
    } else if (isDocument) {
      dockWidget = new ads::CDockWidget(widget->windowTitle());
      dockWidget->setWidget(widget);
      
      // Use CustomCloseHandling to prevent automatic deletion/hiding and let TeXmacs handle it safely.
      dockWidget->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
      dockWidget->setFeature(ads::CDockWidget::CustomCloseHandling, true);
      
      connect(dockWidget, &ads::CDockWidget::closeRequested, [widget]() {
        if (nr_windows <= 1) {
          if (is_server_started()) {
            eval("(safely-quit-ATHENA)");
          } else {
            gTopTabWindow->closeAndSetTopTabWindow();
          }
          return;
        }
        if (widget->metaObject()->indexOfSignal("closed()") != -1) {
          QMetaObject::invokeMethod(widget, "closed");
        }
      });
      
      mDockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget);
      mStackedWidget->setCurrentWidget (mDockManager);
      widget->setFocus();
    } else {

      widget->show();
      widget->raise();
      widget->activateWindow();
      widget->setFocus();
    }
  } else if (tmapp()->useMdi()) {
    QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(widget->parentWidget());
    if (sub) {
      mStackedWidget->setCurrentWidget (mMdiArea);
      sub->show();
      mMdiArea->setActiveSubWindow(sub);
      widget->setFocus();
    } else if (isDocument) {
      bool first = mMdiArea->subWindowList().isEmpty();
      sub = mMdiArea->addSubWindow (widget);
      sub->setAttribute(Qt::WA_DeleteOnClose);
      sub->installEventFilter(this); // Listen for middle clicks on title bar
      mStackedWidget->setCurrentWidget (mMdiArea);
      if (first) sub->showMaximized();
      else sub->show();
      mMdiArea->setActiveSubWindow(sub);
      widget->setFocus();
    } else {
      widget->show();
      widget->raise();
      widget->activateWindow();
      widget->setFocus();
    }
  } else {
    int index = mTabWidget->indexOf(widget);
    if (index == -1) {
      mTabWidget->addTab(widget, widget->windowTitle());
      index = mTabWidget->indexOf(widget);
    }
    mTabWidget->setCurrentIndex(index);
    mStackedWidget->setCurrentWidget (mTabWidget);
    widget->setFocus();
  }
}

void QTMMainTabWindow::removeWidget(QWidget *widget) {
  std::cout << "ATHENA ADS: removeWidget called. nr_windows = " << nr_windows << std::endl;
  if (tmapp()->useAds()) {
    if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(widget->parentWidget())) {
      mDockManager->removeDockWidget(dockWidget);
      dockWidget->deleteLater();
    }
  } else if (tmapp()->useMdi()) {
    if (QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(widget->parentWidget())) {
      sub->close();
    } else {
      mMdiArea->removeSubWindow (widget);
    }
  } else {
    mTabWidget->removeTab(mTabWidget->indexOf(widget));
  }
  
  if (nr_windows <= 1) {
    std::cout << "ATHENA ADS: triggering safely-quit-ATHENA (nr_windows <= 1)" << std::endl;
    if (is_server_started()) {
      eval("(safely-quit-ATHENA)");
    } else {
      closeAndSetTopTabWindow();
    }
  }
}

void QTMMainTabWindow::closeTab(int index) {
  QWidget *w = mTabWidget->widget(index);
  if (w) w->close();
  if (mTabWidget->count() == 0) closeAndSetTopTabWindow();
}

void QTMMainTabWindow::onSubWindowActivated(QMdiSubWindow* sub) {
  if (sub && sub->widget()) {
    sub->widget()->setFocus();
  }
}

void QTMMainTabWindow::tileSubWindows() {
  mMdiArea->tileSubWindows();
}

void QTMMainTabWindow::cascadeSubWindows() {
  QList<QMdiSubWindow *> windows = mMdiArea->subWindowList();
  int x = 0;
  int y = 0;
  int offset = 30;
  
  // Calculate a reasonable default size for cascaded windows (e.g., 80% of area)
  int w = mMdiArea->width() * 0.8;
  int h = mMdiArea->height() * 0.8;

  for (QMdiSubWindow *window : windows) {
    if (window->isMinimized()) continue;
    window->showNormal();
    window->setGeometry(x, y, w, h);
    x += offset;
    y += offset;
    
    // Wrap around if we go too far
    if (x > mMdiArea->width() / 2 || y > mMdiArea->height() / 2) {
      x = 0;
      y = 0;
    }
  }
}

void QTMMainTabWindow::mdi_maximize_active() {
  if (QMdiSubWindow* active = mMdiArea->activeSubWindow()) {
    active->showMaximized();
  }
}

void QTMMainTabWindow::mdi_minimize_active() {
  if (QMdiSubWindow* active = mMdiArea->activeSubWindow()) {
    active->showMinimized();
  }
}

void QTMMainTabWindow::detachWidget(QWidget* widget) {
  if (tmapp()->useMdi()) {
    if (QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(widget->parentWidget())) {
      sub->setWidget(nullptr);
      sub->deleteLater();
      widget->setWindowFlags(Qt::Window);
      widget->show();
    }
  } else {
    int index = mTabWidget->indexOf(widget);
    if (index != -1) {
      mTabWidget->removeTab(index);
      widget->setParent(nullptr);
      widget->setWindowFlags(Qt::Window);
      widget->show();
    }
  }
}

void QTMMainTabWindow::attachWidget(QWidget* widget) {
  if (widget->parentWidget() == nullptr) {
    showWidget(widget, true);
  }
}

void QTMMainTabWindow::tabTitleChanged(QWidget *widget, QString title) {
  if (tmapp()->useAds()) {
    QWidget* p = widget->parentWidget();
    while (p) {
      if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
        dockWidget->setWindowTitle(title);
        break;
      }
      p = p->parentWidget();
    }
  } else if (tmapp()->useMdi()) {
    widget->setWindowTitle (title);
  } else {
    int index = mTabWidget->indexOf(widget);
    if (index != -1) mTabWidget->setTabText(index, title);
  }
}

void QTMMainTabWindow::closeAndSetTopTabWindow() {
  gTopTabWindow = nullptr;
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    QTMMainTabWindow *tabWindow = qobject_cast<QTMMainTabWindow *>(widget);
    if (tabWindow && tabWindow != this) {
      gTopTabWindow = tabWindow;
      break;
    }
  }
  close();
}

void QTMMainTabWindow::setDefaultStyle() {
  QString adsStyle =
    "ads--CDockAreaTitleBar { "
    "   min-height: 38px !important; "
    "} "
    "ads--CDockWidgetTab { "
    "   min-height: 38px !important; "
    "   padding: 0 15px !important; "
    "} "
    "ads--CTitleBarButton, "
    "ads--CDockAreaWidget QToolButton, "
    "#tabsMenuButton, #dockAreaCloseButton, #detachGroupButton, "
    "#tabCloseButton, #floatingTitleCloseButton, #floatingTitleMaximizeButton { "
    "   qproperty-iconSize: 24px 24px !important; "
    "   min-width: 32px !important; "
    "   min-height: 32px !important; "
    "   width: 32px !important; "
    "   height: 32px !important; "
    "   padding: 0px !important; "
    "   margin: 0px !important; "
    "} ";
  
  this->setStyleSheet(adsStyle);

  mTabWidget->setStyleSheet(
    "QTabBar::tab { "
    "   height: 30px; "
    "   width: 150px; "
    "   border-radius: 0px; "
    "   padding: 0px; "
    "} "
    "QTabWidget::pane { "
    "   border: 0px; "
    "   padding: 0px; "
    "}"
  );
}

void QTMMainTabWindow::setHoverStyle() {
  mTabWidget->setStyleSheet(
    "QTabBar::tab { "
    "   height: 30px; "
    "   width: 150px; "
    "   border-radius: 0px; "
    "   padding: 0px; "
    "   background-color: rgba(255, 0, 0, 0.5); "
    "} "
    "QTabWidget::pane { "
    "   border: 0px; "
    "   padding: 0px; "
    "}"
  );
}
