
/******************************************************************************
* MODULE     : list_test.cpp
* DESCRIPTION: test on linked lists with reference counting
* COPYRIGHT  : (C) 2018 Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include "list.hpp"

auto the_nil_list = list<string>();
auto the_atom_list = list<long>(1);
auto normal = list<long>(1, 2, 3, list<long>());

class TestList: public QObject {
  Q_OBJECT

private slots:
  void operate_on_the_last ();
  void test_copy ();
  void test_append ();
  void head_and_tail ();
  void test_reverse ();
  void test_remove ();
};

/******************************************************************************
* tests on insertion and suppression
******************************************************************************/

void
TestList::operate_on_the_last () {
  auto the_atom_list_copy = copy(the_atom_list);
  access_last(the_atom_list_copy) = 2L;
  QCOMPARE (the_atom_list_copy, list<long>(2L));
  suppress_last (the_atom_list_copy);
  QCOMPARE (the_atom_list_copy, list<long>());
  QCOMPARE (last_item(the_atom_list), 1);

  auto normal_copy = copy(normal);
  access_last (normal_copy) = 4;
  QCOMPARE (normal_copy, list<long>(1, 2, 4, list<long>()));
  suppress_last (normal_copy);
  QCOMPARE (normal_copy, list<long>(1, 2, list<long>()));
  QCOMPARE (last_item(normal), 3);
}

/******************************************************************************
* tests on computations with list<T> structures
******************************************************************************/

void
TestList::test_copy () {
  QCOMPARE (copy(the_nil_list), the_nil_list);
  QCOMPARE (copy(the_atom_list), the_atom_list);
  QCOMPARE (copy(normal), normal);
}

void
TestList::test_append() {
  auto appended = the_nil_list * string("a");
  QCOMPARE (appended, list<string>(string("a")));

  auto to_append = list<string>("a");
  QCOMPARE (the_nil_list * to_append, to_append);

  auto nil_to_append = list<string>();
  QCOMPARE (the_nil_list * nil_to_append, nil_to_append);

  QCOMPARE (the_atom_list * list<long>(), the_atom_list);

  QCOMPARE (list<long>(1L, 2L, list<long>()) * list<long>(3L), normal);
  QCOMPARE (list<long>(1L) * list<long>(2L, 3L, list<long>()), normal);
}

void
TestList::head_and_tail() {
  QCOMPARE (head(the_atom_list), the_atom_list);
  QCOMPARE (tail(the_atom_list), list<long>());

  QCOMPARE (head(normal), list<long>(1));
  QCOMPARE (tail(normal), list<long>(2, 3, list<long>()));
}

void
TestList::test_reverse () {
  QCOMPARE (reverse (the_nil_list), the_nil_list);
  QCOMPARE (reverse (the_atom_list), the_atom_list);
  QCOMPARE (reverse (normal), list<long>(3, 2, 1, list<long>()));
}

void
TestList::test_remove () {
  QCOMPARE (remove (the_nil_list, string("a")), the_nil_list);
  QCOMPARE (remove (the_atom_list, 1L), list<long>());
  QCOMPARE (remove (normal, 2L), list<long>(1, 3, list<long>()));
}

QTEST_MAIN(TestList)
#include "list_test.moc"
