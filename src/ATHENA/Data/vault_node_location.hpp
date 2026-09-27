/******************************************************************************
* MODULE     : vault_node_location.hpp
* DESCRIPTION: Vault-scoped node locator with cancellable native actor snapshots
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "node_location.hpp"
#include "vault.hpp"

namespace athena::node_location {
// Capture once per active vault, then share among native reference consumers.
// Creating the service starts a sleeping worker, not an initial vault scan.
std::shared_ptr<service> for_vault (vault_context_handle);

// Background consumption for online targets. The actor revalidates the UUID
// against its current source, then sends an independent XML v2 fragment. The
// caller decodes that fragment on its own thread. No AUDMAP session is involved.
std::string read_live (vault_context_handle, const item&, const std::atomic<bool>& cancelled);
// Online reference consumption also refuses a disk candidate which has become
// actor-owned since resolution. Callers retry resolution on stale locations.
std::string read_online (vault_context_handle, const item&, const std::atomic<bool>& cancelled);
} // namespace athena::node_location
