/******************************************************************************
* MODULE     : node_location.hpp
* DESCRIPTION: Native source identity census and asynchronous disposable locator
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "tree.hpp"
#include "System/Files/confined_filesystem.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace athena::node_location {

// Properties do not consume child indices. These addresses can name nodes in
// rich properties as well as ordinary source children, without inventing IDs.
enum class step_kind { child, property, list_item, dictionary_item, rich_text };
struct step {
  step_kind kind;
  std::size_t index= 0;
  std::string key;
  bool operator == (const step& other) const;
};
using address= std::vector<step>;
struct occurrence { std::string id; address where; };
using census= std::vector<occurrence>;

// Owner-local only. No native tree, string or mutable property crosses threads.
census collect (const tree& source);
tree lookup (const tree& source, const address& where, const std::string& expected_id);

struct live_source {
  std::string file; // Vault-relative generic path, or empty for an untitled source.
  std::uint64_t actor= 0;
  std::uint64_t capture= 0; // Actor command watermark, not a storage revision.
  census nodes;
  std::string error;
};
// Invoked on the locator worker. Must capture owner-consistent native data and
// report failures instead of omitting open files (which would expose old disk
// contents). This callback must honour cancellation during actor queries.
using live_provider= std::function<std::vector<live_source> (const std::atomic<bool>&)>;

enum class status { pending, resolved, missing, unreadable, conflict,
                    cycle, overlap, invalid, cancelled };
struct location {
  std::string file;
  address where;
  std::uint64_t actor= 0, live_capture= 0;
  std::optional<athena::filesystem::metadata> disk_revision;
};
struct item {
  std::string id;
  status state= status::pending;
  std::vector<location> candidates;
  std::string diagnostic;
};
struct diagnostic {
  std::string file, message;
  status state= status::unreadable;
  std::string identity;
};
struct result {
  status state= status::pending;
  std::vector<item> items;
  std::vector<diagnostic> diagnostics;
  std::uint64_t scan= 0;
};

class query {
  struct impl;
  std::shared_ptr<impl> data;
  query (std::vector<std::string>, std::vector<std::string>);
  friend class service;
public:
  result poll () const;
  void cancel ();
};

// One sleeping worker per service, shared by requests for the captured vault.
// request/poll never read files or wait for an actor. Each scan inventories the
// vault; unchanged files reuse their validated census. The cache owns no IDs.
class service {
  struct impl;
  std::unique_ptr<impl> data;
public:
  explicit service (std::filesystem::path root, live_provider live= {});
  ~service ();
  service (const service&)= delete;
  service& operator = (const service&)= delete;
  std::shared_ptr<query> request (std::vector<std::string> ids,
                                std::vector<std::string> ancestry= {});
  void clear_cache ();
};

// Cold/background disk consumption only. Reopens the confined source, checks
// its revision and UUID at the recorded address, and returns an owned snapshot.
// A live candidate must instead be revalidated on its owning actor.
tree read_disk (const std::filesystem::path& root, const item& target);

} // namespace athena::node_location
