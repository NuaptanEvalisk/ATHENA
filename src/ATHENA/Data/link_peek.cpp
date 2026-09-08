/******************************************************************************
* MODULE     : link_peek.cpp
* DESCRIPTION: Resolve link previews without navigating or sharing editors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "link_peek.hpp"
#include "artifact_document.hpp"
#include "vault.hpp"
#include "vault_map_sqlite.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "QTMVaultAnchorModel.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include <QUrl>
#include <filesystem>

namespace {
QUrl parsed_target (string target) {
  return QUrl (QString::fromUtf8 (as_charp (target), N(target)));
}
string native (const QString& text) {
  QByteArray bytes= text.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}
std::string bytes (string text) { return std::string (as_charp (text), N(text)); }
tree unavailable (string message) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")),
               compound ("body", tree (DOCUMENT, message)));
}
tree with_preview_body (tree document, tree preview, path focus) {
  tree body= extract (document, "body");
  tree result= copy (document);
  if (is_document (body) && N(body) &&
      is_compound (body[0], "hide-preamble") &&
      !is_nil (focus) && focus->item != 0) {
    tree with_preamble (DOCUMENT, copy (body[0]));
    with_preamble << A(preview);
    preview= with_preamble;
  }
  for (int i=0; i<N(result); ++i)
    if (is_compound (result[i], "body", 1)) result[i][0]= preview;
  return result;
}
}

bool athena_link_peek_target (string target) {
  QUrl parsed= parsed_target (target);
  return parsed.scheme () == "tmfs" &&
    (parsed.host () == "wikilink" || parsed.host () == "artifact-disambiguation" ||
     parsed.host () == "artifact");
}

tree athena_link_peek_range (tree document, string begin, string end) {
  tree body= extract (document, "body");
  std::vector<WikilinkAnchorEntry> anchors;
  collect_anchors (body, path (), anchors);
  path first, last;
  bool have_first= false, have_last= false;
  for (const auto& anchor: anchors) {
    if (!have_first && native (anchor.anchor) == begin) {
      first= anchor.where; have_first= true;
    }
    if (!have_last && native (anchor.anchor) == end) {
      last= anchor.where; have_last= true;
    }
  }
  tree preview;
  if (begin == "" && end == "") {
    first= path (0);
    preview= build_preview_from_body (body, first);
  }
  else if (begin == "" && have_last) {
    first= last;
    preview= build_preview_from_body (body, last);
  }
  else if (end == "" && have_first)
    preview= build_preview_from_body (body, first);
  else {
    if (!have_first || !have_last || is_nil (first) || is_nil (last) ||
        first->item > last->item)
      return unavailable ("Preview unavailable: source anchors were not found.");
    preview= build_preview_from_anchor_range (body, first, last);
  }
  return with_preview_body (document, preview, first);
}

tree athena_link_peek_document (string target, url& source) {
  source= url_none ();
  if (!athena_link_peek_target (target)) return tree (UNINIT);
  QUrl parsed= parsed_target (target);
  QString key= parsed.path (QUrl::FullyEncoded).section ('/', 1, 1);
  key= QUrl::fromPercentEncoding (key.toUtf8 ());
  if (parsed.host () == "artifact-disambiguation")
    return athena_artifact_disambiguation_page (native (key));
  vault_info vault= vault_get_snapshot ();
  if (is_none (vault.root)) return unavailable ("Preview unavailable: no Vault is open.");

  // The GUI's current_vault_map is not actor-owned. Use a local connection
  // against the published vault paths, as other background readers do.
  namespace fs= std::filesystem;
  fs::path root (bytes (concretize (vault.root)));
  AthenaVaultMapSqlite map;
  AthenaVaultMapNode node;
  std::string error;
  bool found= false;
  AthenaArtifactRecord artifact;
  bool artifact_target= parsed.host () == "artifact";
  if (artifact_target) {
    if (!athena_artifact_radioactive_record (bytes (native (key)), artifact))
      return unavailable ("Preview unavailable: artifact was not found.");
    node.path= artifact.relative_path;
  }
  else if (!map.open_read_only (fs::path (bytes (concretize (vault.db_url))), error) ||
           !map.get_node (bytes (native (key)), node, found, error) || !found)
    return unavailable ("Preview unavailable: link target was not found.");
  fs::path relative= fs::path (node.path).lexically_normal ();
  if (relative.empty () || relative.is_absolute () || *relative.begin () == "..")
    return unavailable ("Preview unavailable: invalid source path.");
  source= url_system (string ((root / relative).string ().c_str ()));
  string serialized;
  if (load_string (source, serialized, false))
    return unavailable ("Preview unavailable: cannot read source.");
  // Native read, not import_tree: import also changes conversion focus and
  // registers links globally, neither of which belongs to a hover operation.
  tree document= texmacs_document_to_tree (serialized);
  if (!is_document (document)) return unavailable ("Preview unavailable: cannot read source.");
  if (artifact_target) {
    path focus;
    if (!athena_artifact_locate_source (document, artifact, focus, error))
      return unavailable ("Preview unavailable: artifact source has changed.");
    return with_preview_body (document,
      build_preview_from_body (extract (document, "body"), focus), focus);
  }
  return athena_link_peek_range (document, string (node.anchor_begin.c_str ()),
                               string (node.anchor_end.c_str ()));
}
