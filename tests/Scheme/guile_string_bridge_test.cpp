/******************************************************************************
* MODULE     : guile_string_bridge_test.cpp
* DESCRIPTION: Byte-compatible strings across the C++/Guile boundary
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "converter.hpp"
#include "scheme.hpp"
#include "Scheme/Guile/guile_tm.hpp"

class TestGuileStringBridge: public QObject {
  Q_OBJECT

private slots:
  void preserves_utf8_bytes ();
  void preserves_cork_bytes ();
  void exports_native_unicode_as_utf8 ();
  void preserves_accent_through_verbatim_conversion ();
  void lazyForceAcceptsOrdinaryGuileProcedures ();
};

void
TestGuileStringBridge::preserves_utf8_bytes () {
  string input ("\xC3\xA9", 2);
  QCOMPARE (tmscm_to_string (string_to_tmscm (input)), input);
}

void
TestGuileStringBridge::preserves_cork_bytes () {
  string input ("\xE9", 1);
  QCOMPARE (tmscm_to_string (string_to_tmscm (input)), input);
}

void
TestGuileStringBridge::exports_native_unicode_as_utf8 () {
  tmscm value= scm_from_utf8_string ("\xE4\xB8\xAD");
  QCOMPARE (tmscm_to_string (value), string ("\xE4\xB8\xAD", 3));
}

void
TestGuileStringBridge::preserves_accent_through_verbatim_conversion () {
  string clipboard ("\xC3\xA9", 2);
  string converter_input=
    tmscm_to_string (string_to_tmscm (clipboard));
  string converted= utf8_to_cork (converter_input);
  string inserted= tmscm_to_string (string_to_tmscm (converted));
  QCOMPARE (inserted, string ("\xE9", 1));
}

void
TestGuileStringBridge::lazyForceAcceptsOrdinaryGuileProcedures () {
  tmscm result= eval_scheme (
    "(catch #t "
    "  (lambda () "
    "    (lazy-define-force system) "
    "    (lazy-define-force (lambda () #t)) "
    "    #t) "
    "  (lambda args #f))");
  QVERIFY (scm_is_true (result));
}

static int test_status= 1;

static void
run_tests (int argc, char** argv) {
  TestGuileStringBridge test;
  test_status= QTest::qExec (&test, argc, argv);
  std::exit (test_status);
}

int
main (int argc, char** argv) {
  start_scheme (argc, argv, run_tests);
  return test_status;
}

#include "guile_string_bridge_test.moc"
