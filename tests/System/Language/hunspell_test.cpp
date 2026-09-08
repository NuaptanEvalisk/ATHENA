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
  dic.write ("1\n\xe9" "cole\n");
  dic.close ();
  init_std_drd ();
  CHECK (ispell_start ("french") == "ok");
  CHECK (ispell_test ("french", utf8_to_cork ("\xc3\xa9" "cole")));
  CHECK (is_tuple (ispell_check ("french", "ecole")));
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

  // Many nodes and a very long atom must yield even with no misspellings.
  string large;
  for (int i=0; i<10000; ++i) large << 'a';
  scan.start (tree (large), "text", "english", path (0), path ());
  scan.step (64, 256, 1000);
  CHECK (!scan.done ());
  while (!scan.done ()) scan.step (64, 256, 1000);
  CHECK (N(scan.selections (path ())) == 0);
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
