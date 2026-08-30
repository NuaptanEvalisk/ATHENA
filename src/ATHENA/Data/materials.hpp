/******************************************************************************
* MODULE     : materials.hpp
* DESCRIPTION: Vault-native Materials database and managed attachments
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_MATERIALS_HPP
#define ATHENA_MATERIALS_HPP

#include "ATHENA/Data/vaultfile_json.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct MaterialField {
  std::string name;
  std::string value;
  std::string language;
  int ordinal= 0;
};

struct MaterialCreator {
  std::string role= "author";
  std::string given;
  std::string family;
  std::string literal;
  std::string suffix;
  int ordinal= 0;
};

struct MaterialIdentifier {
  std::string scheme;
  std::string value;
  std::string normalized_value;
};

struct MaterialProvenance {
  std::string field_name;
  std::string source_kind;
  std::string source_reference;
  std::string observed_value;
  double confidence= 1.0;
};

struct MaterialRecord {
  std::string uuid;
  std::string item_type= "document";
  std::string review_state= "ready";
  std::string extra_json= "{}";
  std::int64_t revision= 0;
  std::int64_t created_at= 0;
  std::int64_t updated_at= 0;
  std::vector<MaterialField> fields;
  std::vector<MaterialCreator> creators;
  std::vector<MaterialIdentifier> identifiers;
  std::vector<std::string> tags;
  std::vector<MaterialProvenance> provenance;

  std::string field (const std::string& name) const;
};

struct MaterialAttachment {
  std::string uuid;
  std::string material_uuid;
  std::string role= "document";
  std::string stored_path;
  std::string original_name;
  std::string canonical_name;
  std::string mime_type;
  std::string sha256;
  std::int64_t byte_size= 0;
  bool primary= false;
  std::int64_t created_at= 0;
};

struct MaterialImportResult {
  MaterialAttachment attachment;
  bool duplicate= false;
  std::string existing_material_uuid;
};

struct MaterialSearchHit {
  std::string uuid;
  std::string item_type;
  std::string title;
  std::string creators;
  std::string issued;
  std::string review_state;
  double rank= 0.0;
};

struct MaterialRelation {
  std::string subject_uuid;
  std::string relation;
  std::string object_uuid;
};

struct MaterialMetadataConflict {
  std::string field;
  std::string existing_value;
  std::string incoming_value;
};

struct MaterialMetadataReconciliation {
  MaterialRecord prefer_existing;
  MaterialRecord prefer_incoming;
  std::vector<MaterialMetadataConflict> conflicts;

  bool compatible () const { return conflicts.empty (); }
};

MaterialMetadataReconciliation athena_materials_reconcile_metadata (
  const MaterialRecord& existing, const MaterialRecord& incoming);
MaterialRecord athena_materials_replace_metadata (
  const MaterialRecord& existing, const MaterialRecord& incoming);

class MaterialsStore {
public:
  MaterialsStore ();
  ~MaterialsStore ();
  MaterialsStore (MaterialsStore&&) noexcept;
  MaterialsStore& operator= (MaterialsStore&&) noexcept;

  MaterialsStore (const MaterialsStore&)= delete;
  MaterialsStore& operator= (const MaterialsStore&)= delete;

  bool open (const std::filesystem::path& vault_root,
             const AthenaVaultfileInfo& vault_info, std::string& error);
  void close ();
  bool is_open () const;

  const std::filesystem::path& vault_root () const;
  const std::filesystem::path& database_path () const;
  const std::filesystem::path& materials_directory () const;

  bool create (MaterialRecord& material, std::string& error);
  bool update (MaterialRecord& material, std::int64_t expected_revision,
               std::string& error);
  bool remove (const std::string& uuid, bool remove_managed_files,
               std::string& error);
  std::optional<MaterialRecord> get (const std::string& uuid,
                                     std::string& error) const;
  std::vector<MaterialSearchHit> search (const std::string& query, int limit,
                                         std::string& error) const;
  std::vector<MaterialSearchHit> list (int limit, int offset,
                                       std::string& error) const;

  std::string resolve_uuid (const std::string& uuid,
                            std::string& error) const;
  bool merge (const std::string& canonical_uuid,
              const std::string& duplicate_uuid, std::string& error);
  bool add_relation (const MaterialRelation& relation, std::string& error);

  bool import_file (const std::string& material_uuid,
                    const std::filesystem::path& source,
                    const std::string& role, bool make_primary,
                    MaterialImportResult& result, std::string& error);
  bool import_material_file (MaterialRecord& material,
                             const std::filesystem::path& source,
                             const std::string& role, bool make_primary,
                             MaterialImportResult& result,
                             std::string& error);
  std::vector<MaterialAttachment> attachments (
    const std::string& material_uuid, std::string& error) const;
  std::optional<MaterialAttachment> primary_attachment (
    const std::string& material_uuid, std::string& error) const;

  std::optional<std::string> material_for_identifier (
    const std::string& scheme, const std::string& value,
    std::string& error) const;
  std::optional<std::string> material_for_sha256 (
    const std::string& sha256, std::string& error) const;
  std::optional<std::string> material_for_source (
    const std::string& source_kind, const std::string& source_reference,
    std::string& error) const;

  static std::string normalize_identifier (const std::string& scheme,
                                           const std::string& value);
  static bool file_sha256 (const std::filesystem::path& path,
                           std::string& sha256, std::string& error);
  static std::string canonical_filename (const MaterialRecord& material,
                                         const std::filesystem::path& source);

private:
  bool import_file_with_sha256 (const std::string& material_uuid,
                                const std::filesystem::path& source,
                                const std::string& sha256,
                                const std::string& role, bool make_primary,
                                MaterialImportResult& result,
                                std::string& error);

  struct Impl;
  std::unique_ptr<Impl> impl;
};

#endif // ATHENA_MATERIALS_HPP
