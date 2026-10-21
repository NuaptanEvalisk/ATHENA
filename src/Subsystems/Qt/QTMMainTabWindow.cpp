#include "QTMMainTabWindow.hpp"
#include "athena_platform.hpp"
#if ATHENA_PLATFORM_IPADOS
#include "Subsystems/iOS/athena_ios.hpp"
#endif
#include "ATHENA/Features/athena_features.hpp"
#include "QTMApplicationMenuPresenter.hpp"
#include "QTMApplication.hpp"
#include "QTMBufferSwitcher.hpp"
#include "QTMCustomStylesManager.hpp"
#include "QTMErrorMessagesPane.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMWidget.hpp"
#include "QTMWindow.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMDocumentHistoryPane.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMNamespaceExplorer.hpp"
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
#include "QTMPersonsExplorer.hpp"
#endif
#include "QTMNeighborhoodsPane.hpp"
#include "QTMHodarium.hpp"
#include "QTMHandwritingSymbolPane.hpp"
#include "ATHENA/Data/background_workers.hpp"
#include "qt_window_widget.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tm_server.hpp"
#include "tm_ostream.hpp"
#include "tm_timer.hpp"
#include "vault.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QToolButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMap>
#include <QProgressBar>
#include <QShortcut>
#include <QSaveFile>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTimer>
#include <QStringList>
#include <DockAreaWidget.h>
#include <DockContainerWidget.h>
#include <FloatingDockContainer.h>

#include <algorithm>
#include <climits>
#include <vector>

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
  if (qApp == nullptr) return false;

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

static void
applyAdsTheme (QWidget* root, const QString& styleSheet) {
  root->setProperty ("athenaDarkMode", QTMIconManager::is_dark_mode ());
  root->setProperty ("athenaTouchUi", bool (ATHENA_PLATFORM_IPADOS));
  root->setStyleSheet (styleSheet);
}

