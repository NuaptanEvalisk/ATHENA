/******************************************************************************
* MODULE     : enunciation_presentation.hpp
* DESCRIPTION: Native presentation of property-backed source enunciations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_ENUNCIATION_PRESENTATION_HPP
#define ATHENA_ENUNCIATION_PRESENTATION_HPP

#include "env.hpp"
#include "ATHENA/Data/enunciation_model.hpp"

// One pure macro plan for execution, cursor environments and all typeset routes.
// The body is an ARG, never a copied or reparented source subtree. Rich property
// values are projected to inert presentation; unknown markup stays visible.
tree native_enunciation_macro (edit_env_rep* env, const tree& source);
tree native_enunciation_rich_text (const tree& source);

#endif
