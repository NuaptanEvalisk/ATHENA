#include <QtTest/QtTest>

#include "Edit/Interface/resize_viewport_restore.hpp"

class ResizeViewportRestoreTest: public QObject {
  Q_OBJECT

private slots:
  void preserves_cursor_distance_from_top ();
  void state_is_independent_per_view ();
  void cursor_navigation_invalidates_restore ();
  void event_time_snapshot_requires_matching_generations ();
};

void
ResizeViewportRestoreTest::preserves_cursor_distance_from_top () {
  resize_viewport_restore_state state;
  path cursor_path= path (1, path (2));
  state.arm (cursor_path, 700, 1000, 4, 7);

  QCOMPARE (state.cursor_distance_from_top, SI (300));
  SI target_center=
    resize_viewport_target_center_y (900, state.cursor_distance_from_top, 800);
  QCOMPARE (target_center, SI (800));
  QCOMPARE ((target_center + 400) - 900, SI (300));
}

void
ResizeViewportRestoreTest::state_is_independent_per_view () {
  resize_viewport_restore_state first;
  resize_viewport_restore_state second;
  path first_path= path (1);
  path second_path= path (2);

  first.arm (first_path, 100, 500, 1, 2);
  second.arm (second_path, 300, 600, 3, 4);
  first.cancel ();

  QVERIFY (!first.pending);
  QVERIFY (second.pending);
  QCOMPARE (second.cursor_distance_from_top, SI (300));
  QVERIFY (second.matches (second_path, 3, 4));
}

void
ResizeViewportRestoreTest::cursor_navigation_invalidates_restore () {
  resize_viewport_restore_state state;
  path old_path= path (1, path (2));
  path new_path= path (1, path (3));
  state.arm (old_path, 100, 400, 5, 8);

  QVERIFY (state.matches (old_path, 5, 8));
  QVERIFY (!state.matches (new_path, 5, 8));
  QVERIFY (!state.matches (old_path, 6, 8));
  QVERIFY (!state.matches (old_path, 5, 9));
}

void
ResizeViewportRestoreTest::event_time_snapshot_requires_matching_generations () {
  QVERIFY (resize_viewport_snapshot_current (4, 4, 7, 7));
  QVERIFY (!resize_viewport_snapshot_current (5, 4, 7, 7));
  QVERIFY (!resize_viewport_snapshot_current (4, 4, 8, 7));
}

QTEST_MAIN (ResizeViewportRestoreTest)
#include "resize_viewport_restore_test.moc"
