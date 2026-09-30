/******************************************************************************
* MODULE     : artifact_live_cache.hpp
* DESCRIPTION: Actor-owned structural artifacts and saved-revision publication
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "artifacts.hpp"
#include "vault.hpp"
#include <map>
#include <memory>

struct buffer_document_state;

namespace athena::artifact {
using record_batch= std::shared_ptr<const std::vector<AthenaArtifactRecord>>;
// No live tree is retained. Unaffected source units reuse their extracted data.
class live_cache {
  std::string incarnation, file;
  std::map<std::string, record_batch> units;
  std::map<std::string, std::string> previous_bindings;
  bool initialized= false;
public:
  bool ready (const buffer_document_state&) const;
  void update (buffer_document_state&, tree& body, const std::vector<int>& dirty);
  void reset () { initialized= false; units.clear (); }
};

struct publication {
  vault_context_handle vault;
  std::string relative_path;
  std::uint64_t owner= 0;
  std::vector<record_batch> units;
};
struct saved_revision {
  vault_context_handle vault;
  std::filesystem::path file;
  std::string sha256;
};
// These queues contain only C++ values, never TeXmacs trees or Scheme objects.
void publish (publication);
void close (std::uint64_t owner);
void saved (const std::filesystem::path&, const std::string& sha256);
void publish_pending ();
std::vector<saved_revision> take_saved ();
} // namespace athena::artifact
