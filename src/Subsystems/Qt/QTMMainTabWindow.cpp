#include "QTMMainTabWindow.hpp"
#include "ATHENA/Features/athena_features.hpp"
#include "QTMApplication.hpp"
#include "QTMBufferSwitcher.hpp"
#include "QTMCustomStylesManager.hpp"
#include "QTMErrorMessagesPane.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMWidget.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMVaultBackupViewer.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMNamespaceExplorer.hpp"
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
#include "QTMPersonsExplorer.hpp"
#endif
#include "QTMNeighborhoodsPane.hpp"
#include "QTMHandwritingSymbolPane.hpp"
#include "qt_window_widget.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tm_server.hpp"
#include "tm_ostream.hpp"
#include "tm_timer.hpp"
#include "vault.hpp"

#include <QMouseEvent>
#include <QTabBar>
#include <QApplication>
#include <QMdiSubWindow>
#include <QByteArray>
#include <QCloseEvent>
#include <QToolButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QTimer>
#include <QStringList>
#include <DockAreaWidget.h>
#include <DockContainerWidget.h>
#include <FloatingDockContainer.h>

QTMMainTabWindow *QTMMainTabWindow::gTopTabWindow = nullptr;
static bool gNextWidgetFloating = false;
static int gAdsDocumentDockCounter = 0;
static bool gAthenaQtClosing = false;

bool
athena_qt_is_closing () {
  return gAthenaQtClosing;
}

bool
athena_has_open_ads_panes () {
  if (qApp == nullptr || !tmapp()->useAds ()) return false;

  for (QWidget* widget: QApplication::topLevelWidgets ()) {
    QTMMainTabWindow* window= qobject_cast<QTMMainTabWindow*> (widget);
    if (window != nullptr && window->hasOpenAdsPanes ()) return true;
  }
  return false;
}

void
qtm_apply_ads_tab_close_preferences () {
  ads::CDockManager::setConfigFlag (
    ads::CDockManager::MiddleMouseButtonClosesTab,
    get_preference ("middle click closes ads tab", "on") == "on");
}

class AthenaQtClosingGuard {
  bool old;
public:
  AthenaQtClosingGuard (): old (gAthenaQtClosing) {
    gAthenaQtClosing= true;
  }
  ~AthenaQtClosingGuard () {
    gAthenaQtClosing= old;
  }
};
static const int ATHENA_ADS_LAYOUT_VERSION = 1;

static bool
isPersistentAdsPane (const QString& name) {
  return name == "athena-outline-pane" ||
         name == "athena-vault-explorer" ||
         name == "athena-namespace-explorer" ||
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
         name == "athena-persons-explorer" ||
#endif
         name == "athena-neighborhoods-pane" ||
         name == "athena-handwritten-symbol" ||
         name == "athena-global-search" ||
         name == "athena-vault-backup-viewer" ||
         name == "athena-error-messages";
}

static QByteArray
adsStateForInspection (const QByteArray& state) {
  QByteArray uncompressed= qUncompress (state);
  return uncompressed.isEmpty()? state: uncompressed;
}

