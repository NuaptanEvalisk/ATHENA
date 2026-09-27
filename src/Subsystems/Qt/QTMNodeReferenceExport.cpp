/******************************************************************************
* MODULE     : QTMNodeReferenceExport.cpp
* DESCRIPTION: Owner-bound export continuations after asynchronous UUID preparation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "ATHENA/Data/node_reference_export.hpp"
#include "ATHENA/Data/vault_node_location.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "editor.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "object.hpp"
#include "guile_tm.hpp"
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <set>

namespace {
namespace ref= athena::node_reference;
struct request {
  athena_actor_id actor;
  athena_view_id view;
  SchemeCapabilitySet capabilities;
  std::uint64_t epoch;
  std::string source;
  std::vector<int> cursor, selection_start, selection_end;
  bool selected= false;
  vault_context_handle vault;
  athena_scheme_handle_id action= ATHENA_NO_SCHEME_HANDLE;
  std::unique_ptr<ref::export_preparation> preparation;
  ref::prepared_snapshot ready;
  std::shared_ptr<athena::node_location::service> locator;
  std::set<ref::selection> requested;
  unsigned rounds= 0;
  ~request () { scheme_command_handle_release (action); }
};
void resume_preparation (std::shared_ptr<request>, std::vector<ref::selection>);
std::string text (string s) { return {s.data (), std::size_t (N(s))}; }
std::vector<int> indices (path p) {
  std::vector<int> out;
  while (!is_nil (p)) { out.push_back (p->item); p= p->next; }
  return out;
}

void perform (const std::shared_ptr<request>& job) {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor || !context->editor ||
      context->actor_id != job->actor || context->view_id != job->view) return;
  try {
    if (!vault_context_is_current (job->vault) || ref::source_epoch () != job->epoch ||
        text (as_string (context->actor->current_buffer_url ())) != job->source)
      throw std::runtime_error ("Source changed while preparing export; please export again");
    auto* editor= context->editor;
    if (indices (editor->the_path ()) != job->cursor ||
        editor->selection_active_any () != job->selected ||
        (job->selected && (indices (editor->selection_get_start ()) != job->selection_start ||
                           indices (editor->selection_get_end ()) != job->selection_end)))
      throw std::runtime_error ("Cursor or selection changed while preparing export; please export again");
    ref::export_reference_scope frozen (job->ready);
    editor->probe_print_references ();
    auto missing= frozen.missing ();
    if (!missing.empty ()) {
      resume_preparation (job, std::move (missing));
      return;
    }
    (void) call_scheme (scheme_command_handle_value (job->action));
    frozen.require_ready ();
  }
  catch (const std::exception& e) {
    context->editor->set_message ("Export failed", tree (e.what ()), true);
  }
}

class exports: public QObject {
  QTimer timer;
  std::vector<std::shared_ptr<request>> pending;
public:
  explicit exports (QObject* parent): QObject (parent), timer (this) {
    timer.setInterval (100);
    connect (&timer, &QTimer::timeout, this, [this] { dispatch (); });
    connect (QCoreApplication::instance (), &QCoreApplication::aboutToQuit, this, [this] {
      timer.stop (); pending.clear ();
    });
    timer.start ();
  }
  void add (std::shared_ptr<request> job, std::vector<ref::selection> seeds) {
    pending.push_back (job);
    const auto before= job->requested.size ();
    job->requested.insert (seeds.begin (), seeds.end ());
    if (++job->rounds > 32 || job->requested.size () > ref::preparation_limits {}.selections ||
        (job->rounds > 1 && job->requested.size () == before)) {
      auto failed= std::make_shared<ref::prepared_references> ();
      failed->error= "Export reference discovery exceeded its budget or made no progress";
      job->ready= std::move (failed);
      return;
    }
    job->ready.reset ();
    seeds.assign (job->requested.begin (), job->requested.end ());
    job->preparation= std::make_unique<ref::export_preparation> (job->locator, std::move (seeds),
      [weak= std::weak_ptr<request> (job)] (ref::prepared_snapshot result) {
        qt_post_to_main_thread ([weak, result= std::move (result)] {
          if (auto job= weak.lock ()) job->ready= result;
        });
      });
  }
  void dispatch () {
    std::set<std::uint64_t> alive;
    for (const auto& pair: published_buffer_metadata ()) alive.insert (pair.second.actor_id);
    for (auto it= pending.begin (); it != pending.end (); ) {
      const auto job= *it;
      if (!alive.count (job->actor)) { it= pending.erase (it); continue; }
      if (!job->ready && (!vault_context_is_current (job->vault) || ref::source_epoch () != job->epoch))
        job->preparation->cancel ();
      if (!job->ready) { ++it; continue; }
      auto continuation= actor_continuation_registry::instance ().store ([job] { perform (job); });
      if (buffer_actor::try_submit_to (job->actor, actor_command_kind::run_native_continuation,
          job->view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, job->capabilities, continuation))
        it= pending.erase (it);
      else { actor_continuation_registry::instance ().discard (continuation); ++it; }
    }
  }
};
exports* controller () {
  static QPointer<exports> instance;
  if (!instance) instance= new exports (QCoreApplication::instance ());
  return instance;
}
void resume_preparation (std::shared_ptr<request> job, std::vector<ref::selection> missing) {
  qt_post_to_main_thread ([job= std::move (job), missing= std::move (missing)] () mutable {
    controller ()->add (std::move (job), std::move (missing));
  });
}
} // namespace

bool athena_node_reference_with_export (object action) {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor || !context->editor) return false;
  auto source= context->actor->current_source (context->view_id);
  auto seeds= ref::export_selections (source);
  if (ref::current_export_references ()) {
    (void) call (action); return true;
  }
  if (seeds.empty ()) {
    try {
      ref::export_reference_scope probe (std::make_shared<const ref::prepared_references> ());
      context->editor->probe_print_references ();
      seeds= probe.missing ();
    }
    catch (const std::exception& e) {
      context->editor->set_message ("Export failed", tree (e.what ()), true);
      return false;
    }
    if (seeds.empty ()) { (void) call (action); return true; }
  }
  auto vault= vault_capture_context ();
  if (!vault || !QCoreApplication::instance ()) {
    context->editor->set_message ("Export failed", "Node references require an active vault", true);
    return false;
  }
  auto job= std::make_shared<request> ();
  job->actor= context->actor_id; job->view= context->view_id;
  job->capabilities= context->capabilities; job->epoch= ref::source_epoch ();
  job->source= text (as_string (context->actor->current_buffer_url ()));
  job->cursor= indices (context->editor->the_path ());
  job->selected= context->editor->selection_active_any ();
  if (job->selected) {
    job->selection_start= indices (context->editor->selection_get_start ());
    job->selection_end= indices (context->editor->selection_get_end ());
  }
  job->vault= vault;
  job->locator= athena::node_location::for_vault (vault);
  job->action= scheme_command_handle_acquire (object_to_tmscm (action));
  context->editor->set_message ("Preparing export references", "Export", true);
  qt_post_to_main_thread ([job, seeds= std::move (seeds)] () mutable {
    controller ()->add (job, std::move (seeds));
  });
  return true;
}
