/******************************************************************************
* MODULE     : QTMVaultAvailableEnunciations.hpp
* DESCRIPTION: Source-preserving collection of locally available enunciations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "ATHENA/Data/node_location.hpp"
#include "tree.hpp"
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

struct AvailableEnunciation {
  QString relative_path, upper, lower, title, tag;
  // Serialized once per source; no native tree or path crosses worker threads.
  std::shared_ptr<const std::string> source_body;
  QString source_uuid;
};
struct AvailableEnunciations {
  std::vector<AvailableEnunciation> entries;
  QStringList warnings;
};

AvailableEnunciations collect_available_enunciations (
  std::shared_ptr<const std::string> source_body, const QString& source_path,
  const std::function<bool (const std::string&, AthenaVaultMapNode&)>& locate,
  const std::function<tree (const QString&)>& load,
  const std::atomic<bool>& cancelled);

// XML v2 snapshots and the shared native locator, never map.sqlite or generated
// anchors. The resolver runs on a SearchWorker and must honour cancellation.
AvailableEnunciations collect_available_source_enunciations (
  std::shared_ptr<const std::string> source_body, const QString& source_path,
  const std::function<athena::node_location::snapshot (
    const std::vector<std::string>&, const std::vector<std::string>&)>& resolve,
  const std::atomic<bool>& cancelled);
