/******************************************************************************
* MODULE     : builtin_language_test.cpp
* DESCRIPTION: native built-in packrat language regressions
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include <QtTest/QtTest>

#include "packrat_grammar.hpp"
#include "convert.hpp"

class TestBuiltinLanguage: public QObject {
  Q_OBJECT

private slots:
  void test_std_symbol_properties ();
  void test_std_math_inheritance ();
  void test_minimal_language ();
  void test_definition_compatibility ();
};

void
TestBuiltinLanguage::test_std_symbol_properties () {
  packrat_grammar gr= find_packrat_grammar ("std-symbols");
  QCOMPARE (gr->get_property ("Relation-nolim-symbol", "type"), "infix");
  QCOMPARE (gr->get_property ("Relation-nolim-symbol", "right-penalty"), "20");
  QCOMPARE (gr->get_property ("Assign-symbol", "left-spacing"), "wide");
}

void
TestBuiltinLanguage::test_std_math_inheritance () {
  packrat_grammar gr= find_packrat_grammar ("std-math");
  QCOMPARE (gr->get_property ("Relation-nolim-symbol", "type"), "infix");
  QCOMPARE (gr->get_property ("Infix", "operator"), "true");
  QVERIFY (gr->grammar->contains (encode_symbol (compound ("symbol", "Main"))));
}

void
TestBuiltinLanguage::test_minimal_language () {
  packrat_grammar gr= find_packrat_grammar ("minimal");
  QCOMPARE (gr->get_property ("If-prefix", "highlight"), "keyword");
  QCOMPARE (gr->get_property ("Spc", "associativity"), "associative");
  QVERIFY (gr->grammar->contains (encode_symbol (compound ("symbol", "Main"))));
}

void
TestBuiltinLanguage::test_definition_compatibility () {
  scheme_tree definition=
    builtin_packrat_definition ("std-symbols", "Assign-symbol");
  string encoded= scheme_tree_to_string (definition);
  QVERIFY (occurs ("<assign>", encoded));
  QVERIFY (occurs ("<backassign>", encoded));
}

QTEST_MAIN(TestBuiltinLanguage)
#include "builtin_language_test.moc"