static void
installApplicationBackgroundStatus (QTMMainTabWindow* shell, QMainWindow* host) {
  if (shell == nullptr) return;
  QStatusBar* bar= new QStatusBar (host);
  bar->setObjectName (QStringLiteral ("athenaApplicationStatus"));
  bar->setSizeGripEnabled (false);
  host->setStatusBar (bar);

  QWidget* widget= new QWidget (bar);
  auto* layout= new QHBoxLayout (widget);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->setSpacing (5);
  QLabel* indicator= new QLabel (widget);
  indicator->setFixedSize (10, 10);
  auto* title= new QToolButton (widget);
  title->setText (QStringLiteral ("Background"));
  title->setAutoRaise (true);
  title->setToolButtonStyle (Qt::ToolButtonTextOnly);
  title->setMinimumWidth (title->fontMetrics ().horizontalAdvance (QStringLiteral ("Maintenance")) + 8);
  title->setAccessibleName (QObject::tr ("Background workers"));
  auto* workersMenu= new QMenu (title);
  title->setMenu (workersMenu);
  title->setPopupMode (QToolButton::MenuButtonPopup);
  QObject::connect (title, &QToolButton::clicked, widget, [title, workersMenu] {
    if (title->property ("hodarium").toBool ()) {
      const auto value= athena::background::snapshot ()[std::size_t (athena::background::worker::hodarium)];
      if (value.conflicts) qtm_hodarium_show_conflicts (); else qtm_hodarium_show ();
    }
    else workersMenu->popup (title->mapToGlobal (QPoint (0, title->height ())));
  });
  QObject::connect (workersMenu, &QMenu::aboutToShow, widget, [workersMenu] {
    workersMenu->clear ();
    const auto states= athena::background::snapshot ();
    for (std::size_t i= 0; i < states.size (); ++i) {
      const auto& state= states[i];
      if (state.state == athena::background::phase::inactive) continue;
      auto* item= workersMenu->addAction (QString::fromLatin1 (
        athena::background::worker_name (athena::background::worker (i))) +
        (state.caption.empty () ? QString () : QStringLiteral (": ") + QString::fromStdString (state.caption)));
      item->setToolTip (QString::fromStdString (state.detail));
      if (i == std::size_t (athena::background::worker::hodarium))
        QObject::connect (item, &QAction::triggered, workersMenu, [conflicts= state.conflicts] {
          if (conflicts) qtm_hodarium_show_conflicts (); else qtm_hodarium_show ();
        });
      else item->setEnabled (false);
    }
  });
  title->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Preferred);
  QProgressBar* progress= new QProgressBar (widget);
  progress->setTextVisible (false);
  progress->setFixedWidth (105);
  progress->setFixedHeight (8);
  progress->hide ();
  QLabel* count= new QLabel (widget);
  count->setMinimumWidth (0);
  count->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  layout->addWidget (indicator);
  layout->addWidget (title);
  layout->addWidget (progress);
  layout->addWidget (count, 1);
  widget->setMinimumWidth (0);
  widget->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  QWidget* contents= new QWidget (bar);
  contents->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* row= new QHBoxLayout (contents);
  row->setContentsMargins (8, 0, 8, 0);
  row->setSpacing (12);
  QLabel* center= new QLabel (contents);
  QLabel* right= new QLabel (contents);
  center->setObjectName (QStringLiteral ("athenaStatusCenter"));
  right->setObjectName (QStringLiteral ("athenaStatusRight"));
  center->setAlignment (Qt::AlignCenter);
  right->setAlignment (Qt::AlignRight | Qt::AlignVCenter);
  center->setMinimumWidth (0);
  right->setMinimumWidth (0);
  center->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  right->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  // Equal, content-independent columns keep the document status centered
  // across worker rotation, progress visibility and changing cursor details.
  row->addWidget (widget, 1);
  row->addWidget (center, 1);
  row->addWidget (right, 1);
  bar->addWidget (contents, 1);
  bar->setMinimumWidth (2);

  auto* timer= new QTimer (widget);
  timer->setInterval (160);
  QObject::connect (
    timer, &QTimer::timeout, widget,
    [shell, host, center, right, widget, layout, indicator, title, progress, count, tick=0u] () mutable {
      const auto* document= qobject_cast<QTMWindow*> (
        shell->activeWorkPaneWidget (host->window ()));
      const auto status= document && document->editorStatus.visible ?
        document->editorStatus : QTMWindow::EditorStatus {};
      center->setToolTip (status.center);
      right->setToolTip (status.right);
      right->setText (right->fontMetrics ().elidedText (
        status.right, Qt::ElideRight, right->width ()));
      center->setText (center->fontMetrics ().elidedText (
        status.center, Qt::ElideRight, center->width ()));
      using athena::background::phase;
      const auto statuses= athena::background::snapshot ();
      auto name= [] (std::size_t i) { return QString::fromLatin1 (
        athena::background::worker_name (athena::background::worker (i))); };
      std::vector<std::size_t> busy;
      bool active= false;
      bool failed= false;
      bool waiting= false;
      std::size_t notable= statuses.size ();
      QString tip;
      for (std::size_t i= 0; i < statuses.size (); ++i) {
        const auto& status= statuses[i];
        if (status.state == phase::inactive) continue;
        active= active || status.state == phase::idle;
        failed= failed || athena::background::failed (status);
        waiting= waiting || status.state == phase::waiting;
        if (athena::background::failed (status) ||
            (notable == statuses.size () && status.state == phase::waiting)) notable= i;
        if (status.state == phase::working) busy.push_back (i);
        if (!tip.isEmpty ()) tip += QStringLiteral ("\n");
        tip += name (i) + QStringLiteral (": ") +
          (!status.caption.empty () ? QString::fromStdString (status.caption) :
           status.state == phase::working ? QStringLiteral ("Working") :
           status.state == phase::paused ? QStringLiteral ("Paused") :
           status.state == phase::waiting ? QStringLiteral ("Waiting") :
           status.state == phase::disconnected ? QStringLiteral ("Disconnected") :
           status.state == phase::error ? QStringLiteral ("Error") :
                                           QStringLiteral ("Idle"));
        if (status.total)
          tip += QStringLiteral (" %1/%2")
                   .arg (qulonglong (status.current))
                   .arg (qulonglong (status.total));
        if (status.errors)
          tip += QStringLiteral ("; %1 errors")
                   .arg (qulonglong (status.errors));
        if (!status.detail.empty ())
          tip += QStringLiteral ("\n") + QString::fromStdString (status.detail);
        if (!status.error_detail.empty () &&
            status.error_detail != status.detail)
          tip += QStringLiteral ("\nError: ") +
                 QString::fromStdString (status.error_detail);
      }

      const bool working= !busy.empty ();
      const std::size_t activeIndex=
        working ? busy[(tick / 19) % busy.size ()] : notable;
      const bool selected= activeIndex < statuses.size ();
      const auto current=
        selected ? statuses[activeIndex] : athena::background::progress {};
      title->setText (
        selected ? name (activeIndex) :
                  QStringLiteral ("Background"));
      title->setProperty ("hodarium", selected && activeIndex == std::size_t (athena::background::worker::hodarium));
      ++tick;

      const QString color=
        failed ? QStringLiteral ("#d94b4b") :
        waiting ? QStringLiteral ("#c58a12") :
        working ? QStringLiteral ("#2d7ff9") :
        active ? QStringLiteral ("#36a852") :
                 QStringLiteral ("#808080");
      indicator->setStyleSheet (
        QStringLiteral ("background:%1;border-radius:5px;").arg (color));
      const int progressWidth= indicator->width () + title->sizeHint ().width () +
        progress->width () + 3 * layout->spacing ();
      progress->setVisible (working && widget->width () >= progressWidth);
      QString caption= QString::fromStdString (current.caption);
      if (working) {
        if (current.total == 0) progress->setRange (0, 0);
        else {
          progress->setRange (0, 10000);
          progress->setValue (int (10000.0 * double (std::min (current.current, current.total)) / double (current.total)));
          caption+= (caption.isEmpty () ? QString () : QStringLiteral (" ")) +
            (current.bytes ? QLocale ().formattedDataSize (qint64 (current.current)) + QStringLiteral ("/") +
                             QLocale ().formattedDataSize (qint64 (current.total)) : QStringLiteral ("%1/%2")
              .arg (qulonglong (current.current))
              .arg (qulonglong (current.total)));
        }
      }
      layout->activate ();
      count->setText (count->fontMetrics ().elidedText (caption, Qt::ElideRight, count->width ()));
      if (failed)
        tip.prepend (
          QStringLiteral (
            "A background worker reported an error. "
            "The progress label rotates independently.\n\n"));
      widget->setToolTip (tip);
    });
  timer->start ();
}