static bool
adsStateMentionsDocumentDocks (const QByteArray& state) {
  return adsStateForInspection (state).contains ("athena-document-");
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

static const char* kAthenaDocumentWidgetProperty= "athena-document-widget";

static bool
isDocumentWidget(QWidget* widget) {
  return widget != nullptr &&
         widget->property (kAthenaDocumentWidgetProperty).toBool ();
}

static ads::CDockWidget*
adsDockWidgetFor(QWidget* widget) {
  for (QWidget* p= widget; p != nullptr; p= p->parentWidget())
    if (ads::CDockWidget* dockWidget= qobject_cast<ads::CDockWidget*> (p))
      return dockWidget;
  return nullptr;
}

bool
qtm_close_focused_ads_tool_pane (QWidget* eventReceiver) {
  if (qApp == nullptr || !tmapp()->useAds ()) return false;

  ads::CDockWidget* dock= adsDockWidgetFor (QApplication::focusWidget ());
  if (dock == nullptr) dock= adsDockWidgetFor (eventReceiver);
  if (dock == nullptr) {
    QTMMainTabWindow* window= QTMMainTabWindow::topTabWindow ();
    if (window != nullptr && window->dockManager () != nullptr)
      dock= window->dockManager ()->focusedDockWidget ();
  }

  if (dock == nullptr || isDocumentWidget (dock->widget ())) return false;
  if (!dock->features ().testFlag (ads::CDockWidget::DockWidgetClosable))
    return false;

  QTMMainTabWindow* owner= nullptr;
  for (QWidget* widget: QApplication::topLevelWidgets ()) {
    QTMMainTabWindow* window= qobject_cast<QTMMainTabWindow*> (widget);
    if (window != nullptr && window->dockManager () == dock->dockManager ()) {
      owner= window;
      break;
    }
  }
  QPointer<ads::CDockWidget> guardedDock= dock;
  QPointer<QTMMainTabWindow> guardedOwner= owner;
  QPointer<QWidget> document=
    owner == nullptr ? nullptr : owner->currentDocumentWidget ();
  QTimer::singleShot (0, dock, [guardedDock, guardedOwner, document] () {
    if (guardedDock == nullptr) return;
    guardedDock->requestCloseDockWidget ();
    if (guardedOwner != nullptr && document != nullptr)
      QTimer::singleShot (0, guardedOwner, [guardedOwner, document] () {
        if (guardedOwner != nullptr && document != nullptr)
          guardedOwner->activateDocumentWidget (document);
      });
  });
  return true;
}

static QWidget*
documentFocusTarget(QWidget* widget) {
  if (widget == nullptr) return nullptr;
  if (QTMWidget* tmWidget= qobject_cast<QTMWidget*> (widget))
    return tmWidget;
  if (QTMWidget* tmWidget= widget->findChild<QTMWidget*> ())
    return tmWidget;
  return widget;
}

static QString
athenaMainWindowBaseTitle() {
#ifdef ATHENA_DEBUG_BUILD
  return "ATHENA DEBUG BUILD";
#else
  return "ATHENA";
#endif
}

QTMMainTabWindow::QTMMainTabWindow()
  : mMdiArea (nullptr), mLastFocusedDocumentWidget (nullptr),
    mAdsLayoutRestoreScheduled (false) {
  bench_start ("construct main window base widgets");
  mStackedWidget = new QStackedWidget(this);
  setCentralWidget (mStackedWidget);
  setWindowTitle (athenaMainWindowBaseTitle());

  mTabWidget = new QTabWidget(mStackedWidget);
  mTabWidget->setTabsClosable(true);
  mTabWidget->setMovable(true);

  // ATHENA uses ADS.  QMdiArea has a surprisingly expensive constructor on
  // Qt 6/Wayland, so retain the legacy path without paying for it unless MDI
  // is explicitly selected before this window is created.
  if (tmapp()->useMdi ()) {
    mMdiArea = new QMdiArea(mStackedWidget);
    mMdiArea->setViewMode (QMdiArea::SubWindowView);
  }
  bench_cumul ("construct main window base widgets");

  qtm_apply_ads_tab_close_preferences ();
  if (QApplication::platformName().startsWith(QStringLiteral("wayland"))) {
    // Keep Wayland floating docks as normal desktop windows. Redocking is
    // started from app-owned ADS tabs/title bars via xdg-toplevel-drag, not
    // from the compositor title bar itself.
    ads::CDockManager::setConfigFlag (
      ads::CDockManager::FloatingContainerForceNativeTitleBar, true);
  }
  else
  {
    // Preserve the existing ADS mouse-grab drag path on XCB, XWayland, and
    // other non-native-Wayland platforms.
    ads::CDockManager::setConfigFlag (
      ads::CDockManager::FloatingContainerForceQWidgetTitleBar, true);
  }
  bench_start ("construct ads dock manager");
  mDockManager = new ads::CDockManager(mStackedWidget);
  bench_cumul ("construct ads dock manager");
  bench_start ("connect main window shell");
  connect(mDockManager, &ads::CDockManager::focusedDockWidgetChanged,
          this, [this](ads::CDockWidget*, ads::CDockWidget* now) {
            if (now) {
              if (isDocumentWidget (now->widget ()))
                mLastFocusedDocumentWidget= now->widget ();
              setMainTitle(now->windowTitle());
            }
            else setMainTitle("");
          });
  connect(qApp, &QCoreApplication::aboutToQuit,
          this, &QTMMainTabWindow::saveAdsLayoutState);

  mStackedWidget->addWidget (mTabWidget);
  if (mMdiArea != nullptr) mStackedWidget->addWidget (mMdiArea);
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
  if (mMdiArea != nullptr)
    connect(mMdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)),
            this, SLOT(onSubWindowActivated(QMdiSubWindow*)));

  installEventFilter(this);
  mTabWidget->tabBar()->installEventFilter(this);

  gTopTabWindow = this;
  bench_cumul ("connect main window shell");
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
    AthenaQtClosingGuard guard;
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
  QString base= athenaMainWindowBaseTitle();
  if (title.isEmpty()) setWindowTitle (base);
  else setWindowTitle (base + " [" + title + "]");
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

