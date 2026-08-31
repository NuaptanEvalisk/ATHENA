/******************************************************************************
* MODULE     : materials_document.cpp
* DESCRIPTION: Materials document AST rendering and tmfs pages
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "ATHENA/Data/materials_document.hpp"

#include "ATHENA/Data/materials_engine.hpp"
#include "ATHENA/Data/vault.hpp"
#include "convert.hpp"
#include "scheme.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace {

string
tm_text (const std::string& utf8) {
  return utf8_to_cork (string (utf8.data (), utf8.size ()));
}

tree
text (const std::string& utf8) {
  return tree (tm_text (utf8));
}

std::string
stdstr (tree value) {
  if (!is_atomic (value)) return {};
  return std::string (as_charp (value->label), N(value->label));
}

tree
html_tree (const std::string& html) {
  tree converted= generic_to_tree (string (html.c_str (), html.size ()),
                                   "html-snippet");
  if (is_func (converted, DOCUMENT, 1)) return converted[0];
  return converted;
}

void
collect_nodes (tree node, std::vector<tree>& citations,
               std::vector<tree>& bibliographies) {
  if (is_compound (node, "material-citation", 2)) citations.push_back (node);
  else if (is_compound (node, "referenced-materials", 3))
    bibliographies.push_back (node);
  for (int i=0; i<N(node); ++i) collect_nodes (node[i], citations, bibliographies);
}

std::vector<std::string>
manual_uuids (tree bibliography) {
  std::vector<std::string> result;
  if (!is_compound (bibliography, "referenced-materials", 3) ||
      !is_func (bibliography[1], TUPLE)) return result;
  for (int i=0; i<N(bibliography[1]); ++i) {
    std::string uuid= stdstr (bibliography[1][i]);
    if (!uuid.empty ()) result.push_back (uuid);
  }
  return result;
}

tree
material_field_row (const char* label, const std::string& value) {
  tree content (CONCAT);
  content << compound ("strong", tree ((std::string (label) + ": ").c_str ()))
          << text (value);
  return compound ("paragraph*", content);
}

tree
material_document (tree body) {
  tree document (DOCUMENT);
  document << compound ("TeXmacs", TEXMACS_COMPAT_VERSION)
           << compound ("style", tuple ("generic"))
           << compound ("body", body);

  string font= get_preference ("vault preferred font", "");
  if (font != "") {
    tree initial (COLLECTION);
    initial << compound ("associate", "font", font)
            << compound ("associate", "font-family", "rm");
    document << compound ("initial", initial);
  }
  return document;
}

} // namespace

std::string
athena_materials_document_citation_style (
    const tree& document, const std::string& fallback_style) {
  for (int i=0; i<N(document); ++i) {
    tree initial= document[i];
    if (!is_compound (initial, "initial", 1)) continue;
    tree attributes= initial[0];
    if (!is_func (attributes, COLLECTION)) continue;
    for (int j=0; j<N(attributes); ++j) {
      tree entry= attributes[j];
      if (is_compound (entry, "associate", 2) &&
          stdstr (entry[0]) == "materials-csl-style") {
        std::string style= stdstr (entry[1]);
        if (!style.empty ()) return style;
      }
    }
  }
  return fallback_style.empty () ? "springer-mathphys" : fallback_style;
}

tree
athena_materials_update_document (tree document,
                                  const std::string& default_style,
                                  std::string& error) {
  error.clear ();
  MaterialsStore* store= vault_get_materials_store ();
  if (store == nullptr) { error= "no active Materials database"; return document; }
  tree updated= copy (document);
  std::vector<tree> citation_nodes;
  std::vector<tree> bibliography_nodes;
  collect_nodes (updated, citation_nodes, bibliography_nodes);
  if (citation_nodes.empty () && bibliography_nodes.empty ()) return updated;

  std::vector<MaterialCitationCluster> clusters;
  std::set<std::string> requested;
  for (tree citation: citation_nodes) {
    MaterialCitationCluster cluster;
    if (is_func (citation[0], TUPLE))
      for (int i=0; i<N(citation[0]); ++i) {
        tree item= citation[0][i];
        if (!is_compound (item, "material-cite-item", 3)) continue;
        std::string uuid= store->resolve_uuid (stdstr (item[0]), error);
        if (!error.empty () || uuid.empty ()) {
          if (error.empty ()) error= "unresolved Material UUID: " + stdstr (item[0]);
          return document;
        }
        item[0]= tree (uuid.c_str ());
        requested.insert (uuid);
        cluster.items.push_back ({uuid, stdstr (item[1]), stdstr (item[2]), false});
      }
    clusters.push_back (std::move (cluster));
  }
  std::vector<std::vector<std::string>> bibliography_manual;
  for (tree bibliography: bibliography_nodes) {
    std::vector<std::string> canonical_manual;
    for (const std::string& uuid: manual_uuids (bibliography)) {
      std::string canonical= store->resolve_uuid (uuid, error);
      if (!error.empty () || canonical.empty ()) {
        if (error.empty ()) error= "unresolved manual Material UUID: " + uuid;
        return document;
      }
      requested.insert (canonical);
      canonical_manual.push_back (canonical);
    }
    tree normalized (TUPLE);
    for (const std::string& uuid: canonical_manual)
      normalized << tree (uuid.c_str ());
    bibliography[1]= normalized;
    bibliography_manual.push_back (std::move (canonical_manual));
  }

  std::vector<MaterialRecord> records;
  for (const std::string& uuid: requested) {
    std::optional<MaterialRecord> record= store->get (uuid, error);
    if (!record) return document;
    records.push_back (*record);
  }
  MaterialRenderedDocument rendered;
  if (!athena_materials_render (records, clusters, {},
                                default_style.empty () ? "springer-mathphys"
                                                       : default_style,
                                rendered, error)) return document;
  if (rendered.citation_html.size () < citation_nodes.size ()) {
    error= "Hayagriva returned fewer citation clusters than requested";
    return document;
  }
  for (size_t i=0; i<citation_nodes.size (); ++i) {
    tree content= html_tree (rendered.citation_html[i]);
    tree previous= citation_nodes[i][1];
    if (is_compound (previous, "hlink", 2) && is_atomic (previous[1])) {
      std::string destination= stdstr (previous[1]);
      if (destination.rfind ("tmfs://material/", 0) == 0)
        content= compound ("hlink", content, previous[1]);
    }
    citation_nodes[i][1]= content;
  }

  for (size_t bibliography_index=0;
       bibliography_index<bibliography_nodes.size (); ++bibliography_index) {
    tree bibliography= bibliography_nodes[bibliography_index];
    std::string style= stdstr (bibliography[0]);
    if (style.empty ()) style= default_style.empty () ? "springer-mathphys"
                                                      : default_style;
    const std::vector<std::string>& manual=
      bibliography_manual[bibliography_index];
    MaterialRenderedDocument bibliography_rendered;
    if (!athena_materials_render (records, clusters, manual, style,
                                  bibliography_rendered, error)) return document;
    tree rows (DOCUMENT);
    for (const MaterialRenderedBibliographyItem& item:
         bibliography_rendered.bibliography) {
      std::string uri= "tmfs://material/" + item.uuid;
      rows << compound (
        "paragraph*",
        compound ("hlink", html_tree (item.html), tree (uri.c_str ())));
    }
    bibliography[2]= rows;
  }
  return updated;
}

tree
athena_material_info_page (const std::string& name) {
  size_t query= name.find ('?');
  std::string uuid= name.substr (0, query);
  MaterialsStore* store= vault_get_materials_store ();
  std::string error;
  std::optional<MaterialRecord> material=
    store == nullptr ? std::nullopt : store->get (uuid, error);
  tree body (DOCUMENT);
  if (!material) {
    body << compound ("section*", tree ("Material not found"));
    body << compound (
      "paragraph*",
      text (error.empty () ? "The Material UUID is not available in this vault."
                           : error));
  }
  else {
    std::string title= material->field ("title");
    if (title.empty ()) title= "Untitled Material";
    body << compound ("section*", text (title));
    body << material_field_row ("UUID", material->uuid);
    body << material_field_row ("Type", material->item_type);
    std::string creators;
    for (const MaterialCreator& creator: material->creators) {
      if (!creators.empty ()) creators += "; ";
      creators += creator.literal.empty ()
        ? creator.given + (creator.given.empty () || creator.family.empty () ? "" : " ") +
          creator.family
        : creator.literal;
    }
    if (!creators.empty ()) body << material_field_row ("Creators", creators);
    if (!material->field ("date").empty ())
      body << material_field_row ("Date", material->field ("date"));
    for (const MaterialIdentifier& identifier: material->identifiers)
      body << material_field_row (identifier.scheme.c_str (), identifier.value);
    std::vector<MaterialAttachment> attachments= store->attachments (material->uuid, error);
    if (!attachments.empty ()) {
      body << compound ("subsection*", tree ("Attachments"));
      for (const MaterialAttachment& attachment: attachments) {
        std::string target= (store->vault_root () /
                             std::filesystem::u8path (attachment.stored_path)).string ();
        body << compound (
          "paragraph*",
          compound ("hlink", text (attachment.canonical_name),
                    text (target)));
      }
    }
  }
  return material_document (body);
}
