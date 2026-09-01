/******************************************************************************
* MODULE     : Memory allocation
* DESCRIPTION: ATHENA allocation entry points backed by mimalloc
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "fast_alloc.hpp"

#include <limits>

void*
fast_new (size_t size) {
  return mi_new (size);
}

void
fast_delete (void* ptr) {
  mi_free (ptr);
}

static size_t
current_memory_usage () {
  size_t current_rss= 0;
  size_t current_commit= 0;
  mi_process_info (nullptr, nullptr, nullptr,
                   &current_rss, nullptr, &current_commit, nullptr, nullptr);
  return current_commit == 0 ? current_rss : current_commit;
}

int
mem_used () {
  size_t bytes= current_memory_usage ();
  size_t limit= static_cast<size_t> (std::numeric_limits<int>::max ());
  return static_cast<int> (bytes > limit ? limit : bytes);
}

void
mem_info () {
  size_t current_rss= 0;
  size_t peak_rss= 0;
  size_t current_commit= 0;
  size_t peak_commit= 0;
  mi_process_info (nullptr, nullptr, nullptr,
                   &current_rss, &peak_rss,
                   &current_commit, &peak_commit, nullptr);
  cout << "\n---------------- memory statistics ----------------\n";
  cout << "Resident      : " << current_rss << " bytes\n";
  cout << "Peak resident : " << peak_rss << " bytes\n";
  cout << "Committed     : " << current_commit << " bytes\n";
  cout << "Peak committed: " << peak_commit << " bytes\n";
}
