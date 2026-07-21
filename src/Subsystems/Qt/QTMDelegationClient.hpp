/******************************************************************************
* MODULE     : QTMDelegationClient.hpp
* DESCRIPTION: Qt client helpers for authenticated ATHENA delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMDELEGATIONCLIENT_HPP
#define QTMDELEGATIONCLIENT_HPP

#include "ATHENA/Data/artifact_range_llm.hpp"

#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

struct QTMDelegationServer {
  QString name;
  QString url;
  QString publicKey;
  QString fingerprint;
  QStringList capabilities;
  int artifactMaxRequests= 512;
  int artifactMaxPlaintextBytes= 8 * 1024 * 1024;
};

QVector<QTMDelegationServer> qtm_delegation_servers ();
bool qtm_delegation_selected_server (
  const QString& configuredUrl, QTMDelegationServer& server);
bool qtm_delegation_save_servers (
  const QVector<QTMDelegationServer>& servers, QString* error= nullptr);
QString qtm_delegation_config_dir ();

bool qtm_delegation_fetch_identity (
  const QString& baseUrl, QTMDelegationServer& server, QString* error);
bool qtm_delegation_enroll (
  const QTMDelegationServer& server, QString* status, QString* error);
bool qtm_delegation_check_auth (
  const QTMDelegationServer& server, QString* status, QString* error);
bool qtm_delegation_run_embedding (
  const QTMDelegationServer& server, const QString& vaultRoot,
  const QString& dbPath, const QString& embeddingModel,
  const QString& embeddingDevice, QString* summary, QString* error);

using QTMArtifactDelegationProgress=
  std::function<bool (size_t completed, size_t total,
                      size_t queued, size_t running)>;

bool qtm_delegation_select_artifact_ranges (
  const QTMDelegationServer& server,
  const std::vector<AthenaArtifactRangeRequest>& requests,
  std::vector<std::vector<int>>& results,
  const QTMArtifactDelegationProgress& progress, QString* error);

#endif // QTMDELEGATIONCLIENT_HPP
