/******************************************************************************
* MODULE     : doc_info_upgrade_test.cpp
* DESCRIPTION: Independent metadata accumulators during concurrent upgrades
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <future>
#include <iostream>
#include <thread>

#include "convert.hpp"
#include "drd_std.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static bool
check_document (int id) {
  string keyword= id == 0 ? "algebra" : "analysis";
  string ams= id == 0 ? "14A05" : "46A03";
  tree title= compound ("make-title", tree (DOCUMENT,
    compound ("title", "Example")));
  tree abstract= compound ("abstract", tree (DOCUMENT,
    compound ("keywords", keyword), compound ("AMS-class", ams)));
  tree result= upgrade (tree (DOCUMENT, title, abstract), "1.0.4");
  tree expected= tree (DOCUMENT,
    compound ("doc-data", compound ("doc-title", "Example")),
    compound ("abstract-data", compound ("abstract", tree (DOCUMENT, "")),
      compound ("abstract-msc", ams), compound ("abstract-keywords", keyword)));
  if (is_func (result, DOCUMENT) && N(result) >= 2 &&
      result[0] == expected[0] && result[1] == expected[1]) return true;
  cerr << "Unexpected metadata upgrade: " << result << LF;
  return false;
}

int
main () {
  init_std_drd ();
  if (!check_document (0) || !check_document (1)) return 1;
  std::promise<void> ready[2];
  auto first= ready[0].get_future ();
  auto second= ready[1].get_future ();
  bool correct[2]= {true, true};
  auto run= [&] (int id, std::future<void>& other) {
    ready[id].set_value ();
    other.wait ();
    for (int i= 0; i < 64; ++i)
      if (!check_document (id)) correct[id]= false;
  };
  std::thread a (run, 0, std::ref (second));
  std::thread b (run, 1, std::ref (first));
  a.join ();
  b.join ();
  if (!correct[0] || !correct[1]) return 1;
  std::cout << "Document metadata stays with its upgrade invocation\n";
  return 0;
}
