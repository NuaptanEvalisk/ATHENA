/******************************************************************************
* MODULE     : guile_string_bridge_test.cpp
* DESCRIPTION: UTF-8 text and explicit binary data across the C++/Guile boundary
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "convert.hpp"
#include "scheme.hpp"
#include "Scheme/Scheme/glue.hpp"
#include "Scheme/Guile/guile_tm.hpp"
#include "utf8_edit.hpp"
#include <stdexcept>

class TestGuileStringBridge: public QObject {
  Q_OBJECT

private slots:
  void preserves_utf8_bytes ();
  void rejects_cork_text ();
  void exports_native_unicode_as_utf8 ();
  void preserves_binary_bytes ();
  void raw_data_roundtrip ();
  void unicode_symbols ();
  void generated_binary_bindings ();
  void explicit_editor_positions ();
  void lazyForceAcceptsOrdinaryGuileProcedures ();
};

void
TestGuileStringBridge::preserves_utf8_bytes () {
  const char sample[]= "\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80" "e\xcc\x81\0<alpha>";
  string input (sample, sizeof (sample)-1);
  const auto value= string_to_tmscm (input);
  QCOMPARE (scm_c_string_length (value), size_t (13));
  QCOMPARE (SCM_CHAR (scm_c_string_ref (value, 0)), scm_t_wchar (0xe9));
  QCOMPARE (SCM_CHAR (scm_c_string_ref (value, 2)), scm_t_wchar (0x1f600));
  QCOMPARE (tmscm_to_string (value), input);
}

void
TestGuileStringBridge::rejects_cork_text () {
  QVERIFY_EXCEPTION_THROWN (string_to_tmscm (string ("\xe9", 1)), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN (symbol_to_tmscm (string ("\xff", 1)), std::invalid_argument);
}

void
TestGuileStringBridge::exports_native_unicode_as_utf8 () {
  tmscm value= scm_from_utf8_string ("\xE4\xB8\xAD");
  QCOMPARE (tmscm_to_string (value), string ("\xE4\xB8\xAD", 3));
  // Latin-1-range Unicode characters are not legacy single-byte text.
  QCOMPARE (tmscm_to_string (scm_from_utf8_string ("\xc3\xa9")), string ("\xc3\xa9"));
}

void
TestGuileStringBridge::preserves_binary_bytes () {
  string input (256);
  for (int i=0; i<256; ++i) input.set (i, static_cast<char> (i));
  const auto value= bytes_to_tmscm (input);
  QVERIFY (tmscm_is_bytes (value));
  QCOMPARE (tmscm_to_bytes (value), input);
  scm_c_bytevector_set_x (value, 0, 42);
  QCOMPARE (static_cast<unsigned char> (input[0]), static_cast<unsigned char> (0));
  QCOMPARE (N(tmscm_to_bytes (bytes_to_tmscm (""))), 0);
}

void
TestGuileStringBridge::raw_data_roundtrip () {
  const string payload ("\0\xff\xc3\xa9", 4);
  tree input (DOCUMENT, tree (RAW_DATA, payload), tree ("\xc3\xa9"));
  const auto value= scheme_tree_to_tmscm (tree_to_scheme_tree (input));
  const auto raw= tmscm_car (tmscm_cdr (value));
  QVERIFY (tmscm_is_bytes (tmscm_car (tmscm_cdr (raw))));
  scm_c_define ("bridge-test-content", value);
  QVERIFY (tmscm_to_tree (eval_scheme ("(tm->tree bridge-test-content)")) == input);
  QVERIFY (tmscm_to_content (value) == input);
  QVERIFY (scheme_tree_to_tree (tmscm_to_scheme_tree (value)) == input);
  QVERIFY (scm_is_true (eval_scheme (
    "(catch #t (lambda () (tm->tree '(raw-data \"not bytes\")) #f) "
    "(lambda args #t))")));
}

void
TestGuileStringBridge::unicode_symbols () {
  const string name= "\xe4\xb8\xad\xc3\xa9";
  QCOMPARE (tmscm_to_symbol (symbol_to_tmscm (name)), name);
  QCOMPARE (tmscm_to_symbol (scm_keyword_to_symbol (keyword_to_tmscm (name))), name);
  tree input (make_tree_label (name), tree (name));
  QVERIFY (scheme_tree_to_tree (tmscm_to_scheme_tree (
    scheme_tree_to_tmscm (tree_to_scheme_tree (input)))) == input);
}

void
TestGuileStringBridge::generated_binary_bindings () {
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  const auto filename= directory.filePath ("bytes.bin").toUtf8 ();
  scm_c_define ("bridge-test-file", string_to_tmscm (string (filename.constData (), filename.size ())));
  const auto value= eval_scheme (
    "(begin (bytes-save (decode-base64 \"AP8=\") bridge-test-file) "
    "(bytes-append-to-file (utf8-text->bytes \"A\") bridge-test-file) "
    "(encode-base64 (bytes-load bridge-test-file)))");
  QCOMPARE (tmscm_to_string (value), string ("AP9B"));
  QVERIFY (scm_is_true (eval_scheme (
    "(catch #t (lambda () (encode-base64 \"text\") #f) (lambda args #t))")));
  QCOMPARE (tmscm_to_string (eval_scheme (
    "(utf8-bytes->text (decode-base64 \"w6k=\"))")), string ("\xc3\xa9"));
}

void
TestGuileStringBridge::explicit_editor_positions () {
  const string text= "\xc3\xa9" "e\xcc\x81\xf0\x9f\x98\x80";
  scm_c_define ("bridge-test-text", string_to_tmscm (text));
  QCOMPARE (scm_to_int (eval_scheme ("(string-length bridge-test-text)")), 4);
  QCOMPARE (scm_to_int (eval_scheme ("(utf8-byte-length bridge-test-text)")), 9);
  QCOMPARE (scm_to_int (eval_scheme ("(tmstring-length bridge-test-text)")), 3);
  QCOMPARE (scm_to_int (eval_scheme ("(string-next bridge-test-text 2)")), 5);
  QCOMPARE (scm_to_int (eval_scheme ("(string-previous bridge-test-text 9)")), 5);
  QCOMPARE (scm_to_int (eval_scheme ("(utf8-byte->char-index bridge-test-text 5)")), 3);
  QCOMPARE (scm_to_int (eval_scheme ("(utf8-char->byte-index bridge-test-text 3)")), 5);
  QCOMPARE (tmscm_to_string (eval_scheme ("(utf8-byte-substring bridge-test-text 2 5)")),
            string ("e\xcc\x81"));
  QCOMPARE (tmscm_to_string (eval_scheme ("(tmstring-ref bridge-test-text 1)")),
            string ("e\xcc\x81"));
  QCOMPARE (tmscm_to_string (eval_scheme ("(tmstring-reverse-ref bridge-test-text 0)")),
            string ("\xf0\x9f\x98\x80"));
  QCOMPARE (tmscm_to_string (eval_scheme ("(list->tmstring (tmstring->list bridge-test-text))")), text);
  QCOMPARE (tmscm_to_string (eval_scheme ("(string->tmstring \"<alpha>\")")), string ("<alpha>"));
  QVERIFY_EXCEPTION_THROWN (utf8_byte_slice (text, 1, 2), std::invalid_argument);
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
  initialize_scheme ();
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
