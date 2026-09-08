/******************************************************************************
* MODULE     : edit_live_spell.cpp
* DESCRIPTION: Bounded live spell checking on the owning BufferActor
* COPYRIGHT  : (C) 2026  Felix Lian
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_interface.hpp"
#include "Ispell/ispell.hpp"
#include "scheme.hpp"
#include "vars.hpp"

void
edit_interface_rep::update_live_spelling () {
  if (get_preference ("live spell checking") != "on") {
    if (live_spelling.initialized) {
      live_spelling.reset ();
    }
    cancel_alt_selection ("spell-live");
    live_spelling_dirty= true;
    return;
  }
  unsigned long revision= ispell_dictionary_revision ();
  if (revision != live_spelling_dictionary_revision) {
    live_spelling_dictionary_revision= revision;
    live_spelling_dirty= true;
  }
  // Never use an old typeset box to choose a starting path after an edit.
  if (env_change & (THE_TREE | THE_ENVIRONMENT)) return;
  if (is_nil (eb) || !ui_viewport ().attached || input_mode != INPUT_NORMAL ||
      texmacs_time () < live_spelling_next) return;
  live_spelling_next= texmacs_time () + 50;
  if (!live_spelling.done () &&
      (live_spelling_x != vx1 || live_spelling_y != vy2))
    live_spelling_dirty= true;
  if (live_spelling_dirty || !live_spelling.initialized) {
    live_spelling_x= vx1;
    live_spelling_y= vy2;
    path focus= tree_path (path (), vx1, vy2, 0);
    if (!(rp <= focus)) focus= tp;
    live_spelling.start (subtree (et, rp), get_init_string (MODE),
                         get_init_string (LANGUAGE), rp, focus / rp);
    live_spelling_dirty= false;
    cancel_alt_selection ("spell-live");
  }
  bool changed= live_spelling.step ();
  if (tp != live_spelling_edit_cursor) live_spelling_edit_cursor= path ();
  if (changed || tp != live_spelling_cursor) {
    live_spelling_cursor= copy (tp);
    set_alt_selection ("spell-live",
                       live_spelling.selections (live_spelling_edit_cursor));
  }
}
