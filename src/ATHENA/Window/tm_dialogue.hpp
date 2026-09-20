/******************************************************************************
* MODULE     : tm_dialogue.hpp
* DESCRIPTION: Internal dialogue dispatch helpers
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef TM_DIALOGUE_H
#define TM_DIALOGUE_H

#include "command.hpp"
#include "scheme_execution_context.hpp"

object evaluate_chooser_result (string expression);

void dispatch_actor_chooser_result (
  command actor_fun, string expression,
  athena_actor_id actor_id, athena_view_id view_id,
  SchemeCapabilitySet capabilities);

#endif // TM_DIALOGUE_H
