/******************************************************************************
* MODULE     : node_location.cpp
* DESCRIPTION: Verified UUID locations with coalesced scans and live source precedence
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "node_location.hpp"
#include "node_metadata.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace athena::node_location {
namespace fs= athena::filesystem;
namespace {
using property= node::property;
constexpr std::size_t max_nodes= 1000000, max_depth= 256;

struct collector {
  census found;
  address where;
  std::set<const tree_rep*> active;
  std::size_t visited= 0;
  void enter () {
    if (++visited > max_nodes || where.size () > max_depth)
      throw std::length_error ("Node location census exceeds its structural budget");
  }
  template<class F> void at (step next, F fn) {
    where.push_back (std::move (next));
    fn ();
    where.pop_back ();
  }
  void value (const property& p) {
    enter ();
    if (const auto* list= std::get_if<property::list> (&p.data))
      for (std::size_t i=0; i<list->size (); ++i)
        at ({step_kind::list_item, i, {}}, [&] { value ((*list)[i]); });
    else if (const auto* dict= std::get_if<property::dictionary> (&p.data))
      for (const auto& field: *dict)
        at ({step_kind::dictionary_item, 0, field.first}, [&] { value (field.second); });
    else if (const auto* rich= std::get_if<node::rich_text> (&p.data))
      at ({step_kind::rich_text, 0, {}}, [&] { walk (rich->content); });
  }
  void walk (const tree& t) {
    enter ();
    if (!active.insert (inside (t)).second)
      throw std::invalid_argument ("Cyclic source tree in node location census");
    if (const auto* m= node::get (t)) {
      if (!m->id.empty ()) {
        if (!node::valid_id (m->id)) throw std::invalid_argument ("Invalid source UUID");
        found.push_back ({m->id, where});
      }
      for (const auto& field: m->properties)
        at ({step_kind::property, 0, field.first}, [&] { value (field.second); });
    }
    if (is_compound (t))
      for (int i=0; i<N(t); ++i)
        at ({step_kind::child, std::size_t (i), {}}, [&] { walk (t[i]); });
    active.erase (inside (t));
  }
};

bool prefix (const address& a, const address& b) {
  return a.size () < b.size () && std::equal (a.begin (), a.end (), b.begin ());
}
bool excluded (const std::string& name) {
  return name == ".athena" || name == ".backup" || name == ".git";
}
bool valid_file (const std::string& name) {
  const std::filesystem::path path (name);
  if (name.empty () || path.is_absolute () || path.generic_string () != name) return false;
  for (const auto& component: path)
    if (component.empty () || component == "." || component == ".." ||
        excluded (component.string ())) return false;
  return path.extension () == ".ath";
}
bool internal_path (const std::filesystem::path& path) {
  for (const auto& component: path) if (excluded (component.string ())) return true;
  return false;
}

struct source {
  std::string file;
  std::uint64_t actor= 0, capture= 0;
  std::optional<fs::metadata> disk_revision;
  census nodes;
};
using sources= std::vector<std::shared_ptr<const source>>;
using index= std::map<std::string, std::vector<location>>;

index make_index (const sources& documents) {
  index out;
  for (const auto& document: documents)
    for (const auto& node: document->nodes)
      out[node.id].push_back ({document->file, node.where, document->actor,
                             document->capture, document->disk_revision});
  return out;
}

result resolve (const std::vector<std::string>& ids,
                const std::vector<std::string>& ancestry,
                const index& locations, const std::vector<diagnostic>& errors,
                std::uint64_t scan) {
  result out;
  out.state= status::resolved;
  out.scan= scan;
  out.ancestry= ancestry;
  out.diagnostics= errors;
  for (const auto& id: ids) {
    item target;
    target.id= id;
    const auto found= locations.find (id);
    if (found != locations.end ()) target.candidates= found->second;
    if (std::find (ancestry.begin (), ancestry.end (), id) != ancestry.end ()) {
      target.state= status::cycle;
      target.diagnostic= "Target is already in the reference expansion ancestry";
    }
    else if (target.candidates.size () > 1 || std::any_of (errors.begin (), errors.end (),
        [&] (const diagnostic& e) { return e.state == status::conflict && e.identity == id; })) {
      target.state= status::conflict;
      target.diagnostic= "UUID occurs at more than one source location";
    }
    else if (!errors.empty ()) {
      target.state= status::unreadable;
      target.diagnostic= "Incomplete source inventory; uniqueness or absence cannot be verified";
    }
    else if (target.candidates.empty ()) {
      target.state= status::missing;
      target.diagnostic= "UUID is absent from the captured vault sources";
    }
    else target.state= status::resolved;
    if (out.state == status::resolved && target.state != status::resolved)
      out.state= target.state;
    out.items.push_back (std::move (target));
  }
  std::vector<item*> ordered;
  for (auto& target: out.items)
    if (target.candidates.size () == 1) ordered.push_back (&target);
  std::sort (ordered.begin (), ordered.end (), [] (const item* a, const item* b) {
    const auto& x= a->candidates.front (); const auto& y= b->candidates.front ();
    if (x.file != y.file || x.actor != y.actor)
      return std::tie (x.file, x.actor) < std::tie (y.file, y.actor);
    return std::lexicographical_compare (x.where.begin (), x.where.end (),
      y.where.begin (), y.where.end (), [] (const step& a, const step& b) {
        return std::tie (a.kind, a.index, a.key) < std::tie (b.kind, b.index, b.key);
      });
  });
  std::vector<item*> ancestors;
  for (auto* target: ordered) {
    const auto& x= target->candidates.front ();
    while (!ancestors.empty ()) {
      const auto& y= ancestors.back ()->candidates.front ();
      if (x.file == y.file && x.actor == y.actor && prefix (y.where, x.where)) break;
      ancestors.pop_back ();
    }
    if (!ancestors.empty ()) {
      for (auto* involved: {target, ancestors.back ()})
        if (involved->state == status::resolved || involved->state == status::overlap) {
          involved->state= status::overlap;
          involved->diagnostic= "Selection contains both an ancestor and its descendant";
        }
      if (out.state == status::resolved) out.state= status::overlap;
    }
    ancestors.push_back (target);
  }
  return out;
}
} // namespace

bool step::operator == (const step& other) const {
  return kind == other.kind && index == other.index && key == other.key;
}
census collect (const tree& source) {
  collector walk;
  walk.walk (source);
  return std::move (walk.found);
}
tree lookup (const tree& source, const address& where, const std::string& expected_id) {
  const tree* t= &source;
  const property* p= nullptr;
  const auto stale= [] { return std::runtime_error ("Stale node location"); };
  for (const auto& part: where) {
    switch (part.kind) {
    case step_kind::child:
      if (!t || !is_compound (*t) || part.index >= std::size_t (N(*t))) throw stale ();
      t= &(*t)[int (part.index)]; break;
    case step_kind::property: {
      if (!t) throw stale ();
      const auto* m= node::get (*t);
      if (!m) throw stale ();
      auto found= m->properties.find (part.key);
      if (found == m->properties.end ()) throw stale ();
      p= &found->second; t= nullptr; break;
    }
    case step_kind::list_item: {
      const auto* values= p ? std::get_if<property::list> (&p->data) : nullptr;
      if (!values || part.index >= values->size ()) throw stale ();
      p= &(*values)[part.index]; break;
    }
    case step_kind::dictionary_item: {
      const auto* values= p ? std::get_if<property::dictionary> (&p->data) : nullptr;
      if (!values) throw stale ();
      auto found= values->find (part.key);
      if (found == values->end ()) throw stale ();
      p= &found->second; break;
    }
    case step_kind::rich_text: {
      const auto* rich= p ? std::get_if<node::rich_text> (&p->data) : nullptr;
      if (!rich) throw stale ();
      t= &rich->content; p= nullptr; break;
    }
    }
  }
  if (!t || !node::valid_id (expected_id) || node::id (*t) != expected_id) throw stale ();
  return copy (*t);
}

struct query::impl {
  std::mutex lock;
  const std::vector<std::string> ids, ancestry;
  const bool content;
  completion ready;
  snapshot answer= std::make_shared<const result> ();
  impl (std::vector<std::string> ids, std::vector<std::string> ancestry,
        bool content, completion ready):
    ids (std::move (ids)), ancestry (std::move (ancestry)), content (content), ready (std::move (ready)) {}
  void finish (result next) {
    completion notify;
    snapshot published;
    {
      std::lock_guard<std::mutex> guard (lock);
      if (answer->state != status::pending) return;
      answer= std::make_shared<const result> (std::move (next));
      published= answer;
      notify= std::move (ready);
    }
    if (notify) try { notify (std::move (published)); } catch (...) {}
  }
};
query::query (std::vector<std::string> ids, std::vector<std::string> ancestry,
              bool content, completion ready):
  data (std::make_shared<impl> (std::move (ids), std::move (ancestry), content, std::move (ready))) {}
snapshot query::read () const { std::lock_guard<std::mutex> guard (data->lock); return data->answer; }
result query::poll () const { return *read (); }
void query::cancel () { result stopped; stopped.state= status::cancelled; data->finish (std::move (stopped)); }

struct service::impl {
  const std::filesystem::path root;
  const live_provider live;
  const content_provider content;
  std::atomic<bool> stopping {false};
  std::mutex lock;
  std::condition_variable wake;
  std::deque<std::shared_ptr<query>> pending;
  std::uint64_t cache_epoch= 0;
  std::thread worker;
  impl (std::filesystem::path root, live_provider live, content_provider content):
    root (std::move (root)), live (std::move (live)), content (std::move (content)),
    worker ([this] { run (); }) {}
  ~impl () {
    { std::lock_guard<std::mutex> guard (lock); stopping= true; }
    wake.notify_all ();
    worker.join ();
    for (const auto& task: pending) task->cancel ();
  }
  void run () {
    std::map<std::string, std::shared_ptr<const source>> cache;
    std::unique_ptr<fs::confined_root> pinned_root;
    std::uint64_t epoch= 0, serial= 0;
    for (;;) {
      std::deque<std::shared_ptr<query>> batch;
      {
        std::unique_lock<std::mutex> guard (lock);
        wake.wait (guard, [&] { return stopping || !pending.empty (); });
        if (stopping) return;
        if (epoch != cache_epoch) { cache.clear (); epoch= cache_epoch; }
        batch.swap (pending);
      }
      if (std::all_of (batch.begin (), batch.end (), [] (const auto& task) {
            return task->poll ().state == status::cancelled;
          })) continue;
      sources documents;
      std::vector<diagnostic> errors;
      auto watched= std::make_shared<std::vector<std::string>> ();
      watched->push_back (root.string ());
      watched->push_back (root.parent_path ().string ());
      std::set<std::string> overridden, present;
      try {
        if (live) {
          for (auto& current: live (stopping)) {
            if (current.actor == 0 || (!current.file.empty () && !valid_file (current.file)))
              throw std::invalid_argument ("Invalid live source descriptor");
            if (!current.file.empty ()) overridden.insert (current.file);
            if (!current.error.empty ()) {
              errors.push_back ({current.file, current.error}); continue;
            }
            auto next= std::make_shared<source> ();
            next->file= std::move (current.file);
            next->actor= current.actor;
            next->capture= current.capture;
            next->nodes= std::move (current.nodes);
            documents.push_back (std::move (next));
          }
        }
        if (!pinned_root) pinned_root= std::make_unique<fs::confined_root> (root);
        const auto& directory= *pinned_root;
        std::set<std::pair<std::uint64_t, std::uint64_t>> directories;
        std::set<std::string> files;
        std::size_t entries= 0;
        std::function<void (const std::filesystem::path&, std::size_t)> visit;
        visit= [&] (const std::filesystem::path& relative, std::size_t depth) {
          if (stopping) return;
          auto name= relative.generic_string ();
          if (!relative.empty () && overridden.count (name)) return;
          try {
            if (depth > max_depth || ++entries > max_nodes)
              throw std::length_error ("Vault location inventory exceeds its budget");
            const auto entry= directory.open (relative);
            const auto physical= entry.path ().lexically_relative (directory.path ());
            if (internal_path (physical)) return;
            name= physical.generic_string ();
            if (overridden.count (name)) return;
            const auto revision= entry.stat ();
            if (revision.directory) {
              if (!directories.emplace (revision.device, revision.inode).second)
                return;
              watched->push_back (entry.path ().string ());
              auto names= entry.names ();
              std::sort (names.begin (), names.end ());
              for (const auto& child: names)
                if (!excluded (child)) visit (relative / child, depth + 1);
            }
            else if (relative.extension () == ".ath") {
              if (!files.insert (name).second) return;
              watched->push_back (entry.path ().string ());
              present.insert (name);
              const auto saved= cache.find (name);
              if (saved != cache.end () && fs::same_revision (*saved->second->disk_revision, revision)) {
                documents.push_back (saved->second); return;
              }
              const document::codec_limits limits;
              const auto bytes= entry.read (limits.input_bytes);
              auto source_tree= document::read_xml_v2 (bytes);
              auto next= std::make_shared<source> ();
              next->file= name;
              next->disk_revision= revision;
              next->nodes= collect (source_tree);
              const auto now= directory.open (relative);
              if (!entry.same_object (now) || !fs::same_revision (revision, now.stat ()))
                throw std::runtime_error ("Source changed during UUID inventory");
              cache[name]= next;
              documents.push_back (std::move (next));
            }
          }
          catch (const std::length_error&) { throw; }
          catch (const document::identity_conflict& e) {
            errors.push_back ({name, e.what (), status::conflict, e.id}); cache.erase (name);
          }
          catch (const std::exception& e) { errors.push_back ({name, e.what ()}); cache.erase (name); }
          catch (const string& e) { errors.push_back ({name, {e.data (), std::size_t (N(e))}}); cache.erase (name); }
        };
        visit ({}, 0);
      }
      catch (const std::exception& e) { errors.push_back ({"", e.what ()}); }
      catch (const string& e) { errors.push_back ({"", {e.data (), std::size_t (N(e))}}); }
      catch (...) { errors.push_back ({"", "Failed to capture source identity inventory"}); }
      for (auto i= cache.begin (); i != cache.end (); )
        if (!present.count (i->first)) i= cache.erase (i); else ++i;
      const auto locations= make_index (documents);
      ++serial;
      // Requests arriving during I/O share this captured inventory. Consumers
      // still validate its revision before using any returned node address.
      {
        std::lock_guard<std::mutex> guard (lock);
        if (epoch == cache_epoch) {
          batch.insert (batch.end (), pending.begin (), pending.end ());
          pending.clear ();
        }
      }
      for (const auto& task: batch) {
        if (stopping) task->cancel ();
        else if (task->read ()->state == status::pending) {
          auto answer= resolve (task->data->ids, task->data->ancestry, locations, errors, serial);
          answer.watched_paths= watched;
          if (task->data->content && answer.state != status::overlap)
            for (auto& target: answer.items) {
              if (stopping || task->read ()->state == status::cancelled) break;
              if (target.state != status::resolved) continue;
              try {
                if (!target.candidates.front ().file.empty ())
                  target.source_directory= (root / target.candidates.front ().file).parent_path ().string ();
                auto payload= content ? content (target, stopping) : read_disk_content (root, target);
                target.fragment_xml= std::move (payload.fragment_xml);
                target.preview_context_xml= std::move (payload.preview_context_xml);
                target.source_url= std::move (payload.source_url);
              }
              catch (const std::exception& e) {
                target.state= status::unreadable; target.diagnostic= e.what ();
              }
              catch (const string& e) {
                target.state= status::unreadable;
                target.diagnostic= {e.data (), std::size_t (N(e))};
              }
              catch (...) {
                target.state= status::unreadable; target.diagnostic= "Failed to read resolved node";
              }
              if (target.state != status::resolved && answer.state == status::resolved)
                answer.state= target.state;
            }
          if (stopping) task->cancel ();
          else task->data->finish (std::move (answer));
        }
      }
    }
  }
};

service::service (std::filesystem::path root, live_provider live, content_provider content):
  data (std::make_unique<impl> (std::move (root), std::move (live), std::move (content))) {}
service::~service ()= default;
std::shared_ptr<query> service::request (std::vector<std::string> ids,
                                       std::vector<std::string> ancestry,
                                       bool content, completion ready) {
  std::vector<std::string> unique;
  std::set<std::string> seen;
  for (auto& id: ids) if (seen.insert (id).second) unique.push_back (std::move (id));
  auto next= std::shared_ptr<query> (new query (std::move (unique), std::move (ancestry),
                                             content, std::move (ready)));
  if (next->data->ids.empty () || next->data->ids.size () > max_nodes ||
      next->data->ancestry.size () > max_depth ||
      !std::all_of (next->data->ids.begin (), next->data->ids.end (), node::valid_id) ||
      !std::all_of (next->data->ancestry.begin (), next->data->ancestry.end (), node::valid_id)) {
    result invalid;
    invalid.state= status::invalid;
    invalid.diagnostics.push_back ({"", "Expected a nonempty UUID selection and bounded UUID ancestry", status::invalid, {}});
    next->data->finish (std::move (invalid));
    return next;
  }
  { std::lock_guard<std::mutex> guard (data->lock); data->pending.push_back (next); }
  data->wake.notify_one ();
  return next;
}
void service::clear_cache () {
  std::lock_guard<std::mutex> guard (data->lock);
  ++data->cache_epoch;
}

static tree read_disk_source (const std::filesystem::path& root, const item& target) {
  if (target.state != status::resolved || target.candidates.size () != 1)
    throw std::invalid_argument ("Node target is not uniquely resolved");
  const auto& location= target.candidates.front ();
  if (location.actor || !location.disk_revision || !valid_file (location.file))
    throw std::invalid_argument ("Node target is not a saved vault source");
  fs::confined_root directory (root);
  const auto entry= directory.open (location.file);
  if (!fs::same_revision (*location.disk_revision, entry.stat ()))
    throw std::runtime_error ("Stale saved node location");
  auto document= document::read_xml_v2 (entry.read (document::codec_limits ().input_bytes));
  lookup (document, location.where, target.id);
  const auto now= directory.open (location.file);
  if (!entry.same_object (now) || !fs::same_revision (*location.disk_revision, now.stat ()))
    throw std::runtime_error ("Source changed while reading node target");
  return document;
}

tree read_disk (const std::filesystem::path& root, const item& target) {
  auto source= read_disk_source (root, target);
  return lookup (source, target.candidates.front ().where, target.id);
}

content_payload capture_content (const tree& source, const item& target, std::string source_url) {
  if (target.state != status::resolved || target.candidates.size () != 1)
    throw std::invalid_argument ("Node target is not uniquely resolved");
  const auto& where= target.candidates.front ().where;
  tree selected= lookup (source, where, target.id);
  tree context (DOCUMENT);
  if (is_document (source)) for (int i=0; i<N(source); ++i) {
    const auto& field= source[i];
    if (is_compound (field, "style", 1) || is_compound (field, "initial", 1))
      context << field;
    else if (is_compound (field, "body", 1) && is_document (field[0]) && N(field[0]) &&
             is_compound (field[0][0], "hide-preamble")) {
      // Do not duplicate the preamble when the selected root already contains it.
      bool includes_preamble= where.empty () ||
        (where[0].kind == step_kind::child && where[0].index == std::size_t (i) &&
         (where.size () == 1 ||
          (where[1].kind == step_kind::child && where[1].index == 0 &&
           (where.size () == 2 || (where[2].kind == step_kind::child && where[2].index == 0)))));
      if (!includes_preamble) context << compound ("body", tree (DOCUMENT, field[0][0]));
    }
  }
  return {document::write_xml_v2 (selected, document::xml_kind::fragment),
          document::write_xml_v2 (context, document::xml_kind::fragment), std::move (source_url)};
}

content_payload read_disk_content (const std::filesystem::path& root, const item& target) {
  auto source= read_disk_source (root, target);
  return capture_content (source, target, (root / target.candidates.front ().file).string ());
}
} // namespace athena::node_location
