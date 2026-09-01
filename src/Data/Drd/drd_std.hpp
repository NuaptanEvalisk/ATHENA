
/******************************************************************************
* MODULE     : drd_std.hpp
* DESCRIPTION: standard drd for TeXmacs; most other drd's inherit from it
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef DRD_STD_H
#define DRD_STD_H
#include "drd_info.hpp"

extern drd_info std_drd;
extern hashmap<string,int> STD_CODE;

drd_info& current_drd () noexcept;
drd_info* swap_current_drd (drd_info* drd) noexcept;

#define the_drd (current_drd ())

inline bool std_contains (string s) { return STD_CODE->contains (s); }

void init_std_drd ();

struct with_drd {
  drd_info local_drd;
  drd_info* old_drd;
  inline with_drd (drd_info new_drd):
    local_drd (new_drd), old_drd (swap_current_drd (&local_drd)) {}
  inline ~with_drd () { swap_current_drd (old_drd); }
};

struct with_borrowed_drd {
  drd_info* old_drd;
  inline explicit with_borrowed_drd (drd_info* new_drd):
    old_drd (swap_current_drd (new_drd)) {}
  inline ~with_borrowed_drd () { swap_current_drd (old_drd); }

  with_borrowed_drd (const with_borrowed_drd&)= delete;
  with_borrowed_drd& operator = (const with_borrowed_drd&)= delete;
};

#endif // defined DRD_STD_H
