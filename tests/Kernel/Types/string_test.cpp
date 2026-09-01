
/******************************************************************************
* MODULE     : string_test.cpp
* DESCRIPTION: Inline/COW byte string tests.
*              Zero characters are allowed in strings.
* COPYRIGHT  : (C) 2018-2021  Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include "string.hpp"
#include <cstdint>
#include <type_traits>

struct alignas(64) aligned_test_value {
  int value;
};

class TestString: public QObject {
  Q_OBJECT

private slots:
  void equality ();
  void compare ();
  void slice ();
  void concat ();
  void append ();
  void value_semantics ();
  void storage_boundaries ();
  void embedded_zero ();
  void aligned_array_allocation ();

  void test_as_bool ();
  void test_as_string_bool ();

  void test_is_empty ();
  void test_is_bool ();
  void test_is_int ();
  void test_is_quoted ();
};



/******************************************************************************
* Tests on Common routines for strings
******************************************************************************/

void
TestString::equality() {
  QCOMPARE (string("abc") == "abc", true);
  QCOMPARE (string("abc") == "", false);

  QCOMPARE (string("abc") != "abc", false);
  QCOMPARE (string("abc") != "", true);

  QCOMPARE (string("abc") == string("abc"), true);
  QCOMPARE (string("abc") == string(), false);
  QCOMPARE (string("abc") != string("abc"), false);
  QCOMPARE (string("abc") != string(), true);

  QCOMPARE (string() == string(), true);
}

void
TestString::compare () {
  QVERIFY (string("ab") < string("b"));
  QVERIFY (string() < string("0"));

  QVERIFY (string("a") <= string("a"));
  QVERIFY (string("ab") <= string("b"));
  QVERIFY (string() <= string());
  QVERIFY (string() <= string("0"));
}

void
TestString::slice () {
  QVERIFY (string("abcde")(0, 0) == string());
  QVERIFY (string("abcde")(0, 1) == string("a"));
  QVERIFY (string("abcde")(0, 10) == string("abcde"));
  QVERIFY (string("abcde")(-1, 1) == string("a"));
  QVERIFY (string("abcde")(3, 2) == string());
  QVERIFY (string("abcde")(3, -2) == string());
  QVERIFY (string("abcde")(10, 11) == string());
  QVERIFY (string("abcde")(-3, -2) == string());
}

void
TestString::concat () {
  QVERIFY (string("abc") * "de" == string("abcde"));
  QVERIFY (string("abc") * string("de") == string("abcde"));
  QVERIFY ("abc" * string("de") == string("abcde"));
}

/******************************************************************************
* Modifications
******************************************************************************/
void
TestString::append () {
  auto str = string();
  str << 'x';
  QVERIFY (str == string("x"));
  str << string("yz");
  QVERIFY (str == string("xyz"));
}

void
TestString::value_semantics () {
  string original ("abc");
  string copied= original;
  copied.set (0, 'x');
  copied << 'd';

  QVERIFY (original == string ("abc"));
  QVERIFY (copied == string ("xbcd"));
  static_assert (std::is_same_v<string::storage_type::allocator_type,
                                mi_stl_allocator<char>>);
}

void
TestString::storage_boundaries () {
  string inline_value ('a', 22);
  string inline_copy= inline_value;
  inline_copy.set (0, 'b');
  QVERIFY (inline_value[0] == 'a');

  inline_value << 'x';
  string shared_copy= inline_value;
  shared_copy.set (0, 'c');
  QVERIFY (inline_value[0] == 'a');
  QVERIFY (shared_copy[0] == 'c');

  shared_copy.resize (4);
  shared_copy << shared_copy;
  QVERIFY (shared_copy == string ("caaa" "caaa"));
}

void
TestString::embedded_zero () {
  const char bytes[]= {'a', '\0', 'b'};
  string value (bytes, 3);

  QCOMPARE (N(value), 3);
  QCOMPARE (value[1], '\0');
  QVERIFY (value != "a");
  QVERIFY (value(1, 3) == string (bytes + 1, 2));
}

void
TestString::aligned_array_allocation () {
  aligned_test_value* values= tm_new_array<aligned_test_value> (3);
  QCOMPARE (reinterpret_cast<uintptr_t> (values) % alignof (aligned_test_value),
            uintptr_t (0));
  QCOMPARE (values[0].value, 0);
  QCOMPARE (values[2].value, 0);
  tm_delete_array (values);
}

/******************************************************************************
* Conversions
******************************************************************************/
void
TestString::test_as_bool () {
  QCOMPARE (as_bool(string("true")), true);
  QCOMPARE (as_bool(string("#t")), true);
  QCOMPARE (as_bool(string("false")), false);

  // implicit conversion from char*
  QVERIFY (as_bool("true"));
  QVERIFY (as_bool("#t"));
  QVERIFY (!as_bool("false"));
}

void
TestString::test_as_string_bool () {
  QVERIFY (as_string_bool(true) == string("true"));
  QVERIFY (as_string_bool(false) == string("false"));
}


/******************************************************************************
* Predicates
******************************************************************************/
void
TestString::test_is_empty () {
  QVERIFY (is_empty (""));
  QVERIFY (!is_empty (" "));
  QVERIFY (!is_empty ("nonempty"));
}

void
TestString::test_is_bool () {
  QVERIFY (is_bool ("true"));
  QVERIFY (is_bool ("false"));
  QVERIFY (is_bool (string ("true")));
  QVERIFY (is_bool (string ("false")));

  QVERIFY (!is_bool ("100"));
  QVERIFY (!is_bool ("nil"));
}

void
TestString::test_is_int () {
  // Empty string is not an int
  QVERIFY (!is_int (""));

  // Only 0-9 in chars are int
  for (auto i= 0; i<256; i++) {
    char iter= (char) i;
    if (iter >= '0' && iter <= '9')
      QVERIFY (is_int (iter));
    else
      QVERIFY (!is_int (iter));
  }

  // Random tests
  QVERIFY (is_int ("-100"));
  QVERIFY (is_int ("+100"));
  QVERIFY (is_int ("100"));

  QVERIFY (!is_int(".0"));
  QVERIFY (!is_int("0x09"));
}

void
TestString::test_is_quoted () {
  QVERIFY (is_quoted ("\"\""));
  QVERIFY (is_quoted ("\"Hello TeXmacs\""));
  // is_quoted only checks if a string starts with a double quote
  // and ends with another double quote, regardless the validity
  // of the raw string
  QVERIFY (is_quoted ("\"Hello\"TeXmacs\""));

  QVERIFY (!is_quoted ("\""));
  QVERIFY (!is_quoted ("A"));
  QVERIFY (!is_quoted ("9"));
  QVERIFY (!is_quoted ("Hello TeXmacs"));
  QVERIFY (!is_quoted ("\"Hello TeXmac\"s"));
  QVERIFY (!is_quoted ("H\"ello TeXmacs\""));
}

QTEST_MAIN(TestString)
#include "string_test.moc"
