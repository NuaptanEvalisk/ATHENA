/******************************************************************************
* MODULE     : node_reference.cpp
* DESCRIPTION: Ordered UUID transclusion presentation and expansion ancestry
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "node_reference.hpp"
#include "node_metadata.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "file.hpp"
#include "convert.hpp"
#include <filesystem>
#include <set>
#include <map>
#include <QUrl>

namespace athena::node_reference {
namespace {
string native (const std::string& s) { return string (s.data (), int (s.size ())); }
tree message (const std::string& text) { return tree (WITH, "color", "#a04040", native (text)); }
tree presentation (const tree& source, const url& base) {
  tree out= is_atomic (source) ? tree (source->label) : tree (L(source), N(source));
  if (is_compound (source))
    for (int i=0; i<N(source); ++i) out[i]= presentation (source[i], base);
  // Render copies retain semantic attributes, never persistent source IDs or
  // artifact bindings. Relative image paths belong to the source document.
  if (const auto* m= node::get (source)) {
    auto props= *m; props.id.clear ();
    props.properties.erase ("athena:artifact-bindings");
    node::set (out, props);
  }
  if (is_func (out, IMAGE) && N(out) > 0 && is_atomic (out[0]) && !is_none (base)) {
    url name= url_system (out[0]->label);
    if (!is_rooted (name) && !is_none (name)) out[0]= as_string (base * name);
  }
  return out;
}
}
bool canonical (const tree& t) { return is_func (t, TRANSCLUDE, 1) && is_tuple (t[0]); }
tree presentation_copy (const tree& source, const url& base) {
  return presentation (node::content_projection (source), base);
}
std::vector<std::string> targets (const tree& t) {
  if (!canonical (t) || N(t[0]) == 0) throw std::invalid_argument ("Expected a nonempty node UUID list");
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (int i=0; i<N(t[0]); ++i) {
    if (!is_atomic (t[0][i])) throw std::invalid_argument ("Node target must be a UUID");
    const auto& text= t[0][i]->label;
    std::string id (text.data (), std::size_t (N(text)));
    if (!node::valid_id (id)) throw std::invalid_argument ("Invalid node target UUID");
    if (seen.insert (id).second) out.push_back (std::move (id));
  }
  return out;
}
std::vector<std::string> ancestry (const tree& value) {
  if (!is_tuple (value)) return {};
  std::vector<std::string> out;
  for (int i=0; i<N(value); ++i) if (is_atomic (value[i])) {
    const auto& text= value[i]->label;
    std::string id (text.data (), std::size_t (N(text)));
    if (node::valid_id (id)) out.push_back (std::move (id));
  }
  return out;
}

std::string target_id (const string& text) {
  const QUrl url (QString::fromUtf8 (text.data (), N(text)), QUrl::StrictMode);
  if (!url.isValid () || url.scheme () != "tmfs" || !url.query ().isEmpty () ||
      !url.fragment ().isEmpty () || !url.userInfo ().isEmpty () || url.port () != -1) return {};
  const auto path= url.path (QUrl::FullyDecoded);
  const auto id= path.section ('/', 1, 1).toStdString ();
  if (!node::valid_id (id)) return {};
  if (url.host () == "transclude") return id;
  if (url.host () == "wikilink" && path == "/" + QString::fromStdString (id)) return id;
  return {};
}

static tree compute_display (const view& current) {
  using node_location::status;
  if (!current.snapshot || current.snapshot->state == status::pending)
    return tree (DOCUMENT, "Locating referenced nodes...");
  const auto& result= *current.snapshot;
  if (result.state == status::overlap)
    return tree (DOCUMENT, message ("Invalid transclusion: ancestor and descendant selected together."));
  tree out (DOCUMENT);
  for (const auto& target: result.items) {
    if (target.state != status::resolved || target.fragment_xml.empty ()) {
      const char* label= "Unavailable node";
      switch (target.state) {
      case status::missing: label= "Missing node"; break;
      case status::conflict: label= "Conflicting node identity"; break;
      case status::cycle: label= "Cyclic reference"; break;
      case status::unreadable: label= "Unreadable node"; break;
      default: break;
      }
      out << message (std::string (label) + " " + target.id + ": " + target.diagnostic);
      continue;
    }
    try {
      auto node= document::read_xml_v2 (target.fragment_xml, document::xml_kind::fragment);
      if (target.candidates.size () == 1 && target.candidates[0].where.empty () && is_document (node))
        for (int i=0; i<N(node); ++i) if (is_compound (node[i], "body", 1)) {
          node= node[i][0]; break;
        }
      node= presentation (node::content_projection (node), target.source_directory.empty () ?
        url_none () : url_system (native (target.source_directory)));
      tree content= is_document (node) ? node : tree (DOCUMENT, node);
      const string source= native ("tmfs://transclude/" + target.id);
      tree lineage (TUPLE);
      for (const auto& id: result.ancestry) lineage << native (id);
      lineage << native (target.id);
      out << tree (WITH, ancestry_variable, lineage,
        tree (DOCUMENT, tree (HLINK, "Source", source), content));
    }
    catch (const std::exception& e) { out << message (e.what ()); }
  }
  if (N(out) == 0) {
    std::string text= "Reference is unavailable";
    if (!result.diagnostics.empty ()) text+= ": " + result.diagnostics.front ().message;
    out << message (text);
  }
  return out;
}

tree display (const view& current) {
  if (!current.snapshot) return compute_display (current);
  struct cached {
    node_location::snapshot source;
    tree rendered;
    std::uint64_t used;
  };
  // Native trees remain owner-local; only immutable wire snapshots are shared.
  static thread_local std::map<const node_location::result*, cached> cache;
  static thread_local std::uint64_t clock= 0;
  const auto key= current.snapshot.get ();
  auto found= cache.find (key);
  if (found != cache.end ()) { found->second.used= ++clock; return found->second.rendered; }
  tree rendered= compute_display (current);
  if (cache.size () >= 128) {
    auto oldest= cache.begin ();
    for (auto i= cache.begin (); i != cache.end (); ++i)
      if (i->second.used < oldest->second.used) oldest= i;
    cache.erase (oldest);
  }
  cache.emplace (key, cached {current.snapshot, rendered, ++clock});
  return rendered;
}

static void merge_preview_context (const node_location::item& target, tree& out,
                                   tree& preamble, tree* nearby= nullptr) {
  if (target.preview_context_xml.empty ()) return;
  auto context= document::read_xml_v2 (
    target.preview_context_xml, document::xml_kind::fragment);
  context= presentation (node::content_projection (context),
    target.source_directory.empty () ? url_none () :
                                        url_system (native (target.source_directory)));
  for (int i=0; i<N(context); ++i) {
    if (is_compound (context[i], "style", 1)) out[0]= context[i];
    else if (is_compound (context[i], "initial", 1)) out << context[i];
    else if (is_compound (context[i], "body", 1) && is_document (context[i][0]))
      for (int j=0; j<N(context[i][0]); ++j) {
        const tree& child= context[i][0][j];
        if (is_compound (child, "hide-preamble", 1) ||
            is_compound (child, "show-preamble", 1))
          preamble << child;
        else if (nearby) *nearby << child;
      }
  }
}

tree preview_document (const view& current, url& source) {
  source= url_none ();
  tree out (DOCUMENT, compound ("style", tree (TUPLE, "generic"))), preamble (DOCUMENT);
  if (current.snapshot && current.snapshot->items.size () == 1) {
    const auto& target= current.snapshot->items.front ();
    if (target.state == node_location::status::resolved) {
      if (!target.source_url.empty ()) source= url (native (target.source_url));
      merge_preview_context (target, out, preamble);
    }
  }
  preamble << A(display (current));
  out << compound ("body", preamble);
  return out;
}

tree preview_context_document (const view& current, url& source) {
  source= url_none ();
  tree out (DOCUMENT, compound ("style", tree (TUPLE, "generic")));
  tree body (DOCUMENT), nearby (DOCUMENT);
  if (current.snapshot && current.snapshot->items.size () == 1) {
    const auto& target= current.snapshot->items.front ();
    if (target.state == node_location::status::resolved) {
      if (!target.source_url.empty ()) source= url (native (target.source_url));
      merge_preview_context (target, out, body, &nearby);
      if (N(nearby) > 0) {
        tree lineage (TUPLE);
        for (const auto& id: current.snapshot->ancestry) lineage << native (id);
        lineage << native (target.id);
        body << tree (WITH, ancestry_variable, lineage, nearby);
      }
      else body << message ("No nearby source context is available.");
    }
    else body << A(display (current));
  }
  else body << A(display (current));
  out << compound ("body", body);
  return out;
}
} // namespace athena::node_reference