// ADS retains ownership of docking and window movement. Each floating window
// hosts the same application chrome as the main shell, around its container.
class QTMFloatingWindowChrome final: public QMainWindow {
  std::unique_ptr<QTMApplicationMenuPresenter> presenter;
public:
  QTMFloatingWindowChrome (QTMMainTabWindow* shell,
                          ads::CFloatingDockContainer* floating):
    QMainWindow (floating) {
    setWindowFlags (Qt::Widget);
    setCentralWidget (floating->dockContainer ());
    installApplicationBackgroundStatus (shell, this);
    presenter= std::make_unique<QTMApplicationMenuPresenter> (shell, this);
    if (!presenter->activate ())
      qWarning ("Could not activate floating ATHENA application menubar");
  }
};

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
         name == "athena-document-history" ||
         name == "athena-error-messages" || name == "athena-hodarium";
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
  if (qApp == nullptr) return false;

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
  if (QTMWindow* window= qobject_cast<QTMWindow*> (widget))
    if (QTMWidget* tmWidget= window->editorCanvas ()) return tmWidget;
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
  : mLastFocusedDocumentWidget (nullptr), mAdsLayoutRestoreScheduled (false),
    mAdsLayoutRestoreAttempted (false) {
  bench_start ("construct main window base widgets");
  setWindowTitle (athenaMainWindowBaseTitle());
  bench_cumul ("construct main window base widgets");

  qtm_apply_ads_tab_close_preferences ();
  // ADS creates its focus controller only with this flag. Shell status,
  // document commands and Outline all consume that controller's active dock.
  ads::CDockManager::setConfigFlag (ads::CDockManager::FocusHighlighting, true);
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
  mDockManager = new ads::CDockManager(this);
  connect (mDockManager, &ads::CDockManager::floatingWidgetCreated, this,
    [this] (ads::CFloatingDockContainer* floating) {
      // Wayland floating windows deliberately have no QObject parent, so
      // their ADS theme cannot be inherited from the dock manager.
      applyAdsTheme (floating, mDockManager->styleSheet ());
      auto* chrome= new QTMFloatingWindowChrome (this, floating);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS) && !defined(Q_OS_IOS)
      floating->setWidget (chrome);
#else
      floating->layout ()->addWidget (chrome);
#endif
      floating->installEventFilter (this);
      connect (floating, &QObject::destroyed, this, [this, floating] {
        mWindowWorkPanes.remove (floating);
        mWindowDocuments.remove (floating);
      });
    });
  mAdsBaseStyleSheet= mDockManager->styleSheet ();
  connect (mDockManager, &ads::CDockManager::dockWidgetAdded, this,
    [] (ads::CDockWidget* dock) {
      if (isDocumentWidget (dock->widget ()) || !dock->icon ().isNull ()) return;
      QIcon icon;
      for (const auto& command: QTMCommandRegistry::instance ().commands ()) {
        if (!command.dockObjectName.isEmpty () &&
            command.dockObjectName == dock->objectName ()) {
          icon= tmapp ()->icon_manager ().getPresentationIcon (command.icon);
          break;
        }
      }
      if (icon.isNull ())
        icon= tmapp ()->icon_manager ().getPresentationIcon ("applications-system");
      dock->setIcon (icon);
    });
