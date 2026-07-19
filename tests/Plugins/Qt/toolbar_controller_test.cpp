/******************************************************************************
* MODULE     : toolbar_controller_test.cpp
* DESCRIPTION: Tests for main window toolbar layout and reveal behavior
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>

#include "Qt/QTMToolbarController.hpp"
#include "boot.hpp"

#include <QMainWindow>
#include <QToolBar>
#include <QWidget>

class TestToolbarController: public QObject {
  Q_OBJECT

private slots:
  void mergesWhenWidthAllows ();
  void wrapsWhenWidthIsInsufficient ();
  void collapsesToRevealBar ();
  void overlayKeepsViewportGeometryStable ();
  void disablingAutoHideRestoresNativeToolbars ();
  void reloadsAutoHidePreference ();
};

static void
add_actions (QToolBar* toolbar, int count) {
  for (int i=0; i<count; i++)
    toolbar->addAction (QStringLiteral ("Action %1").arg (i));
}

static QTMToolbarController*
make_controller (QMainWindow& window, QToolBar*& main, QToolBar*& mode,
                 QToolBar*& focus, QToolBar*& user) {
  if (window.centralWidget () == nullptr)
    window.setCentralWidget (new QWidget (&window));
  main= window.addToolBar ("main");
  window.addToolBarBreak ();
  mode= window.addToolBar ("mode");
  window.addToolBarBreak ();
  focus= window.addToolBar ("focus");
  window.addToolBarBreak ();
  user= window.addToolBar ("user");
  add_actions (main, 4);
  add_actions (mode, 3);
  return new QTMToolbarController (&window, main, mode, focus, user);
}

void
TestToolbarController::mergesWhenWidthAllows () {
  QMainWindow window;
  QToolBar *main, *mode, *focus, *user;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus, user);
  window.resize (1600, 600);
  controller->setAutoHideEnabled (false);
  controller->setRequestedVisibility (true, true, true, false);
  controller->refreshLayout ();
  QVERIFY (controller->isModeMerged ());
}

void
TestToolbarController::wrapsWhenWidthIsInsufficient () {
  QMainWindow window;
  QToolBar *main, *mode, *focus, *user;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus, user);
  window.resize (180, 600);
  controller->setAutoHideEnabled (false);
  controller->setRequestedVisibility (true, true, true, false);
  controller->refreshLayout ();
  QVERIFY (!controller->isModeMerged ());
}

void
TestToolbarController::collapsesToRevealBar () {
  QMainWindow window;
  QToolBar *main, *mode, *focus, *user;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus, user);
  controller->setRequestedVisibility (true, true, true, false);
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
TestToolbarController::overlayKeepsViewportGeometryStable () {
  QMainWindow window;
  QToolBar *main, *mode, *focus, *user;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus, user);
  window.resize (1000, 600);
  window.show ();
  controller->setRequestedVisibility (true, true, true, false);
  controller->setAutoHideEnabled (true);
  QCoreApplication::processEvents ();

  QRect collapsedGeometry= window.centralWidget ()->geometry ();
  controller->expand ();
  QCoreApplication::processEvents ();
  QCOMPARE (window.centralWidget ()->geometry (), collapsedGeometry);
  QCOMPARE (controller->overlayWidget ()->parentWidget (),
            window.centralWidget ());
  QCOMPARE (controller->overlayWidget ()->pos (), QPoint (0, 0));
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
  QToolBar *main, *mode, *focus, *user;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus, user);
  controller->setRequestedVisibility (true, true, true, false);
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
  QToolBar *main, *mode, *focus, *user;
  QTMToolbarController* controller=
    make_controller (window, main, mode, focus, user);

  set_user_preference ("hide toolbars when not using them", "on");
  controller->setRequestedVisibility (true, true, true, false);
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

QTEST_MAIN(TestToolbarController)
#include "toolbar_controller_test.moc"
