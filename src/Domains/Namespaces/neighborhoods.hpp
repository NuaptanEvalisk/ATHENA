/******************************************************************************
* MODULE     : neighborhoods.hpp
* DESCRIPTION: ATHENA document neighborhood service
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_NEIGHBORHOODS_HPP
#define ATHENA_NEIGHBORHOODS_HPP

#include "string.hpp"
#include "url.hpp"

#include <vector>

enum athena_neighborhood_kind {
  ATHENA_NEIGHBORHOOD_PATH,
  ATHENA_NEIGHBORHOOD_NAMESPACE
};

struct athena_neighborhood_entry {
  url    file;
  string display;
  string canonical_path;
};

struct athena_neighborhood_row {
  string                     key;
  string                     name;
  string                     namespace_name;
  athena_neighborhood_kind   kind;
  std::vector<athena_neighborhood_entry> files;
  int                        current_index;
  string                     warning;
};

struct athena_neighborhood_set {
  bool                       valid;
  url                        current_file;
  string                     canonical_path;
  std::vector<athena_neighborhood_row> rows;
  int                        selected_row;
  string                     selected_key;
  string                     error;

  athena_neighborhood_set ();
};

athena_neighborhood_set athena_neighborhoods_for_file (url file);
athena_neighborhood_set athena_current_neighborhoods ();
bool athena_neighborhood_select (url file, string key);
bool athena_neighborhood_select_row (url file, int row);
bool athena_neighborhood_neighbor (url file, int direction, url& out,
                                   string& message);
bool athena_neighborhood_current_neighbor (int direction, url& out,
                                           string& message);
bool athena_neighborhood_cycle (url file, string& message);
bool athena_neighborhood_cycle_current (string& message);
void athena_neighborhood_clear_session ();

#endif // ATHENA_NEIGHBORHOODS_HPP
