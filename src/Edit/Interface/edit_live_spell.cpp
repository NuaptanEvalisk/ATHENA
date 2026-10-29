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
#include "Bridge/impl_typesetter.hpp"

void
edit_interface_rep::live_spelling_announce (modification mod) {
  live_spelling.change (subtree (et, rp), mod / rp);
}

void
edit_interface_rep::publish_live_spelling () {
  if (live_spelling.initialized)
    set_alt_selection ("spell-live", live_spelling.selections (rp));
}

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
  if (live_spelling_dirty || !live_spelling.initialized) {
    live_spelling.invalidate ();
    live_spelling_dirty= false;
  }
  path focus= tree_path (path (), vx1, vy2, 0);
  if (is_nil (focus)) return;
  if (!(rp <= focus)) focus= tp;
  const auto layout= get_typesetter ();
  if (!layout->execution_base) return;
  tree body= subtree (et, rp);
  const spelling_context_lookup lookup= [&] (path relative) -> spelling_context {
    const auto occurrence= find_source_occurrence (
      layout->br->source_occurrences, rp * relative, subtree (body, relative));
    if (!occurrence) return {};
    const auto* prepared= layout->execution_contexts.find (occurrence->boundary);
    if (!prepared) return {};
    const auto& context= *prepared;
    return {{context, &context->contract_context ()}, occurrence->boundary,
            context->effective_value (MODE), context->effective_value (LANGUAGE)};
  };
  if (live_spelling.step (body, lookup, layout->execution_base, focus / rp))
    publish_live_spelling ();
}
