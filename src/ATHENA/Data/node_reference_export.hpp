/******************************************************************************
* MODULE     : node_reference_export.hpp
* DESCRIPTION: Cancellable reference preparation and frozen export snapshots
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "node_reference.hpp"
#include <map>
#include <optional>
#include <set>

namespace athena::node_reference {
struct selection {
  std::vector<std::string> ids, ancestry;
  bool operator < (const selection&) const;
};
// Owner-local inspection only. Source and property trees never leave the caller.
std::vector<selection> export_selections (const tree&, std::vector<std::string> ancestry= {});
struct prepared_references {
  std::map<selection, node_location::snapshot> selections;
  std::string error;
  bool cancelled= false;
  std::uint64_t origin_actor= 0, origin_view= 0;
  std::string origin_url, origin_revision;
};
using prepared_snapshot= std::shared_ptr<const prepared_references>;
// Full storage fingerprint, computed only at the cold export boundary.
std::string export_source_revision (const tree&);
// Only the headless global coordinator may wait. Actors stay available to the
// locator throughout preparation; no Qt application/event loop is required.
prepared_snapshot prepare_headless_export (std::uint64_t actor, std::uint64_t view);
// Owner-local check before exporting the original source. Derived temporary
// buffers inherit the frozen references without claiming the source's identity.
void verify_export_origin ();
struct preparation_limits {
  std::size_t selections= 4096;
  std::size_t content_bytes= 1024ULL * 1024 * 1024;
};

// Uses the existing locator worker; no thread per export and no waiting on the
// initiating actor. Completion runs on the completing/cancelling thread.
class export_preparation {
  struct impl;
  std::shared_ptr<impl> data;
public:
  using completion= std::function<void (prepared_snapshot)>;
  export_preparation (std::shared_ptr<node_location::service>, std::vector<selection>,
                      completion= {}, preparation_limits= {});
  ~export_preparation ();
  export_preparation (const export_preparation&)= delete;
  export_preparation& operator = (const export_preparation&)= delete;
  prepared_snapshot read () const;
  void cancel ();
};

// Install prepared data on the rendering owner. Nested scopes inherit the
// frozen snapshot. Unknown dynamically generated references are recorded and
// must be prepared by the caller before another export attempt.
class export_reference_scope {
  export_reference_scope* previous;
  prepared_snapshot snapshot;
  std::set<selection> absent;
  bool incomplete_child= false;
  friend std::optional<view> export_reference_view (const selection&);
  friend prepared_snapshot current_export_references ();
public:
  explicit export_reference_scope (prepared_snapshot= {});
  ~export_reference_scope ();
  export_reference_scope (const export_reference_scope&)= delete;
  export_reference_scope& operator = (const export_reference_scope&)= delete;
  std::vector<selection> missing () const;
  // Call after layout but BEFORE creating any output file or printer.
  void require_ready () const;
};
std::optional<view> export_reference_view (const selection&);
prepared_snapshot current_export_references ();
} // namespace athena::node_reference
