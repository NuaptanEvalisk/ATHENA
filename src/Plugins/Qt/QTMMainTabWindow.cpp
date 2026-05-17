#include "QTMMainTabWindow.hpp"
#include "QTMApplication.hpp"
#include "QTMBufferSwitcher.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMWidget.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMVaultBackupViewer.hpp"
#include "QTMVaultExplorer.hpp"
#include "qt_window_widget.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tm_server.hpp"
#include "vault.hpp"

#include <QMouseEvent>
#include <QTabBar>
#include <QApplication>
#include <QMdiSubWindow>
#include <QCloseEvent>
#include <QToolButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QTimer>
#include <QStringList>
#include <iostream>

QTMMainTabWindow *QTMMainTabWindow::gTopTabWindow = nullptr;
static bool gNextWidgetFloating = false;
static int gAdsDocumentDockCounter = 0;
static const int ATHENA_ADS_LAYOUT_VERSION = 1;

static bool
isPersistentAdsPane (const QString& name) {
  return name == "athena-outline-pane" ||
         name == "athena-vault-explorer" ||
         name == "athena-global-search" ||
         name == "athena-vault-backup-viewer";
}

bool isMovingTab = false;
bool isMovingWindow = false;
int movingTabIndex = -1;
QPoint movingTabStartPos;
QTMMainTabWindow *newTabWindow = nullptr;
QTMMainTabWindow *targetTabWindow = nullptr;

static bool widgetOrChildHasFocus(QWidget* widget) {
  QWidget* focus = QApplication::focusWidget();
  return widget && (focus == widget || widget->isAncestorOf(focus));
}

static bool
isDocumentWidget(QWidget* widget) {
  return widget != nullptr &&
         (qobject_cast<QTMWidget*> (widget) != nullptr ||
          widget->findChild<QTMWidget*> () != nullptr);
}

QTMMainTabWindow::QTMMainTabWindow() {
  mStackedWidget = new QStackedWidget(this);
  setCentralWidget (mStackedWidget);
  setWindowTitle ("ATHENA");

  mTabWidget = new QTabWidget(mStackedWidget);
  mTabWidget->setTabsClosable(true);
  mTabWidget->setMovable(true);

  mMdiArea = new QMdiArea(mStackedWidget);
  mMdiArea->setViewMode (QMdiArea::SubWindowView);

  mDockManager = new ads::CDockManager(mStackedWidget);
  connect(mDockManager, &ads::CDockManager::focusedDockWidgetChanged,
          this, [this](ads::CDockWidget*, ads::CDockWidget* now) {
            if (now) setMainTitle(now->windowTitle());
            else setMainTitle("");
          });
  connect(qApp, &QCoreApplication::aboutToQuit,
          this, &QTMMainTabWindow::saveAdsLayoutState);

  mStackedWidget->addWidget (mTabWidget);
  mStackedWidget->addWidget (mMdiArea);
  mStackedWidget->addWidget (mDockManager);

  if (tmapp()->useMdi()) mStackedWidget->setCurrentWidget (mMdiArea);
  else if (tmapp()->useAds()) mStackedWidget->setCurrentWidget (mDockManager);
  else mStackedWidget->setCurrentWidget (mTabWidget);

  // todo : keep the tab window size and position in the user preferences
  setMinimumSize(800, 600);

  setAttribute(Qt::WA_DeleteOnClose);

  // remove the border and padding
  setDefaultStyle();

  connect(mTabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
  connect(mMdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)), this, SLOT(onSubWindowActivated(QMdiSubWindow*)));
  
  show();

#if QT_VERSION >= 0x060000
  QRect screenGeometry = QApplication::screens().at(0)->geometry();
  move(screenGeometry.center() - rect().center());
#endif

#if QT_VERSION >= 0x060000
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
  saveAdsLayoutState();
  if (is_server_started()) {
    event->ignore();
    eval("(safely-quit-ATHENA)");
  } else {
    QMainWindow::closeEvent(event);
  }
}

void QTMMainTabWindow::onWindowActivated() {
  gTopTabWindow = this;
  if (tmapp()->useAds()) {
    if (ads::CDockWidget* dockWidget = mDockManager->focusedDockWidget())
      setMainTitle(dockWidget->windowTitle());
  } else if (tmapp()->useMdi()) {
    if (QMdiSubWindow* sub = mMdiArea->activeSubWindow())
      setMainTitle(sub->windowTitle());
  } else {
    setMainTitle(mTabWidget->tabText(mTabWidget->currentIndex()));
  }
}

