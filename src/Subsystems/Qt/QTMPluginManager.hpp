/******************************************************************************
* MODULE     : QTMPluginManager.hpp
* DESCRIPTION: GUI-owned ATHENA plugin processes, launch grants and persistent policy
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "audmap_server.hpp"
#include "package.hpp"
#include <QObject>
#include <QString>

struct QTMPluginPolicy {
  enum class Startup { Manual, Automatic, Delayed } startup = Startup::Manual;
  enum class Access { ReadOnly, Full, Custom } access = Access::ReadOnly;
  int delaySeconds = 10;
  athena::interop::trust_mode trust = athena::interop::trust_mode::confirm_operations;
  std::map<std::string, std::set<std::string>> commands;
};
struct QTMPluginInfo {
  athena::plugins::manifest manifest;
  QTMPluginPolicy policy;
  QString state, error, log;
  athena::interop::value lastResult;
  qint64 pid = 0;
  bool running = false, connected = false;
};

class QTMPluginManager final: public QObject {
  Q_OBJECT
  struct impl;
  std::unique_ptr<impl> implementation;
public:
  QTMPluginManager (std::filesystem::path home, std::filesystem::path endpoint,
    std::shared_ptr<const athena::interop::resolver_registry> registry,
    std::function<void (std::string)> revoke, QObject* parent = nullptr);
  ~QTMPluginManager () override;
  std::vector<QTMPluginInfo> plugins () const;
  bool busy () const;
  void install (const std::filesystem::path& source);
  void uninstall (const std::string& id);
  void configure (const std::string& id, const QTMPluginPolicy& policy);
  void start (const std::string& id);
  void stop (const std::string& id, bool force = false);
  void restart (const std::string& id);
  std::uint64_t command (const std::string& id, const std::string& command);
  bool authorize (const std::string& key, std::optional<athena::interop::connection_grant>& grant,
                  std::string* display_name = nullptr);
  void disconnected (const std::string& key);
signals:
  void changed ();
  void managementFinished (QString error);
};
