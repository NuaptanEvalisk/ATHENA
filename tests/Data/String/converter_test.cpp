/******************************************************************************
* MODULE     : converter_test.cpp
* DESCRIPTION: Properties of characters and strings
* COPYRIGHT  : (C) 2019 Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "converter.hpp"
#include "unicode_ranges.hpp"
#include <future>
#include <thread>

bool headless_mode= true;
bool is_headless () { return true; }

class TestConverter: public QObject {
  Q_OBJECT

private slots:
  void test_utf8_to_cork();
  void test_finite_part_integral();
  void test_unicode_17_cjk_ranges();
  void test_thread_local_converters();
};

void TestConverter::test_utf8_to_cork() {
  QCOMPARE (as_charp (utf8_to_cork ("中")), "<#4E2D>");
  QCOMPARE (as_charp (utf8_to_cork ("“")), "\x10");
  QCOMPARE (as_charp (utf8_to_cork("”")), "\x11");
}

void TestConverter::test_finite_part_integral() {
  QCOMPARE (as_charp (strict_cork_to_utf8 ("<fint>")), "\xE2\xA8\x8D");
  QCOMPARE (as_charp (strict_cork_to_utf8 ("<big-fint-1>")),
            "\xE2\xA8\x8D");
}

void TestConverter::test_unicode_17_cjk_ranges() {
  QCOMPARE (as_charp (utf8_to_cork ("\xF0\xA0\x80\x80")), "<#20000>");
  QVERIFY (unicode_is_cjk_ideograph (0x20000));
  QVERIFY (unicode_is_cjk_ideograph (0x2EBF0));
  QVERIFY (unicode_is_cjk_ideograph (0x31350));
  QVERIFY (unicode_is_cjk_ideograph (0x323B0));
  QVERIFY (unicode_is_cjk_ideograph (0x3347F));
  QVERIFY (!unicode_is_cjk_ideograph (0x33480));
}

void TestConverter::test_thread_local_converters() {
  std::promise<void> ready[2];
  auto first= ready[0].get_future ();
  auto second= ready[1].get_future ();
  bool correct[2]= {true, true};
  converter_rep* instances[2]= {nullptr, nullptr};
  auto run= [&] (int id, std::future<void>& other) {
    string input= id == 0 ? "\xE4\xB8\xAD" : "\xE2\x80\x9C";
    string expected= id == 0 ? "<#4E2D>" : "\x10";
    (void) utf8_to_cork (input);
    instances[id]= converter ("UTF-8-Cork").rep;
    ready[id].set_value ();
    other.wait ();
    for (int i= 0; i < 64; ++i)
      if (utf8_to_cork (input) != expected) correct[id]= false;
  };
  std::thread a (run, 0, std::ref (second));
  std::thread b (run, 1, std::ref (first));
  a.join ();
  b.join ();
  QVERIFY (instances[0] != instances[1]);
  QVERIFY (correct[0] && correct[1]);
}

QTEST_APPLESS_MAIN(TestConverter)
#include "converter_test.moc"
