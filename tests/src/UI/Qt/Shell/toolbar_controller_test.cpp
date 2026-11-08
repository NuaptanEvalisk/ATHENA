/******************************************************************************
* MODULE     : toolbar_controller_test.cpp
* DESCRIPTION: Tests for main window toolbar layout and reveal behavior
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>

#include "UI/Qt/Shell/QTMToolbarController.hpp"
#include "UI/Qt/Shell/QTMMainTabWindow.hpp"
#include "UI/Qt/Shell/QTMApplication.hpp"
#include "UI/Qt/Commands/QTMCommandRegistry.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "gui.hpp"
#include "native_dispatch.hpp"
#include "App/Runtime/server.hpp"

#include <DockAreaWidget.h>
#include <DockWidgetTab.h>
#include <FloatingDockContainer.h>
#include <QLabel>
#include <QComboBox>
#include <QAbstractItemView>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

bool headless_mode= false;
bool is_headless () { return false; }

class TestToolbarController: public QObject {
  Q_OBJECT

private slots:
  void mergesWhenWidthAllows ();
  void wrapsWhenWidthIsInsufficient ();
  void collapsesToRevealBar ();
  void overlayReplacesRevealBarWithoutMovingViewport ();
  void disablingAutoHideRestoresNativeToolbars ();
  void reloadsAutoHidePreference ();
  void floatingWindowsRetainApplicationChrome ();
  void presentationSelectorHasSixDistinctIcons ();
  void presentationCustomizeOpensPaneWithoutChangingMode ();
  void documentMenuCommandsAppearInPalette ();
};

static void
add_actions (QToolBar* toolbar, int count) {
  for (int i=0; i<count; i++)
    toolbar->addAction (QStringLiteral ("Action %1").arg (i));
}

static QTMToolbarController*
make_controller (QMainWindow& window, QToolBar*& main, QToolBar*& mode,
                 QToolBar*& focus) {
  if (window.centralWidget () == nullptr)
    window.setCentralWidget (new QWidget (&window));
  main= window.addToolBar ("main");
  window.addToolBarBreak ();
  mode= window.addToolBar ("mode");
  window.addToolBarBreak ();
  focus= window.addToolBar ("focus");
  add_actions (main, 4);
  add_actions (mode, 3);
  return new QTMToolbarController (&window, main, mode, focus);
}

void
TestToolbarController::mergesWhenWidthAllows () {
  QMainWindow window;
  QToolBar *main, *mode, *focus;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus);
  window.resize (1600, 600);
  controller->setAutoHideEnabled (false);
  controller->setRequestedVisibility (true, true, true);
  controller->refreshLayout ();
  QVERIFY (controller->isModeMerged ());
}

void
TestToolbarController::wrapsWhenWidthIsInsufficient () {
  QMainWindow window;
  QToolBar *main, *mode, *focus;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus);
  window.resize (180, 600);
  controller->setAutoHideEnabled (false);
  controller->setRequestedVisibility (true, true, true);
  controller->refreshLayout ();
  QVERIFY (!controller->isModeMerged ());
}

void
TestToolbarController::collapsesToRevealBar () {
  QMainWindow window;
  QToolBar *main, *mode, *focus;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus);
  controller->setRequestedVisibility (true, true, true);
  controller->setAutoHideEnabled (true);
  QVERIFY (controller->isCollapsed ());
  QVERIFY (main->isHidden ());
  QVERIFY (mode->isHidden ());
  QVERIFY (focus->isHidden ());
  QVERIFY (!controller->revealToolbar ()->isHidden ());
  controller->expand ();
  QVERIFY (!controller->isCollapsed ());
  QVERIFY (!main->isHidden ());
  QVERIFY (!controller->revealToolbar ()->isHidden ());
  QVERIFY (!controller->overlayWidget ()->isHidden ());
}

void
TestToolbarController::overlayReplacesRevealBarWithoutMovingViewport () {
  QMainWindow window;
  QToolBar *main, *mode, *focus;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus);
  window.resize (1000, 600);
  window.show ();
  controller->setRequestedVisibility (true, true, true);
  controller->setAutoHideEnabled (true);
  QCoreApplication::processEvents ();

  QRect collapsedGeometry= window.centralWidget ()->geometry ();
  controller->expand ();
  QCoreApplication::processEvents ();
  QCOMPARE (window.centralWidget ()->geometry (), collapsedGeometry);
  QVERIFY (!controller->revealToolbar ()->isHidden ());
  QCOMPARE (controller->overlayWidget ()->parentWidget (),
            &window);
  QCOMPARE (controller->overlayWidget ()->pos (),
            controller->revealToolbar ()->mapTo (&window, QPoint (0, 0)));
  QCOMPARE (controller->overlayWidget ()->width (),
            controller->revealToolbar ()->width ());
  QVERIFY (controller->overlayWidget ()->height () > 0);
  QCOMPARE (main->x (), 0);
  QCOMPARE (mode->x (), main->width ());
  QVERIFY (mode->geometry ().right () < controller->overlayWidget ()->width ());
  QVERIFY (main->actions ().last ()->isSeparator ());
  QVERIFY (main->actions ().last ()->isVisible ());

  controller->collapse ();
  QCoreApplication::processEvents ();
  QCOMPARE (window.centralWidget ()->geometry (), collapsedGeometry);
}

void
TestToolbarController::disablingAutoHideRestoresNativeToolbars () {
  QMainWindow window;
  QToolBar *main, *mode, *focus;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus);
  controller->setRequestedVisibility (true, true, true);
  controller->setAutoHideEnabled (true);
  controller->expand ();
  QCOMPARE (main->parentWidget (), controller->overlayWidget ());

  controller->setAutoHideEnabled (false);
  QCOMPARE (main->parentWidget (), &window);
  QCOMPARE (mode->parentWidget (), &window);
  QVERIFY (controller->overlayWidget ()->isHidden ());
  QVERIFY (controller->revealToolbar ()->isHidden ());
  QVERIFY (!main->isHidden ());
}

void
TestToolbarController::reloadsAutoHidePreference () {
  const bool oldHadPreference=
    has_user_preference ("hide toolbars when not using them");
  const string oldPreference=
    get_user_preference ("hide toolbars when not using them", "off");
  set_user_preference ("hide toolbars when not using them", "off");

  QMainWindow window;
  QToolBar *main, *mode, *focus;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus);

  set_user_preference ("hide toolbars when not using them", "on");
  controller->setRequestedVisibility (true, true, true);
  QVERIFY (controller->isCollapsed ());
  QVERIFY (main->isHidden ());
  QVERIFY (mode->isHidden ());
  QVERIFY (focus->isHidden ());
  QVERIFY (!controller->revealToolbar ()->isHidden ());

  if (oldHadPreference)
    set_user_preference ("hide toolbars when not using them", oldPreference);
  else
    reset_user_preference ("hide toolbars when not using them");
}

void
TestToolbarController::presentationSelectorHasSixDistinctIcons () {
  QTMMainTabWindow shell;
  auto* modes= shell.findChild<QComboBox*> ("athenaPresentationMode");
  QVERIFY (modes != nullptr);
  QCOMPARE (modes->count (), 8);
  QVERIFY (shell.findChild<QToolButton*> ("athenaPresentationCustomize") == nullptr);
  const QStringList names {"Reflow", "Scroll", "Paper", "Slideshow", "Book", "Panorama"};
  const QStringList identifiers {"automatic", "papyrus", "paper", "beamer", "book", "panorama"};
  QVector<QImage> rendered;
  for (int i=0; i<6; ++i) {
    QCOMPARE (modes->itemText (i), names[i]);
    QCOMPARE (modes->itemData (i).toString (), identifiers[i]);
    QVERIFY2 (!modes->itemIcon (i).isNull (), qPrintable (names[i]));
    const QImage pixels= modes->itemIcon (i).pixmap (32, 32).toImage ();
    QVERIFY2 (!pixels.isNull (), qPrintable (names[i]));
    for (const auto& previous: rendered)
      QVERIFY2 (pixels != previous, qPrintable (names[i]));
    rendered.append (pixels);
  }
  const auto separator= modes->model ()->index (6, 0);
  QVERIFY (!(modes->model ()->flags (separator) & Qt::ItemIsSelectable));
  QCOMPARE (modes->itemText (7), QStringLiteral ("Customize..."));
  QVERIFY (!modes->itemIcon (7).isNull ());
  modes->setCurrentIndex (4);
  QCOMPARE (modes->currentIndex (), 4);
  // Selection temporarily lands on Customize until activation is delivered.
  modes->setCurrentIndex (7);
  QCOMPARE (modes->currentIndex (), 7);
  QVERIFY (QMetaObject::invokeMethod (modes, "activated", Q_ARG (int, 7)));
  QCOMPARE (modes->currentIndex (), 4);
  QCOMPARE (modes->currentData ().toString (), QStringLiteral ("book"));
  QCOMPARE (modes->iconSize (), QSize (16, 16));
  QCOMPARE (modes->minimumContentsLength (), 9);
}

void
TestToolbarController::presentationCustomizeOpensPaneWithoutChangingMode () {
  QTMMainTabWindow shell;
  shell.resize (1100, 700);
  shell.show ();
  auto* modes= shell.findChild<QComboBox*> ("athenaPresentationMode");
  QVERIFY (modes != nullptr);
  modes->setCurrentIndex (4);
  modes->showPopup ();
  QVERIFY (modes->view ()->isVisible ());
  const auto custom= modes->model ()->index (7, 0);
  const QPoint position= modes->view ()->visualRect (custom).center ();
  QVERIFY (modes->view ()->visualRect (custom).isValid ());
  QTest::mouseClick (modes->view ()->viewport (), Qt::LeftButton,
                     Qt::NoModifier, position);
  QTRY_VERIFY (shell.findChild<ads::CDockWidget*> (
    "athena-page-properties-pane") != nullptr);
  QCOMPARE (modes->currentData ().toString (), QStringLiteral ("book"));
}

void
TestToolbarController::documentMenuCommandsAppearInPalette () {
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QVERIFY (registry.initialize ());
  const auto* menu= registry.menu ("document");
  QVERIFY (menu != nullptr);
  QVERIFY (!menu->items.isEmpty ());
  QCOMPARE (menu->items[0].providerId, QStringLiteral ("document-menu"));
  QVERIFY (menu->items[0].showInPalette);
}

void
TestToolbarController::floatingWindowsRetainApplicationChrome () {
  {
    QTMMainTabWindow shell;
    auto makePane= [] (const QString& title) {
      auto* pane= new QLabel (title);
      pane->setFocusPolicy (Qt::StrongFocus);
      pane->setProperty ("athena-document-widget", true);
      auto* dock= new ads::CDockWidget (title);
      dock->setWidget (pane);
      return dock;
    };
    auto* first= makePane ("First");
    auto* second= makePane ("Second");
    auto* area= shell.dockManager ()->addDockWidget (
      ads::CenterDockWidgetArea, first);
    auto* floating= shell.dockManager ()->addDockWidgetFloating (second);
    // Exercise the parentless topology used by native Wayland windows.
    floating->setParent (nullptr, floating->windowFlags ());
    shell.show ();
    floating->show ();
    QCOMPARE (floating->styleSheet (), shell.dockManager ()->styleSheet ());
    QCOMPARE (floating->property ("athenaDarkMode"),
              shell.dockManager ()->property ("athenaDarkMode"));
    auto* chrome= floating->findChild<QMainWindow*> ();
    QVERIFY (chrome != nullptr);
    QVERIFY (!chrome->menuBar ()->actions ().isEmpty ());
    QCOMPARE (chrome->menuBar ()->actions ().size (),
              shell.menuBar ()->actions ().size ());
    QVERIFY (chrome->menuBar ()->isVisible ());
    QVERIFY (chrome->statusBar ()->isVisible ());
    QVERIFY (chrome->findChild<QLabel*> ("athenaStatusCenter") != nullptr);
    QCOMPARE (floating->dockContainer ()->floatingWidget (), floating);
    shell.dockManager ()->setDockWidgetFocused (second);
    QCOMPARE (shell.activeWorkPaneWidget (&shell), first->widget ());
    QCOMPARE (shell.activeWorkPaneWidget (floating), second->widget ());
    QCOMPARE (shell.lastActiveDocumentWidget (floating), second->widget ());
    shell.dockManager ()->setDockWidgetFocused (first);
    QVERIFY (!second->tabWidget ()->property ("focused").toBool ());
    for (auto* dock: {first, second}) {
      auto* tab= dock->tabWidget ();
      QVERIFY (tab->isActiveTab ());
      const QImage image= tab->grab ().toImage ();
      const QColor highlight= tab->palette ().color (QPalette::Highlight);
      int highlighted= 0;
      for (int y= 0; y < image.height (); ++y)
        for (int x= 0; x < image.width (); ++x)
          if (image.pixelColor (x, y) == highlight) ++highlighted;
      QVERIFY2 (highlighted > image.width () * image.height () / 4,
                qPrintable (dock->windowTitle ()));
    }
    shell.dockManager ()->addDockWidgetTabToArea (second, area);
    QCOMPARE (second->window (), static_cast<QWidget*> (&shell));
    QCOMPARE (shell.findChildren<QStatusBar*> (
      QString (), Qt::FindDirectChildrenOnly).size (), 1);
  }
}

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    TestToolbarController test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  QTMApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  init_athena ();
  run_tests (argc, argv);
  return 1;
}
#include "toolbar_controller_test.moc"
