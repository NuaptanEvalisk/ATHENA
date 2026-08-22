/******************************************************************************
* MODULE     : materials_zotero.hpp
* DESCRIPTION: Zotero Local API import mapping for ATHENA Materials
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_MATERIALS_ZOTERO_HPP
#define ATHENA_MATERIALS_ZOTERO_HPP

#include "ATHENA/Data/materials.hpp"

#include <string>
#include <vector>

struct ZoteroAttachmentDescriptor {
  std::string item_key;
  std::string parent_key;
  std::string title;
  std::string filename;
  std::string content_type;
  std::string link_mode;
};

struct ZoteroMaterialImport {
  std::string item_key;
  std::string source_reference;
  MaterialRecord material;
  std::vector<ZoteroAttachmentDescriptor> attachments;
};

struct ZoteroParseSummary {
  int bibliographic_items= 0;
  int standalone_attachments= 0;
  int child_attachments= 0;
  int ignored_notes= 0;
  int ignored_annotations= 0;
};

bool athena_materials_parse_zotero_items (
  const std::string& json, const std::string& server_id,
  const std::string& library_prefix,
  std::vector<ZoteroMaterialImport>& imports,
  ZoteroParseSummary& summary, std::string& error);

#endif // ATHENA_MATERIALS_ZOTERO_HPP
