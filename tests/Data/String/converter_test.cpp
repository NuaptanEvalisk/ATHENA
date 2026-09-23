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
#include "convert.hpp"
#include "scheme.hpp"
#include "universal.hpp"
#include "hyphenate.hpp"
#include "unicode_ranges.hpp"
#include <future>
#include <thread>

bool headless_mode= true;
bool is_headless () { return true; }

class TestConverter: public QObject {
  Q_OBJECT

private slots:
  void test_utf8_to_cork();
  void test_json_encoding_dictionaries();
  void test_universal_symbol_mappings();
  void test_json_latex_and_html_mappings();
  void test_finite_part_integral();
  void test_native_mathml_utf8();
  void test_named_symbol_latex_export();
  void test_native_unicode_case_and_accents();
  void test_utf8_hyphen_byte_offsets();
  void test_unicode_17_cjk_ranges();
  void test_thread_local_converters();
};

void TestConverter::test_utf8_to_cork() {
  QCOMPARE (as_charp (utf8_to_cork ("中")), "<#4E2D>");
  QCOMPARE (as_charp (utf8_to_cork ("“")), "\x10");
  QCOMPARE (as_charp (utf8_to_cork("”")), "\x11");
}

void TestConverter::test_json_encoding_dictionaries() {
  const std::pair<const char*, int> dictionaries[]= {
    {"HTMLlat1", 96}, {"HTMLspecial", 32}, {"HTMLsymbol", 124}, {"XML", 5},
    {"cork-escaped-to-ascii", 256}, {"cork-to-real-ascii", 2},
    {"cork-unicode-oneway", 4}, {"corktounicode", 253},
    {"symbol-unicode-fallback", 8}, {"symbol-unicode-math", 31},
    {"symbol-unicode-oneway", 222}, {"t2atounicode", 121},
    {"tmuniversaltounicode", 972}, {"unicode-cork-oneway", 4},
    {"unicode-symbol-oneway", 3}, {"utf8tolatex-back", 8},
    {"utf8tolatex-onedir", 5}, {"utf8tolatex", 480}
  };
  for (const auto& [name, expected_size]: dictionaries) {
    std::vector<std::pair<string,string>> mappings;
    QVERIFY2 (load_encoding_dictionary (name, mappings), name);
    QCOMPARE ((int) mappings.size (), expected_size);
  }
}

void TestConverter::test_universal_symbol_mappings() {
  QCOMPARE (as_charp (strict_cork_to_utf8 ("<warning-sign>")),
            "\xE2\x9A\xA0");
  QCOMPARE (as_charp (strict_cork_to_utf8 ("<mu>")), "\xCE\xBC");
  QCOMPARE (as_charp (utf8_to_cork ("\xC2\xB5")), "<mu>");
  QCOMPARE (as_charp (utf8_to_cork ("\xCE\xBC")), "<mu>");
  QCOMPARE (as_charp (utf8_to_cork ("\xE2\x80\xA6")), "<ldots>");
}

void TestConverter::test_json_latex_and_html_mappings() {
  QCOMPARE (as_charp (convert_utf8_to_LaTeX ("\xC2\xA3")),
            "{\\textsterling}");
  QCOMPARE (as_charp (convert_LaTeX_to_utf8 ("{\\textsterling}")),
            "\xC2\xA3");
  QCOMPARE (as_charp (utf8_to_html ("\xC2\xA9")), "&copy;");
  QCOMPARE (as_charp (html_to_utf8 ("&copy;")), "\xC2\xA9");
}

void TestConverter::test_finite_part_integral() {
  QCOMPARE (as_charp (strict_cork_to_utf8 ("<fint>")), "\xE2\xA8\x8D");
  QCOMPARE (as_charp (strict_cork_to_utf8 ("<big-fint-1>")),
            "\xE2\xA8\x8D");
}

void TestConverter::test_native_mathml_utf8() {
  scheme_tree mi (TUPLE, "mi", scm_quote ("中"));
  QCOMPARE (mathml_to_tree (mi), tree ("中"));

  scheme_tree sum (TUPLE, "mo", scm_quote ("&Sum;"));
  QCOMPARE (mathml_to_tree (sum), tree (BIG, "∑"));

  scheme_tree base (TUPLE, "mi", scm_quote ("x"));
  scheme_tree brace (TUPLE, "mo", scm_quote ("⏞"));
  tree wide= mathml_to_tree (scheme_tree (TUPLE, "mover", base, brace));
  QVERIFY (is_func (wide, WIDE, 2));
  QCOMPARE (wide[0], tree ("x"));
  QVERIFY (is_func (wide[1], WITH, 3));
  QCOMPARE (wide[1][0], tree ("math-accent-stretch"));
  QCOMPARE (wide[1][1], tree ("true"));
  QCOMPARE (wide[1][2], tree ("⏞"));
}

void TestConverter::test_named_symbol_latex_export() {
  scheme_tree expected= tree (TUPLE, "!symbol", tree (TUPLE, "backassign"));
  QCOMPARE (latex_export_named_symbol ("texmacs:backassign", true), expected);

  scheme_tree unsupported= latex_export_named_symbol ("not-an-athena-symbol", true);
  QVERIFY (is_tuple (unsupported, "nonconverted", 1));
  QCOMPARE (scm_unquote (unsupported[1]->label), string ("not-an-athena-symbol"));
}

void TestConverter::test_native_unicode_case_and_accents() {
  QCOMPARE (uni_locase_all ("ÉCOLE Σ"), string ("école σ"));
  QCOMPARE (uni_upcase_all ("école σ"), string ("ÉCOLE Σ"));
  QCOMPARE (uni_unaccent_char ("é"), string ("e"));
  QCOMPARE (uni_unaccent_char ("Ă"), string ("A"));
  QCOMPARE (uni_unaccent_char ("Ń"), string ("N"));
}

void TestConverter::test_utf8_hyphen_byte_offsets() {
  hashmap<string,string> patterns ("?");
  hashmap<string,string> explicit_hyphenations ("?");
  explicit_hyphenations ("école")= "é-cole";
  const string word= "école";
  array<int> penalty= get_hyphens (word, patterns, explicit_hyphenations);
  QCOMPARE (N(penalty), N(word)-1);
  QCOMPARE (penalty[0], HYPH_INVALID);
  QCOMPARE (penalty[1], HYPH_STD);
  string left, right;
  std_hyphenate (word, 1, left, right, penalty[1]);
  QCOMPARE (left, string ("é-"));
  QCOMPARE (right, string ("cole"));
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
