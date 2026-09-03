/******************************************************************************
* MODULE     : GoogleAsyncDispatch.hpp
* DESCRIPTION: Thread-affine dispatch for Google service requests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef GOOGLEASYNCDISPATCH_HPP
#define GOOGLEASYNCDISPATCH_HPP

#include "scheme_execution_context.hpp"

#include <functional>

struct GoogleAsyncOrigin {
  athena_actor_id actorId= ATHENA_NO_ACTOR;
  athena_view_id viewId= ATHENA_NO_VIEW;
  SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_NONE;
};

GoogleAsyncOrigin google_async_origin () noexcept;
void google_dispatch_to_qt (std::function<void()> continuation);
void google_dispatch_to_origin (GoogleAsyncOrigin origin,
                                std::function<void()> continuation);

#endif // GOOGLEASYNCDISPATCH_HPP