void QTMMainTabWindow::showAfterContentReady(QWidget* focusWidget) {
  if (!isVisible()) {
    show();
    QRect screenGeometry = QApplication::screens().at(0)->geometry();
    move(screenGeometry.center() - rect().center());
    raise();
    activateWindow();
  }
  if (focusWidget != nullptr) focusWidget->setFocus();
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

void QTMMainTabWindow::showAdsDockWidget(ads::CDockWidget* dock,
                                         ads::DockWidgetArea area) {
  if (dock == nullptr || mDockManager == nullptr) return;

  ads::CDockContainerWidget* targetContainer= activeAdsDockContainer ();
  ads::CDockAreaWidget* targetArea= activeAdsDockArea (targetContainer);
  bool isUnplaced= dock->dockAreaWidget () == nullptr ||
                   dock->dockContainer () == nullptr;
  bool isInOtherWindow= targetContainer != nullptr &&
                        dock->dockContainer () != targetContainer;

  if (isUnplaced || isInOtherWindow) {
    if (targetArea != nullptr)
      mDockManager->addDockWidget (area, dock, targetArea);
    else if (targetContainer != nullptr && targetContainer != mDockManager)
      mDockManager->addDockWidgetToContainer (area, dock, targetContainer);
    else
      mDockManager->addDockWidget (area, dock);
    scheduleAdsLayoutRestore (dock, area);
  }

  dock->toggleView (true);
  dock->show ();
  dock->raise ();
}

ads::CDockContainerWidget*
QTMMainTabWindow::activeAdsDockContainer() const {
  if (mDockManager == nullptr) return nullptr;

  QWidget* active= QApplication::activeWindow ();
  for (QWidget* widget= active; widget != nullptr;
       widget= widget->parentWidget ()) {
    if (ads::CFloatingDockContainer* floating=
          qobject_cast<ads::CFloatingDockContainer*> (widget)) {
      ads::CDockContainerWidget* container= floating->dockContainer ();
      if (container != nullptr && container->dockManager () == mDockManager)
        return container;
    }
    if (widget == this) return mDockManager;
  }

  if (ads::CDockWidget* focused=
        adsDockWidgetFor (QApplication::focusWidget ()))
    if (focused->dockManager () == mDockManager &&
        focused->dockContainer () != nullptr)
      return focused->dockContainer ();

  if (ads::CDockWidget* focused= mDockManager->focusedDockWidget ())
    if (focused->dockContainer () != nullptr)
      return focused->dockContainer ();

  return mDockManager;
}

ads::CDockAreaWidget*
QTMMainTabWindow::activeAdsDockArea(
  ads::CDockContainerWidget* container) const {
  if (container == nullptr || mDockManager == nullptr) return nullptr;

  if (ads::CDockWidget* focused=
        adsDockWidgetFor (QApplication::focusWidget ()))
    if (focused->dockManager () == mDockManager &&
        focused->dockContainer () == container)
      return focused->dockAreaWidget ();

  if (ads::CDockWidget* focused= mDockManager->focusedDockWidget ())
    if (focused->dockContainer () == container)
      return focused->dockAreaWidget ();

  QList<ads::CDockAreaWidget*> areas= container->openedDockAreas ();
  return areas.isEmpty () ? nullptr : areas.first ();
}

void QTMMainTabWindow::saveAdsLayoutState() {
  if (!adsLayoutPersistenceEnabled() || mDockManager == nullptr) return;

  QString path= adsLayoutStatePath();
  if (path.isEmpty()) return;

  QDir dir= QFileInfo (path).dir();
  if (!dir.exists() && !dir.mkpath(".")) {
    std_warning << "could not create ADS layout cache directory: "
                << from_qstring (dir.absolutePath()) << LF;
    return;
  }

  QByteArray state= mDockManager->saveState (ATHENA_ADS_LAYOUT_VERSION);
  if (adsStateMentionsDocumentDocks (state)) {
    QFile::remove (path);
  }
  else {
    QSaveFile file (path);
    if (!file.open (QIODevice::WriteOnly)) {
      std_warning << "could not save ADS layout state to "
                  << from_qstring (path) << ": "
                  << from_qstring (file.errorString()) << LF;
      return;
    }

    file.write (state);
    if (!file.commit()) {
      std_warning << "could not commit ADS layout state to "
                  << from_qstring (path) << ": "
                  << from_qstring (file.errorString()) << LF;
    }
  }

  QString panesPath= adsVisiblePanesStatePath();
  if (panesPath.isEmpty()) return;

  QSaveFile panesFile (panesPath);
  if (!panesFile.open (QIODevice::WriteOnly | QIODevice::Text)) {
    std_warning << "could not save ADS visible panes to "
                << from_qstring (panesPath) << ": "
                << from_qstring (panesFile.errorString()) << LF;
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
    std_warning << "could not commit ADS visible panes to "
                << from_qstring (panesPath) << ": "
                << from_qstring (panesFile.errorString()) << LF;
  }
}

void QTMMainTabWindow::restoreAdsLayoutState() {
  if (!adsLayoutPersistenceEnabled() || mDockManager == nullptr) return;

  QString path= adsLayoutStatePath();
  if (path.isEmpty()) return;

  QFile file (path);
  if (!file.exists()) return;
  if (!file.open (QIODevice::ReadOnly)) {
    std_warning << "could not read ADS layout state from "
                << from_qstring (path) << ": "
                << from_qstring (file.errorString()) << LF;
    return;
  }

  QByteArray state= file.readAll();
  if (adsStateMentionsDocumentDocks (state)) {
    file.close();
    QFile::remove (path);
    std_warning << "ignored ADS layout state containing document docks: "
                << from_qstring (path) << LF;
    return;
  }
  if (!state.isEmpty() &&
      !mDockManager->restoreState (state, ATHENA_ADS_LAYOUT_VERSION)) {
    std_warning << "ignored incompatible ADS layout state: "
                << from_qstring (path) << LF;
  }
}

void QTMMainTabWindow::restoreAdsVisiblePanes() {
  if (!adsLayoutPersistenceEnabled() || mDockManager == nullptr) return;

  QString path= adsVisiblePanesStatePath();
  if (path.isEmpty()) return;

  QFile file (path);
  if (!file.exists()) return;
  if (!file.open (QIODevice::ReadOnly | QIODevice::Text)) {
    std_warning << "could not read ADS visible panes from "
                << from_qstring (path) << ": "
                << from_qstring (file.errorString()) << LF;
    return;
  }

  QStringList panes= QString::fromUtf8 (file.readAll()).split ('\n');
  for (const QString& rawName : panes) {
    QString name= rawName.trimmed();
    if (name == "athena-outline-pane") outline_pane_show ();
    else if (vault_active() && name == "athena-vault-explorer")
      vault_show_explorer ();
    else if (vault_active() && name == "athena-namespace-explorer")
      namespace_explorer_show ();
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    else if (vault_active() && name == "athena-persons-explorer")
      persons_explorer_show ();
#endif
    else if (vault_active() && name == "athena-neighborhoods-pane")
      neighborhoods_pane_show ();
    else if (name == "athena-handwritten-symbol")
      handwriting_symbol_pane_show ();
    else if (vault_active() && name == "athena-global-search")
      global_search_show ();
    else if (vault_active() && name == "athena-vault-backup-viewer")
      vault_backup_viewer_show ();
    else if (name == "athena-error-messages" ||
             name == "athena-tool-pane-Error messages")
      error_messages_show ();
    else if (name == "athena-custom-styles-manager")
      custom_styles_manager_show ();
  }

  scheduleAdsLayoutRestore();
}

void QTMMainTabWindow::scheduleAdsLayoutRestore(
  ads::CDockWidget* revealDock, ads::DockWidgetArea area) {
  if (!adsLayoutPersistenceEnabled()) return;
  if (revealDock != nullptr) {
    bool alreadyQueued= false;
    for (const auto& pending: mAdsDocksToReveal)
      if (pending.first == revealDock) {
        alreadyQueued= true;
        break;
      }
    if (!alreadyQueued)
      mAdsDocksToReveal.append (qMakePair (QPointer<ads::CDockWidget> (
                                            revealDock), area));
  }
  if (mAdsLayoutRestoreScheduled) return;
  mAdsLayoutRestoreScheduled= true;
  QTimer::singleShot (0, this, [this] () {
    restoreAdsLayoutState();
    QList<QPair<QPointer<ads::CDockWidget>, ads::DockWidgetArea>> pending=
      std::move (mAdsDocksToReveal);
    mAdsDocksToReveal.clear ();
    for (const auto& reveal: pending) {
      ads::CDockWidget* dock= reveal.first;
      if (dock == nullptr) continue;
      if (dock->dockAreaWidget () == nullptr ||
          dock->dockContainer () == nullptr) {
        if (reveal.second == ads::NoDockWidgetArea)
          mDockManager->addDockWidgetFloating (dock);
        else
          mDockManager->addDockWidget (reveal.second, dock);
      }
      dock->toggleView (true);
      dock->show ();
      dock->raise ();
    }
    mAdsLayoutRestoreScheduled= false;
  });
}

void QTMMainTabWindow::setNextWidgetFloating() {
  gNextWidgetFloating = true;
}

bool QTMMainTabWindow::eventFilterWindow(QObject *obj, QEvent *event) {
  // if the window is a top level window
  if (event->type() == QEvent::WindowActivate) {
    if (DEBUG_QT_WIDGETS) cout << "TabWindow: WindowActivated" << LF;
    onWindowActivated();
  }

  if (event->type() == QEvent::MouseButtonPress &&
      !tmapp()->useAds() && !tmapp()->useMdi()) {
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
}

bool QTMMainTabWindow::eventFilterTabBar(QObject *obj, QEvent *event) {
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
      bool wasDocument = isDocumentWidget(widgetToMove);
      mTabWidget->removeTab(movingTabIndex);
      newTabWindow->showWidget(widgetToMove, wasDocument);
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
      if (tabWindow == nullptr || tmapp()->useMdi()) continue;

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
      bool wasDocument = isDocumentWidget(widgetToMove);
      mTabWidget->removeTab(movingTabIndex);
      targetTabWindow->showWidget(widgetToMove, wasDocument);
      targetTabWindow->setDefaultStyle();
      targetTabWindow->activateWindow();
      targetTabWindow = nullptr;
      if (mTabWidget->count() == 0) {
        closeAndSetTopTabWindow();
      }
    }
  }
  return QMainWindow::eventFilter(obj, event);
}

