/******************************************************************************
* MODULE     : hunspell_test.cpp
* DESCRIPTION: System dictionaries, personal words and bounded traversal
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include <thread>
#include "Ispell/ispell.hpp"
#include "tree_spell.hpp"
#include "drd_std.hpp"
#include "language.hpp"
#include "convert.hpp"
#include "unicode_text.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

#define CHECK(x) do { if (!(x)) { \
  std::cerr << "Spell assertion failed at " << __LINE__ << "\n"; return 1; \
} } while (false)

int main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  QTemporaryDir home;
  CHECK (home.isValid ());
  qputenv ("HOME", home.path ().toUtf8 ());
  qputenv ("ATHENA_HOME_PATH", home.path ().toUtf8 ());
  qputenv ("DICPATH", home.path ().toUtf8 ());
  QFile aff (home.path () + "/fr_FR.aff");
  CHECK (aff.open (QIODevice::WriteOnly));
  aff.write ("SET ISO8859-1\n");
  aff.close ();
  QFile dic (home.path () + "/fr_FR.dic");
  CHECK (dic.open (QIODevice::WriteOnly));
  dic.write ("2\n\xe9" "cole\n?\n");
  dic.close ();
  init_std_drd ();
  CHECK (ispell_start ("french") == "ok");
  CHECK (ispell_test ("french", u8"\u00e9cole"));
  tree suggestions= ispell_check ("french", "ecole");
  CHECK (is_tuple (suggestions) && as_int (suggestions[0]) == N(suggestions) - 1);
  bool accent= false;
  for (int i= 1; i < N(suggestions); ++i) {
    const string value= suggestions[i]->label;
    CHECK (athena::text::valid_utf8 ({value.data (), std::size_t (N(value))}));
    accent |= value == u8"\u00e9cole";
  }
  CHECK (accent);
  CHECK (!ispell_test ("french", u8"\u4e2d")); // Must not become the accepted '?'.
  CHECK (!ispell_test ("french", string ("?\0junk", 6)));
  ispell_insert ("french", u8"\u4e2d e\u0301");
  CHECK (ispell_test ("french", u8"\u4e2d e\u0301"));
  ispell_done ("french");
  QFile french_personal (home.path () + "/system/spelling/fr_FR.txt");
  CHECK (french_personal.open (QIODevice::ReadOnly));
  CHECK (french_personal.readAll ().contains (u8"\u4e2d e\u0301\n"));
  CHECK (ispell_start ("english") == "ok");
  CHECK (ispell_test ("english", "hello"));
  CHECK (!ispell_test ("english", "zzqqxxzz"));
  CHECK (is_tuple (ispell_check ("english", "helo")));
  ispell_accept ("english", "zzqqxxzz");
  CHECK (ispell_test ("english", "zzqqxxzz"));
  bool isolated= false;
  std::thread other ([&] { isolated= !ispell_test ("english", "zzqqxxzz"); });
  other.join ();
  CHECK (isolated);
  ispell_done ("english");
  CHECK (!ispell_test ("english", "zzqqxxzz"));
  CHECK (!check_word ("english", "zzqqxxzz"));
  std::thread writer ([] {
    ispell_insert ("english", "zzqqxxzz");
    ispell_done ("english");
  });
  writer.join ();
  CHECK (check_word ("english", "zzqqxxzz"));
  QFile personal (home.path () + "/system/spelling/en_US.txt");
  CHECK (personal.open (QIODevice::ReadOnly));
  CHECK (personal.readAll ().contains ("zzqqxxzz\n"));

  tree doc (DOCUMENT);
  for (int i=0; i<100; ++i) doc << tree ("hello qzxqzxqzx");
  incremental_spell scan;
  scan.start (doc, "text", "english", path (0), path (70, 0));
  scan.step (2, 16, 1000);
  CHECK (!scan.done ());
  range_set hits= scan.selections (path ());
  CHECK (N(hits) == 2 && hits[0] == path (0, 70, 6));
  int steps= 0;
  while (!scan.done () && ++steps < 1000) scan.step (4, 16, 1000);
  CHECK (scan.done () && steps > 1);
  hits= scan.selections (path ());
  CHECK (N(hits) == 200);
  for (int i=0; i<100; ++i) CHECK (hits[2*i] == path (0, i, 6));
  scan.reset ();
  CHECK (scan.done () && !scan.initialized);

  const string unicode= u8"\u00e9cole\u2003qzx\u00e9\u00a0e\u0301qq";
  scan.start (tree (unicode), "text", "french", path (0), path ());
  while (!scan.done ()) scan.step (1, 16, 1000);
  hits= scan.selections (path ());
  CHECK (N(hits) == 4);
  CHECK (hits[0] == path (0, 9) && hits[1] == path (0, 14));
  CHECK (hits[2] == path (0, 16) && hits[3] == path (0, N(unicode)));

  // Crossing a bounded ICU window must neither lose nor invent partial words.
  string boundary;
  for (int i= 0; i < 4093; ++i) boundary << ' ';
  boundary << unicode;
  scan.start (tree (boundary), "text", "french", path (0), path ());
  while (!scan.done ()) scan.step (1, 16, 1000);
  hits= scan.selections (path ());
  CHECK (N(hits) == 4);
  CHECK (hits[0] == path (0, 4102) && hits[1] == path (0, 4107));
  CHECK (hits[2] == path (0, 4109) && hits[3] == path (0, N(boundary)));

  // Many nodes and a very long atom must yield even with no misspellings.
  string large;
  for (int i=0; i<10000; ++i) large << 'a';
  scan.start (tree (large), "text", "english", path (0), path ());
  scan.step (64, 256, 1000);
  CHECK (!scan.done ());
  while (!scan.done ()) scan.step (64, 256, 1000);
  CHECK (N(scan.selections (path ())) == 0);
  large << " qzxqzxqzx";
  scan.start (tree (large), "text", "english", path (0), path ());
  while (!scan.done ()) scan.step (64, 256, 1000);
  hits= scan.selections (path ());
  CHECK (N(hits) == 2 && hits[0] == path (0, 10001));
  scan.start (tree ("qzxqzxqzx hello qzxqzxqzx"), "text", "english",
              path (0), path (16));
  scan.step (1, 16, 1000);
  hits= scan.selections (path ());
  CHECK (N(hits) == 2 && hits[0] == path (0, 16));
  while (!scan.done ()) scan.step (64, 256, 1000);
  CHECK (N(scan.selections (path ())) == 4);
  scan.start (doc, "math", "english", path (0), path (70, 0));
  while (!scan.done ()) scan.step (64, 256, 1000);
  CHECK (N(scan.selections (path ())) == 0);
  std::cout << "Hunspell and incremental spelling passed\n";
  return 0;
}
