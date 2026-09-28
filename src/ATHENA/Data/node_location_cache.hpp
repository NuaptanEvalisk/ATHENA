/******************************************************************************
* MODULE     : node_location_cache.hpp
* DESCRIPTION: Persistent LMDB UUID locator and continuous vault index worker
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "node_location.hpp"
#include "vault.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace athena::node_location {

enum class persistent_phase {
  inactive,
  bootstrap,
  idle,
  sweep,
  work,
  degraded,
  error
};

struct persistent_status {
  persistent_phase phase= persistent_phase::inactive;
  std::size_t current= 0;
  std::size_t total= 0;
  std::size_t files= 0;
  std::size_t nodes= 0;
  std::size_t errors= 0;
  std::uint64_t generation= 0;
};

struct persistent_lookup {
  std::map<std::string,std::vector<location>> locations;
  bool bootstrap_complete= false;
  std::size_t errors= 0;
  std::uint64_t generation= 0;
};

// One continuously maintained index for the active node-model vault.  Starting
// it never waits for a scan.  The writer is background-only; readers use LMDB
// read transactions and a bounded in-process L1 cache.
void persistent_index_start (vault_context_handle vault);
void persistent_index_stop ();
void persistent_index_wake ();
persistent_status persistent_index_status ();
std::uint64_t persistent_index_generation ();
persistent_lookup persistent_index_lookup (
  const std::filesystem::path& root, const std::vector<std::string>& ids);

} // namespace athena::node_location
