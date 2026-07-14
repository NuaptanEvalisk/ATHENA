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

class TestToolbarController: public QObject {
  Q_OBJECT

private slots:
  void mergesWhenWidthAllows ();
  void wrapsWhenWidthIsInsufficient ();
  void collapsesToRevealBar ();
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
  QVERIFY (controller->revealToolbar ()->isHidden ());
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
