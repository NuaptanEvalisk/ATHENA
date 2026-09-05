/******************************************************************************
* MODULE     : table_resize_test.cpp
* DESCRIPTION: Equation layout tables cannot be resized with the mouse
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QtTest/QtTest>
#include "table_resize.hpp"

class TableResizeTest: public QObject {
  Q_OBJECT
private slots:
  void protectsEquationLayout () {
    tree table (TABLE, tree (ROW, tree (CELL, "x")));
    QVERIFY (table_mouse_resize_allowed (tree (DOCUMENT, table), path (0)));
    for (const char* name: {"eqnarray", "eqnarray*"}) {
      tree doc (DOCUMENT, compound (name, tree (TFORMAT, table)));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 0)));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 0, 0)));
      tree nested (DOCUMENT, compound (name, tree (TFORMAT,
        tree (TABLE, tree (ROW, tree (CELL, table))))));
      QVERIFY (table_mouse_resize_allowed (nested, path (0, 0, 0) * 0 * 0 * 0));
    }
  }

  void protectsDocumentWrappedEquationLayout () {
    tree table (TABLE, tree (ROW, tree (CELL, "x")));
    for (const char* name: {"eqnarray", "eqnarray*"}) {
      tree format (TFORMAT,
        tree (CWITH, "1", "-1", "1", "1", "cell-width", "100px"), table);
      // Source shape observed in the live editor, including an existing resize.
      tree doc (TUPLE, tree (DOCUMENT, tree (""),
        compound (name, tree (DOCUMENT, format))));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 1, 0, 0)));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 1, 0, 0) * 1));

      tree nested (DOCUMENT, compound (name, tree (DOCUMENT,
        tree (TFORMAT, tree (TABLE, tree (ROW,
          tree (CELL, tree (DOCUMENT, tree (TFORMAT, table)))))))));
      QVERIFY (table_mouse_resize_allowed (nested,
        path (0, 0, 0) * 0 * 0 * 0 * 0 * 0));
    }
  }
};

QTEST_MAIN (TableResizeTest)
#include "table_resize_test.moc"
