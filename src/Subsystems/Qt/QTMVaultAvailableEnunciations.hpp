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