void QTMMainTabWindow::onDoubleClickOnEmptyTabBarSpace() {
  eval ("new-document*");
}

void QTMMainTabWindow::setMainTitle(QString title) {
  if (title.isEmpty()) setWindowTitle ("ATHENA");
  else setWindowTitle (QString ("ATHENA [") + title + "]");
}

void QTMMainTabWindow::setMainTitleFromWidget(QWidget* widget) {
  if (!widget) {
    setMainTitle("");
    return;
  }
  if (tmapp()->useAds()) {
    QWidget* p = widget->parentWidget();
    while (p) {
      if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
        if (isDocumentWidget (dockWidget->widget ()))
          buffer_switcher_note_widget (dockWidget->widget ());
        setMainTitle(dockWidget->windowTitle());
        return;
      }
      p = p->parentWidget();
    }
  } else if (tmapp()->useMdi()) {
    if (QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(widget->parentWidget())) {
      if (isDocumentWidget (sub->widget ()))
        buffer_switcher_note_widget (sub->widget ());
      setMainTitle(sub->windowTitle());
      return;
    }
  } else {
    int index = mTabWidget->indexOf(widget);
    if (index != -1) {
      if (isDocumentWidget (widget))
        buffer_switcher_note_widget (widget);
      setMainTitle(mTabWidget->tabText(index));
      return;
    }
  }
  setMainTitle(widget->windowTitle());
}

bool QTMMainTabWindow::adsLayoutPersistenceEnabled() const {
  return tmapp()->useAds() &&
         get_preference ("remember ads panes layout", "on") == "on";
}

QString QTMMainTabWindow::adsLayoutStatePath() const {
  QString home= to_qstring (get_env ("ATHENA_HOME_PATH"));
  if (home.isEmpty()) return QString ();
  return QDir (home).filePath ("system/ads-layout-state.bin");
}

QString QTMMainTabWindow::adsVisiblePanesStatePath() const {
  QString home= to_qstring (get_env ("ATHENA_HOME_PATH"));
  if (home.isEmpty()) return QString ();
  return QDir (home).filePath ("system/ads-visible-panes.txt");
}

void QTMMainTabWindow::saveAdsLayoutState() {
  if (!adsLayoutPersistenceEnabled() || mDockManager == nullptr) return;

  QString path= adsLayoutStatePath();
  if (path.isEmpty()) return;

  QDir dir= QFileInfo (path).dir();
  if (!dir.exists() && !dir.mkpath(".")) {
    std::cerr << "ATHENA] warning, could not create ADS layout cache directory: "
              << dir.absolutePath().toStdString() << std::endl;
    return;
  }

  QSaveFile file (path);
  if (!file.open (QIODevice::WriteOnly)) {
    std::cerr << "ATHENA] warning, could not save ADS layout state to "
              << path.toStdString() << ": "
              << file.errorString().toStdString() << std::endl;
    return;
  }

  file.write (mDockManager->saveState (ATHENA_ADS_LAYOUT_VERSION));
  if (!file.commit()) {
    std::cerr << "ATHENA] warning, could not commit ADS layout state to "
              << path.toStdString() << ": "
              << file.errorString().toStdString() << std::endl;
  }

  QString panesPath= adsVisiblePanesStatePath();
  if (panesPath.isEmpty()) return;

  QSaveFile panesFile (panesPath);
  if (!panesFile.open (QIODevice::WriteOnly | QIODevice::Text)) {
    std::cerr << "ATHENA] warning, could not save ADS visible panes to "
              << panesPath.toStdString() << ": "
              << panesFile.errorString().toStdString() << std::endl;
    return;
  }

  QMap<QString, ads::CDockWidget*> docks= mDockManager->dockWidgetsMap();
  for (auto it= docks.constBegin(); it != docks.constEnd(); ++it) {
    ads::CDockWidget* dock= it.value();
    if (dock == nullptr || dock->isClosed()) continue;
    QString name= dock->objectName();
    if (!isPersistentAdsPane (name)) continue;
    panesFile.write (name.toUtf8());
    panesFile.write ("\n");
  }
  if (!panesFile.commit()) {
    std::cerr << "ATHENA] warning, could not commit ADS visible panes to "
              << panesPath.toStdString() << ": "
              << panesFile.errorString().toStdString() << std::endl;
  }
}

