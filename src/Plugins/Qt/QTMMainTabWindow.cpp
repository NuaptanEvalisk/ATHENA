#include "QTMMainTabWindow.hpp"
#include "QTMApplication.hpp"
#include "scheme.hpp"

#include <QMouseEvent>
#include <QTabBar>
#include <QApplication>
#include <QMdiSubWindow>

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

  mStackedWidget->addWidget (mTabWidget);
  mStackedWidget->addWidget (mMdiArea);

  if (tmapp()->useMdi()) mStackedWidget->setCurrentWidget (mMdiArea);
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
  if (obj == this) {
    return eventFilterWindow(obj, event);
  }

  return eventFilterTabBar(obj, event);
}

void QTMMainTabWindow::showWidget(QWidget *widget, bool isDocument) {
  if (tmapp()->useMdi()) {
    bool first = mMdiArea->subWindowList().isEmpty();
    QMdiSubWindow* sub = mMdiArea->addSubWindow (widget);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    if (isDocument && (first || mMdiArea->activeSubWindow())) {
      sub->showMaximized();
    } else {
      sub->show();
    }
  } else {
    mTabWidget->addTab(widget, widget->windowTitle());
    mTabWidget->setCurrentWidget(widget);
  }
}

void QTMMainTabWindow::removeWidget(QWidget *widget) {
  if (tmapp()->useMdi()) {
    mMdiArea->removeSubWindow (widget);
  } else {
    mTabWidget->removeTab(mTabWidget->indexOf(widget));
  }
  
  bool empty = tmapp()->useMdi() ? mMdiArea->subWindowList().isEmpty() : (mTabWidget->count() == 0);
  if (empty) closeAndSetTopTabWindow();
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
      mMdiArea->removeSubWindow(widget);
      widget->setParent(nullptr);
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
  if (tmapp()->useMdi()) {
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
