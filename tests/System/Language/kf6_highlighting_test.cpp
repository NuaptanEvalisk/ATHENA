/******************************************************************************
* MODULE     : kf6_highlighting_test.cpp
* DESCRIPTION: Structured code highlighting and owner-local state regression
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QCoreApplication>
#include <iostream>
#include <thread>
#include "language.hpp"
#include "drd_std.hpp"
#include "modification.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static bool failed (int line) {
  std::cerr << "Highlighting assertion failed at line " << line << "\n";
  return false;
}

static bool check () {
  font_domain owner;
  font_domain_binding binding (owner);
  language cpp= prog_language ("cpp");
  tree document (DOCUMENT, "/* open", "int x;", "*/ int y;");
  cpp->highlight (document);
  string comment= cpp->get_color (document[0], 0, 2);
  if (comment == "" || cpp->get_color (document[1], 0, 3) != comment)
    return failed (__LINE__);
  string keyword= cpp->get_color (document[2], 3, 6);
  if (keyword == "" || keyword == comment) return failed (__LINE__);
  apply (document, mod_assign (path (0), tree ("// closed")));
  cpp->highlight (document);
  if (cpp->get_color (document[1], 0, 3) != keyword) return failed (__LINE__);

  tree split (CONCAT, "in", "t value;");
  cpp->highlight (split);
  if (cpp->get_color (split[0], 0, 2) != keyword ||
      cpp->get_color (split[1], 0, 1) != keyword) return failed (__LINE__);
  tree unicode ("\"<#1F600>\" int value;");
  cpp->highlight (unicode);
  if (cpp->get_color (unicode, 11, 14) != keyword) return failed (__LINE__);

  language python= prog_language ("python");
  tree py (DOCUMENT, "\"\"\"open", "still a string", "\"\"\"", "return 42");
  python->highlight (py);
  if (python->get_color (py[1], 0, 5) == "" ||
      python->get_color (py[3], 0, 6) == "") return failed (__LINE__);
  language scheme= prog_language ("scheme");
  tree scm ("(define x \"text\") ; comment");
  scheme->highlight (scm);
  if (scheme->get_color (scm, 19, 20) == "") return failed (__LINE__);
  language plain= prog_language ("verbatim");
  if (plain->hl_lan != 0) return failed (__LINE__);
  return true;
}

int main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  init_std_drd ();
  bool a= false, b= false;
  std::thread first ([&] { a= check (); });
  std::thread second ([&] { b= check (); });
  first.join (); second.join ();
  if (!a || !b) {
    std::cerr << "KF6 highlighting regression failed\n";
    return 1;
  }
  std::cout << "KF6 multiline, edits, structured spans, Unicode and owners passed\n";
  return 0;
}
