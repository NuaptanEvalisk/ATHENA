/******************************************************************************
* MODULE     : current_format_floor_test.cpp
* DESCRIPTION: ATHENA TeXmacs-format compatibility floor regression
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "convert.hpp"
#include "drd_std.hpp"

#include <iostream>

static bool
contains_compound (tree t, string name) {
  if (is_compound (t, name)) return true;
  if (is_atomic (t)) return false;
  for (int i=0; i<N(t); i++)
    if (contains_compound (t[i], name)) return true;
  return false;
}

int
main () {
  init_std_drd ();

  tree current= texmacs_document_to_tree (
    "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\n"
    "current-format sentinel\n</body>\n");
  if (!is_document (current) ||
      extract (current, "body") == tree (DOCUMENT, "")) {
    std::cerr << "Current TeXmacs format did not parse\n";
    return 1;
  }

  tree below_floor= texmacs_document_to_tree (
    "<TeXmacs|2.1.2>\n\n<style|generic>\n\n<\\body>\n"
    "<mouse-over-balloon|tip|body>\n</body>\n");
  if (!is_document (below_floor) ||
      !contains_compound (below_floor, "mouse-over-balloon") ||
      contains_compound (below_floor, "hover-balloon")) {
    std::cerr << "Pre-2.1.3 current-syntax input was rejected or historically upgraded\n";
    return 1;
  }

  tree ancient= texmacs_document_to_tree ("TeXmacs(generic,(),body)");
  if (!is_func (ancient, _ERROR)) {
    std::cerr << "Ancient TeXmacs serialization unexpectedly parsed\n";
    return 1;
  }

  return 0;
}
