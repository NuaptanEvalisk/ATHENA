/******************************************************************************
* MODULE     : artifact_document.cpp
* DESCRIPTION: TMFS documents for navigating semantic artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "ATHENA/Data/artifact_document.hpp"

#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/vault.hpp"
#include "System/Boot/boot.hpp"
#include "convert.hpp"
#include "scheme.hpp"
#include "wencoding.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs= std::filesystem;

namespace {

std::string to_std (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

string internal_text (const std::string& value) {
  string raw (value.data (), (int) value.size ());
  bool universal= false;
  for (int i=0; i+1<N(raw); ++i)
    if (raw[i] == '<' && raw[i+1] == '#') { universal= true; break; }
  return looks_utf8 (raw) && !universal ? utf8_to_cork (raw) : raw;
}

tree artifact_document (tree body, string preferred_font) {
  tree document (DOCUMENT);
  document << compound ("TeXmacs", TEXMACS_COMPAT_VERSION)
           << compound ("style", tuple ("generic"))
           << compound ("body", body);
  if (preferred_font != "") {
    tree initial (COLLECTION);
    initial << compound ("associate", "font", preferred_font)
            << compound ("associate", "font-family", "rm");
    document << compound ("initial", initial);
  }
  return document;
}

tree error_document (const char* title, const std::string& message,
                     string preferred_font) {
  tree body (DOCUMENT);
  body << compound ("section*", tree (title))
       << compound ("paragraph*", tree (internal_text (message)));
  return artifact_document (body, preferred_font);
}

} // namespace

tree
athena_artifact_disambiguation_document (
  const std::vector<AthenaArtifactRecord>& records, string preferred_font) {
  if (records.empty ())
    return error_document ("Artifact not found",
      "None of the candidate artifacts is available in this vault.",
      preferred_font);

  string term= athena_artifact_radioactive_name (records.front ());
  if (term == "") term= "Artifact";
  tree body (DOCUMENT);
  body << compound ("section*", term);
  tree introduction (CONCAT);
  introduction << "The term " << compound ("strong", term)
               << " refers to more than one artifact in this vault. "
                  "Select the intended one:";
  body << compound ("paragraph*", introduction);

  tree items (DOCUMENT);
  for (const AthenaArtifactRecord& record: records) {
    string name= athena_artifact_radioactive_name (record);
    if (name == "") name= internal_text (record.display_text);
    if (name == "") name= "Untitled artifact";
    string destination= "tmfs://artifact/" *
      string (record.uuid.data (), (int) record.uuid.size ());
    tree row (CONCAT);
    row << compound ("item")
        << compound ("hlink", compound ("strong", name), destination)
        << " (" << internal_text (record.type) << ")";
    string description= internal_text (record.display_text);
    if (description != "" && description != name)
      row << " - " << description;
    row << " [" << compound ("samp", internal_text (record.relative_path))
        << "]";
    items << row;
  }
  body << compound ("itemize", items);
  return artifact_document (body, preferred_font);
}

tree
athena_artifact_disambiguation_page (string disambiguation_key) {
  string font= get_preference ("vault preferred font", "");
  if (!vault_active ())
    return error_document ("Artifact disambiguation",
                           "No vault is currently active.", font);
  std::string key= to_std (disambiguation_key);
  if (key.size () != 64 ||
      !std::all_of (key.begin (), key.end (), [] (unsigned char c) {
        return std::isxdigit (c) != 0;
      }))
    return error_document ("Artifact disambiguation",
                           "The artifact disambiguation key is invalid.", font);
  fs::path root (to_std (concretize (vault_get_root ())));
  std::vector<AthenaArtifactRecord> all_records;
  std::string error;
  if (!athena_artifacts_query (root, all_records, error))
    return error_document ("Artifact disambiguation", error, font);
  std::vector<AthenaArtifactRecord> records;
  for (const AthenaArtifactRecord& record: all_records)
    if (athena_artifact_radioactive_key (record) == key)
      records.push_back (record);
  if (records.size () < 2)
    return error_document (
      "Artifact disambiguation",
      "This name no longer refers to multiple artifacts in the active vault.",
      font);
  return athena_artifact_disambiguation_document (records, font);
}
