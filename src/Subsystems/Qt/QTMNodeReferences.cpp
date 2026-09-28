/******************************************************************************
* MODULE     : QTMNodeReferences.cpp
* DESCRIPTION: Watched native reference snapshots with actor-safe refresh delivery
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "ATHENA/Data/node_reference.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "ATHENA/Data/vault_node_location.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "Interface/edit_interface.hpp"
#include "tree_cursor.hpp"
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QTimer>
#include <QMessageBox>
#include <QUrl>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace athena::node_reference {
namespace {
using recipient= std::pair<std::uint64_t, std::uint64_t>;
using key= std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>;
struct entry {
  vault_context_handle vault;
  std::shared_ptr<node_location::service> locator;
  std::vector<std::string> ids, ancestry;
  std::shared_ptr<node_location::query> query;
  view published;
  std::set<recipient> subscribers;
  std::uint64_t generation= 0;
  bool refreshing= false;
  bool retry_pending= false;
  std::chrono::steady_clock::time_point retry_after;
};
std::atomic<std::uint64_t> changes {0};
std::mutex cache_lock;
std::map<key, std::shared_ptr<entry>> entries;
std::uint64_t revision= 0;
void start (const std::shared_ptr<entry>&);

class monitor: public QObject {
  QFileSystemWatcher watcher;
  QTimer timer;
  std::uint64_t observed= changes.load ();
  std::map<std::string, recipient> members;
  std::set<recipient> refresh;
  std::weak_ptr<entry> navigating;
  std::uint64_t navigation_origin= 0;
public:
  explicit monitor (QObject* parent): QObject (parent), watcher (this), timer (this) {
    connect (&watcher, &QFileSystemWatcher::fileChanged, this, [] { source_changed (); });
    connect (&watcher, &QFileSystemWatcher::directoryChanged, this, [] { source_changed (); });
    timer.setInterval (100);
    connect (&timer, &QTimer::timeout, this, [this] {
      std::map<std::string, recipient> current;
      for (const auto& pair: published_buffer_metadata ())
        current[pair.first]= {pair.second.actor_id, pair.second.source_view};
      if (current != members) { members= std::move (current); source_changed (); }
      if (observed != changes.load ()) { observed= changes.load (); invalidate (); }
      std::vector<std::shared_ptr<entry>> retry;
      {
        std::lock_guard<std::mutex> guard (cache_lock);
        for (const auto& pair: entries) {
          auto& e= *pair.second;
          if (e.retry_pending && !e.refreshing &&
              e.retry_after <= std::chrono::steady_clock::now ()) {
            ++e.generation;
            e.retry_pending= false;
            retry.push_back (pair.second);
          }
        }
      }
      for (const auto& e: retry) start (e);
      deliver ();
      finish_navigation ();
    });
    timer.start ();
    connect (QCoreApplication::instance (), &QCoreApplication::aboutToQuit, this, [] {
      std::map<key, std::shared_ptr<entry>> old;
      { std::lock_guard<std::mutex> guard (cache_lock); old.swap (entries); }
    });
  }
  void navigate (const std::shared_ptr<entry>& target, std::uint64_t origin) {
    navigating= target; navigation_origin= origin;
    finish_navigation ();
  }
  void finish_navigation () {
    auto target= navigating.lock ();
    if (!target) return;
    if (!vault_context_is_current (target->vault) ||
        (navigation_origin && published_active_buffer () != navigation_origin)) {
      navigating.reset (); return;
    }
    node_location::snapshot result;
    { std::lock_guard<std::mutex> guard (cache_lock); result= target->published.snapshot; }
    if (!result || result->state == node_location::status::pending) return;
    navigating.reset ();
    if (result->state != node_location::status::resolved || result->items.size () != 1) {
      std::string reason= "Referenced node is unavailable";
      if (!result->items.empty ()) reason= result->items.front ().diagnostic;
      else if (!result->diagnostics.empty ()) reason= result->diagnostics.front ().message;
      QMessageBox box (QMessageBox::Warning, "Node reference", QString::fromStdString (reason));
      box.setTextFormat (Qt::PlainText); box.exec (); return;
    }
    const auto& position= result->items.front ().candidates.front ();
    std::string destination;
    if (position.actor) {
      for (const auto& pair: published_buffer_metadata ())
        if (pair.second.actor_id == position.actor) destination= pair.first;
    }
    else destination= (target->vault->root / position.file).string ();
    if (destination.empty ()) return;
    array<object> command;
    command << symbol_object ("node-reference-jump-to-source")
            << object (url (destination.c_str ())) << object (string (target->ids.front ().c_str ()));
    exec_delayed (scheme_cmd (as_list_object (command)));
  }
  void deliver () {
    std::set<std::uint64_t> alive;
    for (const auto& pair: published_buffer_metadata ()) alive.insert (pair.second.actor_id);
    for (auto it= refresh.begin (); it != refresh.end (); ) {
      if (!alive.count (it->first)) { it= refresh.erase (it); continue; }
      auto callback= actor_continuation_registry::instance ().store ([] {
        athena_refresh_node_reference_view ();
      });
      if (buffer_actor::try_submit_to (it->first, actor_command_kind::run_native_continuation,
          it->second, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, callback))
        it= refresh.erase (it);
      else { actor_continuation_registry::instance ().discard (callback); ++it; }
    }
  }
  void invalidate () {
    std::vector<std::shared_ptr<entry>> again, retired;
    {
      std::lock_guard<std::mutex> guard (cache_lock);
      for (auto i= entries.begin (); i != entries.end (); ) {
        if (!vault_context_is_current (i->second->vault)) {
          retired.push_back (i->second); i= entries.erase (i); continue;
        }
        auto& e= *i->second;
        ++e.generation;
        e.retry_pending= false;
        again.push_back (i->second); ++i;
      }
    }
    if (!retired.empty ()) {
      const auto paths= watcher.files () + watcher.directories ();
      if (!paths.empty ()) watcher.removePaths (paths);
    }
    for (const auto& e: again) start (e);
  }
  void complete (const std::shared_ptr<entry>& e, std::uint64_t generation,
                   node_location::snapshot result) {
    {
      std::lock_guard<std::mutex> guard (cache_lock);
      if (generation != e->generation || !vault_context_is_current (e->vault)) return;
      e->refreshing= false;
    }
    QStringList missing;
    const auto known= watcher.files () + watcher.directories ();
    if (result->watched_paths)
      for (const auto& path: *result->watched_paths) {
        QString name= QString::fromStdString (path);
        if (!known.contains (name) && !missing.contains (name)) missing << name;
      }
    if (!missing.empty ()) {
      const auto failed= watcher.addPaths (missing);
      if (failed.empty ()) {
        // Install watches before trusting a scan. A second inventory closes
        // the discovery-to-watch gap, including newly copied duplicate IDs.
        invalidate (); return;
      }
      auto error= std::make_shared<node_location::result> (*result);
      error->state= node_location::status::unreadable;
      error->diagnostics.push_back ({failed.front ().toStdString (), "Cannot watch reference source changes"});
      for (auto& target: error->items) if (target.state == node_location::status::resolved) {
        target.state= node_location::status::unreadable;
        target.fragment_xml.clear ();
        target.diagnostic= "Cannot establish source change monitoring";
      }
      result= error;
    }
    {
      std::lock_guard<std::mutex> guard (cache_lock);
      if (generation != e->generation) return;
      // A locator/cache miss or stale address is an implementation detail, not
      // a change of node identity.  If this entry already has a verified
      // resolved presentation, keep it visible while only this entry retries.
      if (result->state == node_location::status::unreadable &&
          e->published.snapshot &&
          e->published.snapshot->state == node_location::status::resolved) {
        e->retry_pending= true;
        e->retry_after= std::chrono::steady_clock::now () + std::chrono::seconds (2);
        return;
      }
      e->published= {std::move (result), ++revision};
      e->retry_pending= e->published.snapshot &&
        e->published.snapshot->state == node_location::status::unreadable;
      e->retry_after= std::chrono::steady_clock::now () + std::chrono::seconds (2);
      refresh.insert (e->subscribers.begin (), e->subscribers.end ());
    }
    deliver ();
  }
};

monitor* controller () {
  static QPointer<monitor> instance;
  if (!instance) instance= new monitor (QCoreApplication::instance ());
  return instance;
}
void start (const std::shared_ptr<entry>& e) {
  std::shared_ptr<node_location::query> old;
  std::uint64_t generation;
  {
    std::lock_guard<std::mutex> guard (cache_lock);
    generation= e->generation;
    e->refreshing= true;
    old.swap (e->query);
  }
  if (old) old->cancel ();
  auto pending= e->locator->request (e->ids, e->ancestry, true,
    [weak= std::weak_ptr<entry> (e), generation] (node_location::snapshot result) {
      if (result->state == node_location::status::cancelled) return;
      qt_post_to_main_thread ([weak, generation, result= std::move (result)] {
        if (auto current= weak.lock ()) controller ()->complete (current, generation, result);
      });
    });
  bool obsolete;
  {
    std::lock_guard<std::mutex> guard (cache_lock);
    obsolete= generation != e->generation;
    if (!obsolete) e->query= pending;
  }
  if (obsolete) pending->cancel ();
}
}

void source_changed () {
  changes.fetch_add (1, std::memory_order_relaxed);
}
std::uint64_t source_epoch () { return changes.load (std::memory_order_relaxed); }
view get (std::vector<std::string> ids, std::vector<std::string> ancestry) {
  if (auto frozen= export_reference_view ({ids, ancestry})) return *frozen;
  auto vault= vault_capture_context ();
  if (!vault || !QCoreApplication::instance ()) {
    auto error= std::make_shared<node_location::result> ();
    error->state= node_location::status::unreadable;
    error->diagnostics.push_back ({"", "Node references require an active vault and event loop"});
    return {error, 0};
  }
  const key lookup {vault->incarnation, ids, ancestry};
  std::shared_ptr<entry> current;
  bool fresh= false;
  view result;
  {
    std::lock_guard<std::mutex> guard (cache_lock);
    auto& stored= entries[lookup];
    if (!stored) {
      stored= std::make_shared<entry> ();
      stored->vault= vault;
      stored->locator= node_location::for_vault (vault);
      stored->ids= std::move (ids); stored->ancestry= std::move (ancestry);
      fresh= true;
    }
    current= stored;
    if (const auto* context= current_scheme_execution_context ())
      if (context->actor) stored->subscribers.emplace (context->actor_id, context->view_id);
    result= stored->published;
  }
  if (fresh) { qt_post_to_main_thread ([] { controller (); }); start (current); }
  return result;
}
} // namespace athena::node_reference

bool athena_node_reference_target (string target) {
  const auto id= athena::node_reference::target_id (target);
  if (id.empty ()) return false;
  const QUrl parsed (QString::fromUtf8 (target.data (), N(target)), QUrl::StrictMode);
  if (parsed.host ().compare ("wikilink", Qt::CaseInsensitive) == 0) {
    const auto vault= vault_capture_context ();
    return vault && vault->node_model_version >= 1;
  }
  return true;
}
bool athena_node_reference_open (string target) {
  try {
  namespace ref= athena::node_reference;
  const auto id= ref::target_id (target);
  const auto vault= vault_capture_context ();
  if (id.empty () || !vault) return false;
  const QUrl parsed (QString::fromUtf8 (target.data (), N(target)), QUrl::StrictMode);
  if (parsed.host ().compare ("wikilink", Qt::CaseInsensitive) == 0 &&
      vault->node_model_version < 1)
    return false;
  ref::get ({id});
  std::shared_ptr<ref::entry> entry;
  {
    std::lock_guard<std::mutex> guard (ref::cache_lock);
    const auto found= ref::entries.find (ref::key {vault->incarnation, {id}, {}});
    if (found == ref::entries.end ()) return false;
    entry= found->second;
  }
  const auto* context= current_scheme_execution_context ();
  const auto origin= context && context->actor ? context->actor_id : published_active_buffer ();
  qt_post_to_main_thread ([entry, origin] { ref::controller ()->navigate (entry, origin); });
  return true;
  }
  catch (const std::exception& e) {
    const auto* context= current_scheme_execution_context ();
    if (context && context->actor && context->editor)
      context->editor->set_message ("Node reference", tree (e.what ()), true);
    return false;
  }
}
bool athena_node_reference_position (string identity) {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor || !context->editor) return false;
  auto* editor= context->editor;
  const std::string id (identity.data (), std::size_t (N(identity)));
  auto locate= [&] {
    auto body= editor->the_buffer ();
    path relative;
    bool found= false;
    for (const auto& match: athena::node_location::collect (body)) if (match.id == id) {
      if (found) throw std::runtime_error ("Conflicting node identity in the current document");
      found= true;
      for (const auto& step: match.where) {
        if (step.kind != athena::node_location::step_kind::child)
          throw std::runtime_error ("Target is inside a structured property, not the document body");
        relative= relative * int (step.index);
      }
    }
    if (!found) throw std::runtime_error ("Referenced node is no longer in this document");
    return editor->the_buffer_path () * start (body, relative);
  };
  try {
    editor->go_to (locate ());
    editor->show_cursor_if_hidden ();
    editor->go_to (locate ());
    return true;
  }
  catch (const std::exception& e) {
    editor->set_message ("Node reference", tree (e.what ()), true); return false;
  }
}