bool QTMMainTabWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Close) {
    debug_qt << "ATHENA ADS DEBUG: QEvent::Close received on object of type: "
             << obj->metaObject()->className() << LF;
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
        int y = mouseEvent->position().toPoint().y();
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
  if (widget != nullptr)
    widget->setProperty (kAthenaDocumentWidgetProperty, isDocument);
  if (isDocument) mLastFocusedDocumentWidget= widget;
  if (isDocument) widget->installEventFilter(this);
  if (isDocument) buffer_switcher_note_widget (widget);
  if (tmapp()->useAds()) {
    ads::CDockWidget* dockWidget = adsDockWidgetFor(widget);
    if (dockWidget) {
      mStackedWidget->setCurrentWidget (mDockManager);
      dockWidget->toggleView(true);
      dockWidget->raise();
      mDockManager->setDockWidgetFocused(dockWidget);
      if (QWidget* focusTarget= documentFocusTarget(widget))
        focusTarget->setFocus(Qt::OtherFocusReason);
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
      if (QWidget* focusTarget= documentFocusTarget(widget))
        focusTarget->setFocus(Qt::OtherFocusReason);
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
  showAfterContentReady(widget);
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

bool
QTMMainTabWindow::hasOpenAdsPanes() const {
  if (!tmapp()->useAds () || mDockManager == nullptr) return false;

  QMap<QString, ads::CDockWidget*> docks= mDockManager->dockWidgetsMap ();
  for (auto it= docks.constBegin (); it != docks.constEnd (); ++it) {
    ads::CDockWidget* dock= it.value ();
    if (dock != nullptr && (!dock->isClosed () || dock->isVisible ()) &&
        !isDocumentWidget (dock->widget ()))
      return true;
  }
  return false;
}

QWidget*
QTMMainTabWindow::currentDocumentWidget() const {
  QWidget* current= nullptr;
  if (tmapp()->useAds()) {
    if (ads::CDockWidget* dockWidget= mDockManager->focusedDockWidget ())
      current= dockWidget->widget ();
    if (!isDocumentWidget (current)) {
      if (mLastFocusedDocumentWidget != nullptr &&
          documentWidgets ().contains (mLastFocusedDocumentWidget))
        current= mLastFocusedDocumentWidget;
    }
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
    if (ads::CDockWidget* dockWidget= adsDockWidgetFor(widget))
      return dockWidget->windowTitle ();
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
  if (QWidget* focusTarget= documentFocusTarget(widget))
    focusTarget->setFocus(Qt::OtherFocusReason);
}

bool
QTMMainTabWindow::placeDocumentWidgetsSideBySide(QWidget* left,
                                                 QWidget* right) {
  if (left == nullptr || right == nullptr || left == right) return false;

  if (tmapp()->useAds()) {
    ads::CDockWidget* leftDock= adsDockWidgetFor (left);
    ads::CDockWidget* rightDock= adsDockWidgetFor (right);
    if (leftDock == nullptr || rightDock == nullptr ||
        leftDock->dockAreaWidget () == nullptr)
      return false;
    leftDock->toggleView (true);
    rightDock->toggleView (true);
    mDockManager->addDockWidget (ads::RightDockWidgetArea, rightDock,
                                 leftDock->dockAreaWidget ());
    mDockManager->setDockWidgetFocused (leftDock);
    return true;
  }

  if (tmapp()->useMdi()) {
    tileSubWindows ();
    return true;
  }
  return false;
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
  
  if (nr_windows <= 1 && !hasOpenAdsPanes ()) {
    if (is_server_started()) {
      AthenaQtClosingGuard guard;
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
    "   min-height: 26px !important; "
    "   max-height: 26px !important; "
    "} "
    "ads--CDockWidgetTab { "
    "   min-height: 26px !important; "
    "   max-height: 26px !important; "
    "   padding: 0 8px !important; "
    "} "
    "ads--CTitleBarButton, "
    "ads--CDockAreaTitleBar QToolButton, "
    "#tabsMenuButton, #dockAreaCloseButton, #detachGroupButton, "
    "#tabCloseButton, #floatingTitleCloseButton, #floatingTitleMaximizeButton { "
    "   qproperty-iconSize: 16px 16px !important; "
    "   min-width: 22px !important; "
    "   min-height: 22px !important; "
    "   max-width: 22px !important; "
    "   max-height: 22px !important; "
    "   width: 22px !important; "
    "   height: 22px !important; "
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