void QTMMainTabWindow::restoreAdsLayoutState() {
  if (!adsLayoutPersistenceEnabled() || mDockManager == nullptr) return;

  QString path= adsLayoutStatePath();
  if (path.isEmpty()) return;

  QFile file (path);
  if (!file.exists()) return;
  if (!file.open (QIODevice::ReadOnly)) {
    std::cerr << "ATHENA] warning, could not read ADS layout state from "
              << path.toStdString() << ": "
              << file.errorString().toStdString() << std::endl;
    return;
  }

  QByteArray state= file.readAll();
  if (!state.isEmpty() &&
      !mDockManager->restoreState (state, ATHENA_ADS_LAYOUT_VERSION)) {
    std::cerr << "ATHENA] warning, ignored incompatible ADS layout state: "
              << path.toStdString() << std::endl;
  }
}

void QTMMainTabWindow::restoreAdsVisiblePanes() {
  if (!adsLayoutPersistenceEnabled() || mDockManager == nullptr) return;

  QString path= adsVisiblePanesStatePath();
  if (path.isEmpty()) return;

  QFile file (path);
  if (!file.exists()) return;
  if (!file.open (QIODevice::ReadOnly | QIODevice::Text)) {
    std::cerr << "ATHENA] warning, could not read ADS visible panes from "
              << path.toStdString() << ": "
              << file.errorString().toStdString() << std::endl;
    return;
  }

  QStringList panes= QString::fromUtf8 (file.readAll()).split ('\n');
  for (const QString& rawName : panes) {
    QString name= rawName.trimmed();
    if (name == "athena-outline-pane") outline_pane_show ();
    else if (vault_active() && name == "athena-vault-explorer")
      vault_show_explorer ();
    else if (vault_active() && name == "athena-global-search")
      global_search_show ();
    else if (vault_active() && name == "athena-vault-backup-viewer")
      vault_backup_viewer_show ();
  }

  restoreAdsLayoutState();
}

void QTMMainTabWindow::scheduleAdsLayoutRestore() {
  if (!adsLayoutPersistenceEnabled()) return;
  QTimer::singleShot (0, this, [this] () { restoreAdsLayoutState(); });
}

