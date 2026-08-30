/******************************************************************************
* MODULE     : scrollable_menu_test.cpp
* DESCRIPTION: Tests for scrollable Scheme menus in the Qt action bridge
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include <QtTest/QtTest>

#include "QTMMenuHelper.hpp"
#include "QTMStyle.hpp"
#include "qt_widget.hpp"

#include <QAction>
#include <QMenu>

bool headless_mode= false;
bool is_headless () { return false; }

namespace {

class TestActionListRep: public qt_widget_rep {
public:
  TestActionListRep (): qt_widget_rep (vertical_list) {}

  QList<QAction*>* get_fresh_qactionlist () override {
    QList<QAction*>* actions= new QList<QAction*> ();
    actions->append (new QAction ("First"));
    actions->append (new QAction ("Second"));
    return actions;
  }
};

class TestWidgetPromiseRep: public promise_rep<widget> {
public:
  explicit TestWidgetPromiseRep (widget value): value (value) {}
  widget eval () override { return value; }

private:
  widget value;
};

widget
scrollable_action_menu () {
  widget inner= abstract (tm_new<TestActionListRep> ());
  widget scrollable= user_canvas_widget (inner, 0);
  array<widget> root_entries (1);
  root_entries[0]= scrollable;
  return vertical_menu (root_entries);
}

} // namespace

class TestScrollableMenu: public QObject {
  Q_OBJECT

private slots:
  void exposesInnerActions ();
  void forcesScrollableLazyMenu ();
  void requestsNativeMenuScrolling ();
};

void
TestScrollableMenu::exposesInnerActions () {
  widget root= scrollable_action_menu ();

  QVERIFY (concrete (root)->requires_menu_scrolling ());
  QList<QAction*>* actions= concrete (root)->get_qactionlist ();
  QVERIFY (actions != nullptr);
  QCOMPARE (actions->size (), 2);
  QCOMPARE (actions->at (0)->text (), QString ("First"));
  QCOMPARE (actions->at (1)->text (), QString ("Second"));
}

void
TestScrollableMenu::forcesScrollableLazyMenu () {
  promise<widget> source=
    tm_new<TestWidgetPromiseRep> (scrollable_action_menu ());
  QTMLazyMenu menu (source);
  menu.force ();

  QCOMPARE (menu.actions ().size (), 2);
  QVERIFY (menu.property (QTM_SCROLLABLE_MENU_PROPERTY).toBool ());
}

void
TestScrollableMenu::requestsNativeMenuScrolling () {
  QMenu menu;
  menu.setProperty (QTM_SCROLLABLE_MENU_PROPERTY, true);
  QCOMPARE (qtmstyle ()->styleHint (
              QStyle::SH_Menu_Scrollable, nullptr, &menu), 1);
}

QTEST_MAIN (TestScrollableMenu)
#include "scrollable_menu_test.moc"
