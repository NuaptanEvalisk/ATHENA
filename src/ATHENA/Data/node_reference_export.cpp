/******************************************************************************
* MODULE     : node_reference_export.cpp
* DESCRIPTION: Native reference dependency preparation without actor waits
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "node_reference_export.hpp"
#include "node_metadata.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include <QCryptographicHash>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace athena::node_reference {
std::string export_source_revision (const tree& source) {
  const auto xml= document::write_xml_v2 (source, document::xml_kind::fragment);
  return QCryptographicHash::hash (QByteArrayView (xml.data (), xml.size ()),
                                   QCryptographicHash::Sha256).toHex ().toStdString ();
}

bool selection::operator < (const selection& other) const {
  return std::tie (ids, ancestry) < std::tie (other.ids, other.ancestry);
}
namespace {
thread_local export_reference_scope* active_export= nullptr;
struct collector {
  std::set<selection> found;
  std::vector<std::string> ancestry;
  std::size_t visited= 0;
  void budget (std::size_t depth) {
    if (depth > 256 || ++visited > 1000000)
      throw std::length_error ("Export reference inspection exceeds its budget");
  }
  void property (const node::property& value, std::size_t depth) {
    budget (depth);
    if (auto rich= std::get_if<node::rich_text> (&value.data)) visit (rich->content, depth+1);
    else if (auto list= std::get_if<node::property::list> (&value.data))
      for (const auto& item: *list) property (item, depth+1);
    else if (auto dict= std::get_if<node::property::dictionary> (&value.data))
      for (const auto& item: *dict) property (item.second, depth+1);
  }
  void visit (const tree& source, std::size_t depth) {
    budget (depth);
    if (canonical (source)) found.insert ({targets (source), ancestry});
    if (is_compound (source))
      for (int i=0; i<N(source); ++i) visit (source[i], depth+1);
    if (const auto* metadata= node::get (source))
      for (const auto& item: metadata->properties) property (item.second, depth+1);
  }
};
}

std::vector<selection> export_selections (const tree& source, std::vector<std::string> ancestry) {
  collector scan; scan.ancestry= std::move (ancestry); scan.visit (source, 0);
  return {scan.found.begin (), scan.found.end ()};
}

struct export_preparation::impl: std::enable_shared_from_this<impl> {
  std::shared_ptr<node_location::service> locator;
  const preparation_limits limits;
  mutable std::mutex lock;
  prepared_references collected;
  prepared_snapshot published;
  std::vector<std::shared_ptr<node_location::query>> queries;
  std::map<std::string, athena::filesystem::metadata> disk_revisions;
  std::size_t pending= 0, bytes= 0;
  completion done;
  impl (std::shared_ptr<node_location::service> service, completion callback, preparation_limits limits):
    locator (std::move (service)), limits (limits), done (std::move (callback)) {}

  void finish (std::string error= {}, bool cancelled= false) {
    completion callback;
    prepared_snapshot result;
    std::vector<std::shared_ptr<node_location::query>> release;
    {
      std::lock_guard<std::mutex> guard (lock);
      if (published || (error.empty () && !cancelled && pending)) return;
      collected.error= std::move (error); collected.cancelled= cancelled;
      published= std::make_shared<const prepared_references> (std::move (collected));
      result= published; callback= std::move (done); release.swap (queries);
    }
    for (const auto& query: release) query->cancel ();
    if (callback) try { callback (std::move (result)); } catch (...) {}
  }
  void schedule (std::vector<selection> requested) {
    std::vector<selection> added;
    {
      std::lock_guard<std::mutex> guard (lock);
      if (published) return;
      for (auto& next: requested) {
        // Use the same ordered-set contract as canonical source selections.
        std::set<std::string> seen;
        std::vector<std::string> unique;
        for (auto& id: next.ids) if (seen.insert (id).second) unique.push_back (std::move (id));
        next.ids= std::move (unique);
        if (collected.selections.count (next)) continue;
        if (collected.selections.size () >= limits.selections)
          throw std::length_error ("Export reference graph exceeds its selection budget");
        collected.selections.emplace (next, nullptr); ++pending;
        added.push_back (std::move (next));
      }
    }
    if (!added.empty () && !locator)
      throw std::runtime_error ("Export references require a vault locator");
    for (const auto& next: added) {
      auto weak= weak_from_this ();
      auto query= locator->request (next.ids, next.ancestry, true,
        [weak, next] (node_location::snapshot result) {
          if (auto self= weak.lock ()) self->accept (next, std::move (result));
        });
      bool obsolete;
      {
        std::lock_guard<std::mutex> guard (lock);
        obsolete= bool (published);
        if (!obsolete) queries.push_back (query);
      }
      if (obsolete) query->cancel ();
    }
  }
  void accept (const selection& selected, node_location::snapshot result) {
    try {
      {
        std::lock_guard<std::mutex> guard (lock);
        if (published) return;
        for (const auto& target: result->items) {
          for (const auto& candidate: target.candidates) if (candidate.disk_revision) {
            auto found= disk_revisions.emplace (candidate.file, *candidate.disk_revision);
            if (!found.second && !athena::filesystem::same_revision (found.first->second, *candidate.disk_revision))
              throw std::runtime_error ("Source changed during export reference preparation");
          }
          for (const auto* part: {&target.fragment_xml, &target.preview_context_xml, &target.source_url}) {
            if (part->size () > limits.content_bytes-bytes)
              throw std::length_error ("Export reference graph exceeds its content budget");
            bytes+= part->size ();
          }
        }
      }
      if (result->state == node_location::status::cancelled) {
        finish ("Export reference preparation cancelled", true); return;
      }
      std::vector<selection> dependencies;
      if (result->state != node_location::status::overlap)
        for (const auto& target: result->items)
          if (target.state == node_location::status::resolved && !target.fragment_xml.empty ()) {
            auto fragment= document::read_xml_v2 (target.fragment_xml, document::xml_kind::fragment);
            auto lineage= selected.ancestry; lineage.push_back (target.id);
            auto nested= export_selections (fragment, std::move (lineage));
            dependencies.insert (dependencies.end (), nested.begin (), nested.end ());
          }
      // Register descendants before retiring the parent; a synchronous invalid
      // query completion must not publish an incomplete graph.
      schedule (std::move (dependencies));
      {
        std::lock_guard<std::mutex> guard (lock);
        if (published) return;
        collected.selections.at (selected)= std::move (result); --pending;
      }
      finish ();
    }
    catch (const std::exception& e) { finish (e.what ()); }
    catch (const string& e) { finish ({e.data (), std::size_t (N(e))}); }
    catch (...) { finish ("Failed to prepare export references"); }
  }
};

export_preparation::export_preparation (std::shared_ptr<node_location::service> locator,
  std::vector<selection> seeds, completion done, preparation_limits limits):
  data (std::make_shared<impl> (std::move (locator), std::move (done), limits)) {
  try { data->schedule (std::move (seeds)); data->finish (); }
  catch (const std::exception& e) { data->finish (e.what ()); }
}
export_preparation::~export_preparation () { cancel (); }
prepared_snapshot export_preparation::read () const {
  std::lock_guard<std::mutex> guard (data->lock); return data->published;
}
void export_preparation::cancel () { data->finish ("Export reference preparation cancelled", true); }

export_reference_scope::export_reference_scope (prepared_snapshot prepared):
  previous (active_export), snapshot (std::move (prepared)) {
  if (!snapshot && previous) snapshot= previous->snapshot;
  if (snapshot && (snapshot->cancelled || !snapshot->error.empty ()))
    throw std::runtime_error (snapshot->error.empty () ? "Export preparation cancelled" : snapshot->error);
  active_export= this;
}
export_reference_scope::~export_reference_scope () {
  if (previous && (incomplete_child || !absent.empty ())) previous->incomplete_child= true;
  active_export= previous;
}
std::vector<selection> export_reference_scope::missing () const { return {absent.begin (), absent.end ()}; }
void export_reference_scope::require_ready () const {
  if (incomplete_child || !absent.empty ())
    throw std::runtime_error ("Export references are not prepared; refusing incomplete output");
}
std::optional<view> export_reference_view (const selection& selected) {
  if (!active_export) return {};
  if (active_export->snapshot) {
    const auto found= active_export->snapshot->selections.find (selected);
    if (found != active_export->snapshot->selections.end () && found->second &&
        found->second->state != node_location::status::pending)
      return view {found->second, found->second->scan};
  }
  active_export->absent.insert (selected);
  return view {};
}
prepared_snapshot current_export_references () {
  return active_export ? active_export->snapshot : prepared_snapshot {};
}
} // namespace athena::node_reference