void QTMMainTabWindow::setNextWidgetFloating() {
  gNextWidgetFloating = true;
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

  if (event->type() == QEvent::FocusIn) {
    if (QWidget* widget = qobject_cast<QWidget*>(obj))
      setMainTitleFromWidget(widget);
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
  if (isDocument) widget->installEventFilter(this);
  if (isDocument) buffer_switcher_note_widget (widget);
  if (tmapp()->useAds()) {
    ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(widget->parentWidget());
    if (dockWidget) {
      mStackedWidget->setCurrentWidget (mDockManager);
      dockWidget->show();
      dockWidget->raise();
      widget->setFocus();
      setMainTitleFromWidget(widget);
    } else if (isDocument) {
      dockWidget = new ads::CDockWidget(widget->windowTitle());
      dockWidget->setObjectName (
        QString ("athena-document-%1").arg (++gAdsDocumentDockCounter));
      dockWidget->setWidget(widget);
      
      // Use CustomCloseHandling to let TeXmacs handle the safe-exit sequence.
      dockWidget->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
      dockWidget->setFeature(ads::CDockWidget::CustomCloseHandling, true);
      
      connect(dockWidget, &ads::CDockWidget::closeRequested, [widget]() {
        if (widget->metaObject()->indexOfSignal("closed()") != -1) {
          QMetaObject::invokeMethod(widget, "closed");
        }
      });
      
      if (gNextWidgetFloating) {
        mDockManager->addDockWidgetFloating(dockWidget);
        gNextWidgetFloating = false;
      } else {
        mDockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget);
      }

      scheduleAdsLayoutRestore();
      mStackedWidget->setCurrentWidget (mDockManager);
      widget->setFocus();
      setMainTitleFromWidget(widget);
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
      setMainTitleFromWidget(widget);
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
      setMainTitleFromWidget(widget);
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
    setMainTitleFromWidget(widget);
  }
}

QList<QWidget*>
QTMMainTabWindow::documentWidgets() const {
  QList<QWidget*> out;
  if (tmapp()->useAds()) {
    auto map= mDockManager->dockWidgetsMap();
    for (auto it= map.begin (); it != map.end (); ++it) {
      ads::CDockWidget* dockWidget= it.value ();
      if (dockWidget == nullptr) continue;
      QWidget* widget= dockWidget->widget ();
      if (isDocumentWidget (widget) && !out.contains (widget))
        out.append (widget);
    }
  }
  else if (tmapp()->useMdi()) {
    QList<QMdiSubWindow*> windows= mMdiArea->subWindowList ();
    for (QMdiSubWindow* sub : windows) {
      if (sub == nullptr) continue;
      QWidget* widget= sub->widget ();
      if (isDocumentWidget (widget) && !out.contains (widget))
        out.append (widget);
    }
  }
  else {
    for (int i=0; i<mTabWidget->count (); ++i) {
      QWidget* widget= mTabWidget->widget (i);
      if (isDocumentWidget (widget) && !out.contains (widget))
        out.append (widget);
    }
  }
  return out;
}

QWidget*
QTMMainTabWindow::currentDocumentWidget() const {
  QWidget* current= nullptr;
  if (tmapp()->useAds()) {
    if (ads::CDockWidget* dockWidget= mDockManager->focusedDockWidget ())
      current= dockWidget->widget ();
    if (!isDocumentWidget (current)) {
      QTMWidget* last= QTMWidget::getLastFocusedWidget ();
      for (QWidget* widget : documentWidgets ())
        if (widget == last || widget->isAncestorOf (last)) {
          current= widget;
          break;
        }
    }
  }
  else if (tmapp()->useMdi()) {
    if (QMdiSubWindow* sub= mMdiArea->activeSubWindow ())
      current= sub->widget ();
  }
  else current= mTabWidget->currentWidget ();

  if (isDocumentWidget (current) && documentWidgets ().contains (current))
    return current;
  return nullptr;
}

QString
QTMMainTabWindow::documentWidgetTitle(QWidget* widget) const {
  if (widget == nullptr) return QString ();

  if (tmapp()->useAds()) {
    QWidget* p= widget->parentWidget ();
    while (p != nullptr) {
      if (ads::CDockWidget* dockWidget= qobject_cast<ads::CDockWidget*> (p))
        return dockWidget->windowTitle ();
      p= p->parentWidget ();
    }
  }
  else if (tmapp()->useMdi()) {
    if (QMdiSubWindow* sub= qobject_cast<QMdiSubWindow*> (widget->parentWidget ()))
      return sub->windowTitle ();
  }
  else {
    int index= mTabWidget->indexOf (widget);
    if (index >= 0) return mTabWidget->tabText (index);
  }

  return widget->windowTitle ();
}

void
QTMMainTabWindow::activateDocumentWidget(QWidget* widget) {
  if (widget == nullptr || !documentWidgets ().contains (widget)) return;
  showWidget (widget, true);
  buffer_switcher_note_widget (widget);
  activateWindow ();
  widget->setFocus ();
}

void QTMMainTabWindow::removeWidget(QWidget *widget) {
  if (tmapp()->useAds()) {
    QWidget* p = widget->parentWidget();
    while (p) {
      if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
        mDockManager->removeDockWidget(dockWidget);
        dockWidget->deleteLater();
        break;
      }
      p = p->parentWidget();
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
  if (tmapp()->useAds()) {
    QWidget* p = widget->parentWidget();
    while (p) {
      if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
        mDockManager->addDockWidgetFloating(dockWidget);
        break;
      }
      p = p->parentWidget();
    }
  } else if (tmapp()->useMdi()) {
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
        setMainTitle(title);
        break;
      }
      p = p->parentWidget();
    }
  } else if (tmapp()->useMdi()) {
    widget->setWindowTitle (title);
    if (QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(widget->parentWidget()))
      sub->setWindowTitle (title);
    if (widgetOrChildHasFocus(widget)) setMainTitle(title);
  } else {
    int index = mTabWidget->indexOf(widget);
    if (index != -1) {
      mTabWidget->setTabText(index, title);
      if (index == mTabWidget->currentIndex()) setMainTitle(title);
    }
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
