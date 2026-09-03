/******************************************************************************
* MODULE     : GoogleAsyncDispatch.cpp
* DESCRIPTION: Thread-affine dispatch for Google service requests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "GoogleAsyncDispatch.hpp"

#include "buffer_actor.hpp"
#include "scheme_execution_context.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

GoogleAsyncOrigin
google_async_origin () noexcept {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr) return {};
  return {context->actor_id, context->view_id, context->capabilities};
}

void
google_dispatch_to_qt (std::function<void()> continuation) {
  if (!continuation) return;
  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr || QThread::currentThread () == app->thread ()) {
    continuation ();
    return;
  }

  athena_continuation_id id=
    actor_continuation_registry::instance ().store (
      std::move (continuation));
  bool queued= QMetaObject::invokeMethod (
    app,
    [id] {
      std::function<void()> pending=
        actor_continuation_registry::instance ().take (id);
      if (pending) pending ();
    },
    Qt::QueuedConnection);
  if (!queued)
    (void) actor_continuation_registry::instance ().discard (id);
}

void
google_dispatch_to_origin (GoogleAsyncOrigin origin,
                           std::function<void()> continuation) {
  if (!continuation) return;
  if (origin.actorId == ATHENA_NO_ACTOR) {
    google_dispatch_to_qt (std::move (continuation));
    return;
  }

  athena_continuation_id id=
    actor_continuation_registry::instance ().store (
      std::move (continuation));
  actor_command_ticket ticket= buffer_actor::submit_to (
    origin.actorId, actor_command_kind::run_native_continuation,
    origin.viewId, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
    origin.capabilities, id);
  if (!ticket)
    (void) actor_continuation_registry::instance ().discard (id);
}
