/******************************************************************************
* MODULE     : document_upgrade_file.hpp
* DESCRIPTION: Backup-first atomic replacement of captured legacy document files
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "athena_document_xml.hpp"
#include "confined_filesystem.hpp"
#include <optional>

namespace athena::document {
std::string storage_bytes_fingerprint (std::string_view);
enum class legacy_format { texmacs, scheme };
enum class upgrade_durability { durable, replaced_not_durable };
class xml_file;
struct upgrade_result {
  std::filesystem::path backup;
  std::string original_sha256, xml_sha256;
  filesystem::entry file;
  // A failed directory fsync follows an already completed atomic rename. Do
  // not report success, retry with the old revision, or claim nothing changed.
  upgrade_durability durability;
  std::vector<int> root_child_map;
};

// This is a storage transaction, not a text converter. Capture at load time;
// retain the original revision/bytes without rewriting or marking the buffer
// modified. Only commit a fully migrated UTF-8 tree on an explicit normal save.
// The caller must also coordinate its database migration before activation.
class legacy_file {
  friend class document_file;
  filesystem::confined_root root_;
  std::filesystem::path relative_, backup_;
  filesystem::entry file_;
  filesystem::metadata revision_;
  std::string original_, digest_;
  legacy_format format_;
  legacy_file (filesystem::confined_root, std::filesystem::path, bool,
               filesystem::entry, filesystem::metadata, std::string, legacy_format);
public:
  static legacy_file capture (const std::filesystem::path& source,
    const std::optional<std::filesystem::path>& vault= {}, codec_limits = {});
  const std::string& original_bytes () const { return original_; }
  const std::string& original_sha256 () const { return digest_; }
  legacy_format format () const { return format_; }
  std::filesystem::path backup_path () const { return root_.path () / backup_; }
  upgrade_result commit (const tree& utf8_document, codec_limits = {}) const;
};

struct xml_save_result {
  std::string xml_sha256;
  upgrade_durability durability;
};

class xml_file {
  filesystem::confined_root root_;
  std::filesystem::path relative_;
  filesystem::entry file_;
  filesystem::metadata revision_;
  std::string digest_;
  xml_file (filesystem::confined_root, std::filesystem::path,
            filesystem::entry, filesystem::metadata, std::string);
  friend class legacy_file;
  friend class document_file;
public:
  static xml_file capture (const std::filesystem::path&, codec_limits = {});
  static xml_file create (const std::filesystem::path&, const tree&,
                          xml_save_result&, codec_limits = {});
  const std::string& source_sha256 () const { return digest_; }
  xml_save_result commit (const tree&, codec_limits = {});
};

struct document_save_result {
  std::optional<std::filesystem::path> backup;
  std::string xml_sha256;
  upgrade_durability durability= upgrade_durability::durable;
  bool upgraded_legacy= false;
};

class document_file {
  std::optional<legacy_file> legacy_;
  std::optional<xml_file> xml_;
  explicit document_file (legacy_file value): legacy_ (std::move (value)) {}
  explicit document_file (xml_file value): xml_ (std::move (value)) {}
public:
  static document_file capture (const std::filesystem::path&,
    const std::optional<std::filesystem::path>& vault= {}, codec_limits = {});
  static document_file create (const std::filesystem::path&, const tree&,
                               document_save_result&, codec_limits = {});
  bool legacy () const { return legacy_.has_value (); }
  const std::string& source_sha256 () const {
    return legacy_ ? legacy_->original_sha256 () : xml_->source_sha256 ();
  }
  document_save_result save (const tree&, codec_limits = {});
};
} // namespace athena::document
