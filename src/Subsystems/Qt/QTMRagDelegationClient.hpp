/******************************************************************************
* MODULE     : QTMRagDelegationClient.hpp
* DESCRIPTION: Qt client helpers for ATHENA RAG delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMRAGDELEGATIONCLIENT_HPP
#define QTMRAGDELEGATIONCLIENT_HPP

#include <QString>
#include <QVector>

struct QTMRagDelegationServer {
  QString name;
  QString url;
  QString publicKey;
  QString fingerprint;
};

QVector<QTMRagDelegationServer> qtm_rag_delegation_servers ();
bool qtm_rag_delegation_save_servers (
  const QVector<QTMRagDelegationServer>& servers, QString* error= nullptr);
QString qtm_rag_delegation_config_dir ();

bool qtm_rag_delegation_fetch_identity (
  const QString& baseUrl, QTMRagDelegationServer& server, QString* error);
bool qtm_rag_delegation_enroll (
  const QTMRagDelegationServer& server, QString* status, QString* error);
bool qtm_rag_delegation_check_auth (
  const QTMRagDelegationServer& server, QString* status, QString* error);
bool qtm_rag_delegation_run_embedding (
  const QTMRagDelegationServer& server, const QString& vaultRoot,
  const QString& dbPath, const QString& embeddingModel,
  const QString& embeddingDevice, QString* summary, QString* error);

#endif // QTMRAGDELEGATIONCLIENT_HPP
