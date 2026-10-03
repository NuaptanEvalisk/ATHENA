/******************************************************************************
* MODULE     : compound_virtual_document.hpp
* DESCRIPTION: Compound virtual document descriptors and lazy member state
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "vault.hpp"
#include <QByteArray>
#include <QString>
#include <optional>
#include <vector>

namespace athena::avd {

// Cache payloads are detached XML bytes, never actor-owned mutable trees.
// A checkpoint contains counter values only, not the member's macro environment.
struct counter_checkpoint {
  QString member;
  QByteArray prefix_revision;
  QByteArray counters_xml;
};

struct descriptor {
  QString namespace_uuid;
  // Relative to the .avd file's directory, or an absolute vault directory.
  QString vault_directory;
  std::vector<counter_checkpoint> checkpoints;
};

struct descriptor_file {
  descriptor value;
  QByteArray revision;
};

descriptor_file read_descriptor (const QString& filename);
// expected_revision is the digest returned by read_descriptor. An empty digest
// means create, not overwrite. Saving the view never writes a source document.
QByteArray save_descriptor (const QString& filename, const descriptor&,
                           const QByteArray& expected_revision);

struct member {
  QString filename;
  QString relative_filename;
  // The source actor supplies these for an open document, including unsaved
  // changes. A disk mtime alone is not a semantic counter-cache revision.
  QByteArray content_revision;
  QByteArray environment_revision;
};

std::vector<member> resolve_members (const QString& descriptor_filename,
                                    const descriptor&,
                                    const vault_context_handle&);

// All mutable model state belongs to the compound-view coordinator. Workers
// receive value snapshots and return byte results tagged with prefix_revision.
class counter_cache {
  std::vector<member> members_;
  std::vector<QByteArray> prefixes_;
  std::vector<counter_checkpoint> checkpoints_;
  QByteArray seed_;
  void recompute_from (std::size_t);
public:
  counter_cache (std::vector<member>, QByteArray computation_contract,
                 const std::vector<counter_checkpoint>& persisted= {});
  std::size_t size () const { return members_.size (); }
  const member& at (std::size_t i) const { return members_.at (i); }
  void set_revision (std::size_t, QByteArray content, QByteArray environment);
  void invalidate_from (std::size_t);
  // Empty means that some preceding source/environment revision is unknown.
  QByteArray prefix_after (std::size_t) const;
  std::optional<counter_checkpoint> before (std::size_t member_index) const;
  bool publish (std::size_t, const QByteArray& expected_prefix,
                QByteArray counters_xml);
  std::vector<counter_checkpoint> persistent_checkpoints () const;
};

// Fenwick sums avoid walking all member geometries on every scroll event.
// Heights include the separator; a view only mounts the intersecting members.
class layout_index {
  std::vector<double> heights_;
  std::vector<double> sums_;
public:
  layout_index (std::size_t count, double estimated_height);
  std::size_t size () const { return heights_.size (); }
  void set_height (std::size_t, double);
  double height (std::size_t i) const { return heights_.at (i); }
  double top (std::size_t) const;
  double extent () const { return top (size ()); }
  // Returns size() for an empty layout or an offset at/past its end.
  std::size_t member_at (double offset) const;
};

} // namespace athena::avd