#if ATHENA_PLATFORM_IPADOS
  connect (mDockManager, &ads::CDockManager::dockWidgetAdded, this,
    [] (ads::CDockWidget* dock) {
      dock->setFeature (ads::CDockWidget::DockWidgetFloatable, false);
    });
#endif
  setCentralWidget (mDockManager);
  installApplicationBackgroundStatus (this, this);
  // The application shell owns the menubar.  Editor actors publish command
  // state only; they no longer construct or replace a Scheme menu widget.
  mNativeMenuPresenter= std::make_unique<QTMApplicationMenuPresenter> (this);
  bench_cumul ("construct ads dock manager");
  bench_start ("connect main window shell");
  connect(mDockManager, &ads::CDockManager::focusedDockWidgetChanged,
          this, [this](ads::CDockWidget*, ads::CDockWidget* now) {
            if (now) {
              mLastFocusedWorkPaneWidget= now->widget ();
              mWindowWorkPanes[now->window ()]= now->widget ();
              if (isDocumentWidget (now->widget ())) {
                mLastFocusedDocumentWidget= now->widget ();
                mWindowDocuments[now->window ()]= now->widget ();
              }
              setMainTitle(now->windowTitle());
            }
            else setMainTitle("");
          });
  connect(qApp, &QCoreApplication::aboutToQuit,
           this, &QTMMainTabWindow::saveAdsLayoutState);

  auto* nextTab= new QShortcut (QKeySequence (Qt::CTRL | Qt::Key_Tab), this);
  nextTab->setContext (Qt::WindowShortcut);
  connect (nextTab, &QShortcut::activated, this,
           [this] { cycleActiveAdsTab (1); });

  auto* previousTab= new QShortcut (
    QKeySequence (Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
  previousTab->setContext (Qt::WindowShortcut);
  connect (previousTab, &QShortcut::activated, this,
           [this] { cycleActiveAdsTab (-1); });

  // todo : keep the tab window size and position in the user preferences
#if ATHENA_PLATFORM_IPADOS
  setMinimumSize (320, 240);
#else
  setMinimumSize(800, 600);
#endif

  setAttribute(Qt::WA_DeleteOnClose);

  // remove the border and padding
  setDefaultStyle();

  installEventFilter(this);

  gTopTabWindow = this;
  if (!mNativeMenuPresenter->activate ())
    qWarning ("Could not activate native ATHENA application menubar");
#if ATHENA_PLATFORM_IPADOS
  athena_ios_register_shell (this);
#endif
  bench_cumul ("connect main window shell");
}

QTMMainTabWindow::~QTMMainTabWindow() {
  // Floating-window destruction notifies the per-window focus maps. Tear down
  // ADS while those members still exist, before QWidget deletes its children.
  mNativeMenuPresenter.reset ();
  delete mDockManager;
  mDockManager= nullptr;
  if (gTopTabWindow == this) {
    gTopTabWindow = nullptr;
  }
}

void QTMMainTabWindow::closeEvent(QCloseEvent *event) {
#if ATHENA_PLATFORM_IPADOS
  event->ignore ();
  athena_ios_close_scene (this);
  return;
#endif
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
  if (ads::CDockWidget* dockWidget = mDockManager->focusedDockWidget())
    setMainTitle(dockWidget->windowTitle());
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
  setMainTitle(widget->windowTitle());
}

void QTMMainTabWindow::showAfterContentReady(QWidget* focusWidget) {
  if (!isVisible()) {
    show();
#if !ATHENA_PLATFORM_IPADOS
    QRect screenGeometry = QApplication::screens().at(0)->geometry();
    move(screenGeometry.center() - rect().center());
#endif
    raise();
    activateWindow();
  }
  if (focusWidget != nullptr) focusWidget->setFocus();
}

bool QTMMainTabWindow::adsLayoutPersistenceEnabled() const {
  return get_preference ("remember ads panes layout", "on") == "on";
}

QString QTMMainTabWindow::adsLayoutStatePath() const {
  QString home= to_qstring (get_env ("ATHENA_HOME_PATH"));
  if (home.isEmpty()) return QString ();
  QString key= property ("athena.layoutKey").toString ();
  return QDir (home).filePath (key.isEmpty () ? "system/ads-layout-state.bin" :
    "system/ads-layout-" + key + ".bin");
}

QString QTMMainTabWindow::adsVisiblePanesStatePath() const {
  QString home= to_qstring (get_env ("ATHENA_HOME_PATH"));
  if (home.isEmpty()) return QString ();
  QString key= property ("athena.layoutKey").toString ();
  return QDir (home).filePath (key.isEmpty () ? "system/ads-visible-panes.txt" :
    "system/ads-visible-panes-" + key + ".txt");
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

void
QTMMainTabWindow::cycleActiveAdsTab(int delta) {
  if (delta == 0 || mDockManager == nullptr) return;
  ads::CDockContainerWidget* container= activeAdsDockContainer ();
  ads::CDockAreaWidget* area= activeAdsDockArea (container);
  if (area == nullptr) return;

  const QList<ads::CDockWidget*> tabs= area->openedDockWidgets ();
  if (tabs.size () < 2) return;
  ads::CDockWidget* current= area->currentDockWidget ();
  int index= tabs.indexOf (current);
  if (index < 0) index= 0;
  const int count= tabs.size ();
  const int nextIndex= (index + (delta > 0 ? 1 : -1) + count) % count;
  ads::CDockWidget* next= tabs[nextIndex];
  if (next == nullptr) return;

  area->setCurrentDockWidget (next);
  mDockManager->setDockWidgetFocused (next);
  if (isDocumentWidget (next->widget ()))
    mLastFocusedDocumentWidget= next->widget ();
  setMainTitle (next->windowTitle ());
  if (QWidget* target= isDocumentWidget (next->widget ()) ?
        documentFocusTarget (next->widget ()) : next->widget ())
    target->setFocus (Qt::ShortcutFocusReason);
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
  if (mAdsLayoutRestoreAttempted) return;
  mAdsLayoutRestoreAttempted= true;

  // This cache contains tool panes only. ADS closes and unassigns every dock
  // absent from it, so it must never be restored over live document panes.
  if (!documentWidgets ().isEmpty ()) return;

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
    else if (vault_active() && name == "athena-document-history")
      document_history_pane_show ();
    else if (name == "athena-error-messages" ||
             name == "athena-tool-pane-Error messages")
      error_messages_show ();
    else if (name == "athena-custom-styles-manager")
      custom_styles_manager_show ();
    else if (name == "athena-hodarium")
      qtm_hodarium_show ();
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
        if (reveal.second == ads::NoDockWidgetArea) {
#if ATHENA_PLATFORM_IPADOS
          mDockManager->addDockWidget (ads::CenterDockWidgetArea, dock);
#else
          mDockManager->addDockWidgetFloating (dock);
#endif
        } else
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
  (void) obj;
  if ((event->type () == QEvent::PaletteChange ||
       event->type () == QEvent::ApplicationPaletteChange) &&
      mDockManager->property ("athenaDarkMode").toBool () !=
        QTMIconManager::is_dark_mode ())
    setDefaultStyle ();
  if (event->type() == QEvent::WindowActivate) {
    if (DEBUG_QT_WIDGETS) cout << "TabWindow: WindowActivated" << LF;
    onWindowActivated();
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

  if (obj == this || qobject_cast<ads::CFloatingDockContainer*> (obj))
    return eventFilterWindow(obj, event);
  return QMainWindow::eventFilter(obj, event);
}

void QTMMainTabWindow::showWidget(QWidget *widget, bool isDocument) {
  if (ads::CDockWidget* dock= adsDockWidgetFor (widget)) {
    if (dock->dockManager () != mDockManager) {
      auto* owner= qobject_cast<QTMMainTabWindow*> (dock->dockManager ()->parentWidget ());
      if (owner) {
        owner->showWidget (widget, isDocument);
        return;
      }
    }
  }
  if (isDocument) restoreAdsLayoutState ();
  QPointer<ads::CDockAreaWidget> documentArea;
  if (isDocument) {
    if (ads::CDockWidget* current= adsDockWidgetFor (currentDocumentWidget ()))
      documentArea= current->dockAreaWidget ();
    if (documentArea == nullptr)
      for (QWidget* document: documentWidgets ())
        if (ads::CDockWidget* dock= adsDockWidgetFor (document)) {
          documentArea= dock->dockAreaWidget ();
          if (documentArea != nullptr) break;
        }
  }
  if (widget != nullptr)
    widget->setProperty (kAthenaDocumentWidgetProperty, isDocument);
  if (isDocument) mLastFocusedDocumentWidget= widget;
  mLastFocusedWorkPaneWidget= widget;
  if (isDocument) widget->installEventFilter(this);
  if (isDocument) buffer_switcher_note_widget (widget);
  ads::CDockWidget* dockWidget = adsDockWidgetFor(widget);
  if (dockWidget) {
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
    dockWidget->setIcon (widget->windowIcon ());
    connect (widget, &QWidget::windowIconChanged,
             dockWidget, &ads::CDockWidget::setIcon);

    // Let the TeXmacs close sequence decide whether a document may close.
    dockWidget->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
    dockWidget->setFeature(ads::CDockWidget::CustomCloseHandling, true);
    connect(dockWidget, &ads::CDockWidget::closeRequested, [widget]() {
      if (widget->metaObject()->indexOfSignal("closed()") != -1)
        QMetaObject::invokeMethod(widget, "closed");
    });

    if (gNextWidgetFloating) {
#if ATHENA_PLATFORM_IPADOS
      mDockManager->addDockWidget (ads::CenterDockWidgetArea, dockWidget);
      athena_ios_new_scene (this, dockWidget);
#else
      mDockManager->addDockWidgetFloating(dockWidget);
#endif
      gNextWidgetFloating = false;
    } else if (documentArea != nullptr)
      mDockManager->addDockWidgetTabToArea (dockWidget, documentArea);
    else
      mDockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget);

    mDockManager->setDockWidgetFocused (dockWidget);
    if (QWidget* focusTarget= documentFocusTarget(widget))
      focusTarget->setFocus(Qt::OtherFocusReason);
    setMainTitleFromWidget(widget);
  } else {
    widget->show();
    widget->raise();
    widget->activateWindow();
    widget->setFocus();
  }
  showAfterContentReady(widget);
}

void
QTMMainTabWindow::showShell() {
  showAfterContentReady(nullptr);
}

QList<QWidget*>
QTMMainTabWindow::documentWidgets() const {
  QList<QWidget*> out;
  auto map= mDockManager->dockWidgetsMap();
  for (auto it= map.begin (); it != map.end (); ++it) {
    ads::CDockWidget* dockWidget= it.value ();
    if (dockWidget == nullptr) continue;
    QWidget* widget= dockWidget->widget ();
    if (isDocumentWidget (widget) && !out.contains (widget))
      out.append (widget);
  }
  return out;
}

bool
QTMMainTabWindow::hasOpenAdsPanes() const {
  if (mDockManager == nullptr) return false;

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
QTMMainTabWindow::activeWorkPaneWidget(QWidget* window) const {
  if (mDockManager == nullptr) return nullptr;
  ads::CDockWidget* dock= mDockManager->focusedDockWidget ();
  if (window != nullptr) {
    if (dock == nullptr || dock->window () != window || dock->isClosed ())
      dock= adsDockWidgetFor (mWindowWorkPanes.value (window));
    if (dock != nullptr && dock->window () == window && !dock->isClosed ())
      return dock->widget ();
    // A newly created or restored window may not have received focus yet.
    for (auto* candidate: mDockManager->dockWidgetsMap ())
      if (candidate->window () == window && !candidate->isClosed () &&
          candidate->dockAreaWidget () &&
          candidate->dockAreaWidget ()->currentDockWidget () == candidate)
        return candidate->widget ();
    return nullptr;
  }
  // Menus and modal dialogs can temporarily take focus away from ADS.
  if (dock == nullptr) dock= adsDockWidgetFor (mLastFocusedWorkPaneWidget);
  if (dock == nullptr || dock->isClosed ()) return nullptr;
  return dock->widget ();
}

QWidget*
QTMMainTabWindow::lastActiveDocumentWidget(QWidget* window) const {
  if (window != nullptr) {
    QWidget* document= mWindowDocuments.value (window);
    if (isDocumentWidget (document) && document->window () == window &&
        documentWidgets ().contains (document)) return document;
    document= activeWorkPaneWidget (window);
    if (isDocumentWidget (document)) return document;
    for (auto* candidate: documentWidgets ())
      if (candidate->window () == window) return candidate;
    return nullptr;
  }
  QWidget* document= mLastFocusedDocumentWidget.data ();
  if (!isDocumentWidget (document)) return nullptr;
  return documentWidgets ().contains (document) ? document : nullptr;
}

QWidget*
QTMMainTabWindow::currentDocumentWidget() const {
  QWidget* current= nullptr;
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

  if (isDocumentWidget (current) && documentWidgets ().contains (current))
    return current;
  return nullptr;
}

QString
QTMMainTabWindow::documentWidgetTitle(QWidget* widget) const {
  if (widget == nullptr) return QString ();

  if (ads::CDockWidget* dockWidget= adsDockWidgetFor(widget))
    return dockWidget->windowTitle ();

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

void QTMMainTabWindow::removeWidget(QWidget *widget) {
  QWidget* p = widget->parentWidget();
  while (p) {
    if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
      dockWidget->dockManager()->removeDockWidget(dockWidget);
      dockWidget->deleteLater();
      break;
    }
    p = p->parentWidget();
  }
}

void QTMMainTabWindow::detachWidget(QWidget* widget) {
  QWidget* p = widget->parentWidget();
  while (p) {
    if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
#if ATHENA_PLATFORM_IPADOS
      athena_ios_new_scene (this, dockWidget);
#else
      mDockManager->addDockWidgetFloating(dockWidget);
#endif
      break;
    }
    p = p->parentWidget();
  }
}

void QTMMainTabWindow::prepareNativeMenus () {
  if (mNativeMenuPresenter) mNativeMenuPresenter->prepareNativeMenus ();
}

void QTMMainTabWindow::adoptPane (ads::CDockWidget* pane) {
  if (!pane || pane->dockManager () == mDockManager) return;
  QWidget* widget= pane->widget ();
  QTMMainTabWindow* previous= qobject_cast<QTMMainTabWindow*> (pane->dockManager ()->parentWidget ());
  if (previous && widget) {
    widget->removeEventFilter (previous);
    if (previous->mLastFocusedDocumentWidget == widget)
      previous->mLastFocusedDocumentWidget= nullptr;
    if (previous->mLastFocusedWorkPaneWidget == widget)
      previous->mLastFocusedWorkPaneWidget= nullptr;
  }
  pane->dockManager ()->removeDockWidget (pane);
  mDockManager->addDockWidget (ads::CenterDockWidgetArea, pane);
  if (isDocumentWidget (widget)) {
    widget->installEventFilter (this);
    mLastFocusedDocumentWidget= widget;
  }
  mLastFocusedWorkPaneWidget= widget;
  pane->toggleView (true);
  mDockManager->setDockWidgetFocused (pane);
}

void QTMMainTabWindow::tabTitleChanged(QWidget *widget, QString title) {
  QWidget* p = widget->parentWidget();
  while (p) {
    if (ads::CDockWidget* dockWidget = qobject_cast<ads::CDockWidget*>(p)) {
      // Nested source windows do not own the containing compound-document tab.
      if (dockWidget->widget () != widget) return;
      dockWidget->setWindowTitle(title);
      if (widgetOrChildHasFocus(widget)) setMainTitle(title);
      break;
    }
    p = p->parentWidget();
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
  QFile file (QDir (to_qstring (get_env ("ATHENA_PATH"))).filePath (
    "misc/themes/ads.css"));
  if (!file.open (QIODevice::ReadOnly)) {
    std_warning << "Could not load ATHENA ADS theme: "
                << from_qstring (file.errorString ()) << LF;
    return;
  }
  const QString styleSheet=
    mAdsBaseStyleSheet + '\n' + QString::fromUtf8 (file.readAll ());
  applyAdsTheme (mDockManager, styleSheet);
  for (auto* floating: mDockManager->floatingWidgets ())
    applyAdsTheme (floating, styleSheet);
}
