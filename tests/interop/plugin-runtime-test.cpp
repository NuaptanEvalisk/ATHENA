/******************************************************************************
* MODULE     : plugin-runtime-test.cpp
* DESCRIPTION: Real plugin subprocess lifecycle, desktop policy and menu integration tests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMPluginManager.hpp"
#include "QTMPluginUi.hpp"
#include "QTMAudmap.hpp"
#include "resources.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace athena::interop;
namespace athena::interop {
struct plugin_test_root: resource {
  std::string type () const override { return "root"; }
  std::string identity () const override { return "root"; }
  value properties () const override { return {{"name", "Test root"}}; }
  value inspect () const override { return properties (); }
  operation_result operate (const std::string&, const value&) const override { return {"OK", true}; }
};
struct plugin_test_resolver: resolver {
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (req.basepoint || req.offset >= req.selectors.size () || req.selectors[req.offset].type != selector::kind::default_resource)
      return resolver_outcome::irrelevant;
    out.publish (std::make_shared<plugin_test_root> (), req.offset + 1); return resolver_outcome::resolved;
  }
};
std::shared_ptr<const resolver_registry> native_resolvers () {
  return std::make_shared<const resolver_registry> (resolver_registry {std::make_shared<plugin_test_resolver> ()});
}
}
static void require (bool condition, const char* text) { if (!condition) throw std::runtime_error (text); }
static void until (const std::function<bool ()>& predicate, const char* text, int ms = 6000) {
  const auto end = std::chrono::steady_clock::now () + std::chrono::milliseconds (ms);
  while (!predicate ()) { require (std::chrono::steady_clock::now () < end, text); QTest::qWait (10); }
}
static QTMPluginInfo info (QTMPluginManager* manager) {
  auto list = manager->plugins (); require (list.size () == 1, "Expected one installed fixture"); return list[0];
}
static value run (QTMPluginManager* manager, const char* name) {
  const auto id = manager->command ("fixture.plugin", name);
  until ([&] { auto r = info (manager).lastResult; return r.is_object () && r.at ("id") == id; }, "Plugin command did not reply");
  return info (manager).lastResult.at ("result");
}
static bool alive (pid_t pid) {
  std::ifstream in ("/proc/" + std::to_string (pid) + "/stat"); std::string line;
  if (!std::getline (in, line)) return false;
  const auto end = line.rfind (')'); return end == std::string::npos || line.substr (end + 2, 1) != "Z";
}
int main (int argc, char** argv) {
  QApplication app (argc, argv);
  QTemporaryDir profile;
  struct cleanup { ~cleanup () { qt_audmap_stop (); } } guard;
  try {
    require (profile.isValid (), "No isolated profile");
    qputenv ("ATHENA_HOME_PATH", profile.filePath ("home").toUtf8 ());
    qputenv ("PYTHONPATH", QByteArray (PLUGIN_PYTHON_PATH));
    // The fixture uses distribution PyZMQ/msgpack, not the developer's Conda environment.
    qputenv ("PATH", QByteArray ("/usr/bin:") + qgetenv ("PATH"));
    const auto source = profile.filePath ("fixture").toStdString ();
    std::filesystem::create_directory (source);
    require (QFile::copy (PLUGIN_FIXTURE_PATH, QString::fromStdString (source + "/plugin_exec")), "Cannot copy plugin script");
    const value manifest {{"schema", 1}, {"id", "fixture.plugin"}, {"name", "Fixture Plugin"}, {"version", "1"},
      {"commands", value::array ({{{"id", "hello"}, {"title", "Say hello"}, {"parameters", {{"x", 42}}}},
        {{"id", "probe"}, {"title", "Probe write"}}, {{"id", "child"}, {"title", "Spawn child"}},
        {{"id", "ignore-stop"}, {"title", "Ignore stop"}}})}};
    { std::ofstream file (source + "/manifest.json"); file << manifest.dump (); }
    qt_audmap_start ();
    auto* manager = qtm_plugin_manager (); require (manager, "No desktop plugin manager");
    QString error;
    QObject::connect (manager, &QTMPluginManager::managementFinished, &app, [&] (QString e) { error = e; });
    manager->install (source);
    until ([&] { return !manager->busy (); }, "Install did not finish");
    require (error.isEmpty () && !info (manager).running, "Install failed or auto-executed a new plugin");
    std::unique_ptr<QWidget> page (qtm_plugin_preferences (manager)); page->resize (760, 900); page->show ();
    require (page->findChild<QTreeWidget*> ("plugin-list")->topLevelItemCount () == 1, "Preferences omitted plugin");
    page->findChild<QToolButton*> ("plugin-start-stop")->click ();
    until ([&] { return info (manager).connected; }, "Plugin did not authenticate");
    require (run (manager, "hello").at ("parameters").at ("x") == 42, "Plugin lost command arguments");
    require (run (manager, "probe").at ("allowed") == false, "Read-only plugin wrote to a native resource");
    for (auto* widget: QApplication::topLevelWidgets ())
      require (!qobject_cast<QDialog*> (widget) || !widget->isVisible (), "Plugin IPC unexpectedly prompted for authorization");
    QWidget menuParent; auto* menu = qtm_plugins_menu (&menuParent);
    QMetaObject::invokeMethod (menu, "aboutToShow", Qt::DirectConnection);
    auto submenus = menu->findChildren<QMenu*> (QString (), Qt::FindDirectChildrenOnly);
    require (submenus.size () == 1 && submenus[0]->title () == "Fixture Plugin", "Plugin menu missing manifest entry");
    for (auto* action: submenus[0]->actions ()) if (action->text () == "Say hello") action->trigger ();
    until ([&] { return info (manager).lastResult.at ("id") == 3; }, "Menu command did not reach subscription");
    auto first = info (manager); const auto oldGuid = first.lastResult.at ("result").at ("guid");
    manager->restart ("fixture.plugin");
    until ([&] { auto i = info (manager); return i.connected && i.pid && i.pid != first.pid; }, "Restart did not create a new process");
    require (!alive (first.pid), "Restart left old leader alive");
    require (run (manager, "hello").at ("guid") != oldGuid, "Restart reused subscription identity");
    page->findChild<QComboBox*> ("plugin-access")->setCurrentIndex (1);
    page->findChild<QComboBox*> ("plugin-trust")->setCurrentIndex (0);
    page->findChild<QPushButton*> ("plugin-apply")->click ();
    until ([&] { return !info (manager).running; }, "Changing policy left old process running");
    manager->start ("fixture.plugin");
    until ([&] { return info (manager).connected; }, "Full-access restart did not connect");
    require (run (manager, "probe").at ("allowed") == true, "Configured full access was not granted");
    if (!qEnvironmentVariable ("ATHENA_PLUGIN_TEST_SCREENSHOT").isEmpty ())
      require (page->grab ().save (qEnvironmentVariable ("ATHENA_PLUGIN_TEST_SCREENSHOT")), "Cannot save preferences screenshot");
    auto confirmed = info (manager).policy; confirmed.trust = trust_mode::confirm_operations;
    manager->configure ("fixture.plugin", confirmed);
    until ([&] { return !info (manager).running; }, "Confirmation policy did not stop old grant");
    manager->start ("fixture.plugin"); until ([&] { return info (manager).connected; }, "Confirmation launch failed");
    const auto probe = manager->command ("fixture.plugin", "probe");
    QDialog* confirmation = nullptr;
    until ([&] {
      for (auto* widget: QApplication::topLevelWidgets ())
        if (widget->isVisible () && widget->findChild<QLabel*> ("audmap_command")) {
          confirmation = qobject_cast<QDialog*> (widget); return confirmation != nullptr;
        }
      return false;
    }, "Native operation was not confirmed");
    require (confirmation->findChild<QLabel*> ("audmap_client")->text () == "Fixture Plugin [fixture.plugin]",
             "Confirmation trusted the plugin's self-declared name");
    confirmation->reject ();
    until ([&] { auto r = info (manager).lastResult; return r.is_object () && r.at ("id") == probe; }, "Rejected operation lost plugin reply");
    require (info (manager).lastResult.at ("result").at ("allowed") == false, "Rejected operation executed");
    confirmed.trust = trust_mode::full_access; confirmed.access = QTMPluginPolicy::Access::Custom;
    confirmed.commands = {{"root", {"write"}}}; manager->configure ("fixture.plugin", confirmed);
    until ([&] { return !info (manager).running; }, "Custom policy did not retire old process");
    manager->start ("fixture.plugin"); until ([&] { return info (manager).connected; }, "Custom policy launch failed");
    require (run (manager, "probe").at ("allowed") == true, "Custom command permission did not reach session");
    auto child = run (manager, "child").at ("child").get<pid_t> ();
    auto leader = info (manager).pid;
    manager->stop ("fixture.plugin", true);
    until ([&] { return !info (manager).running && !alive (leader) && !alive (child); }, "Force quit left a process alive");
    auto policy = info (manager).policy; policy.startup = QTMPluginPolicy::Startup::Delayed; policy.delaySeconds = 1;
    manager->configure ("fixture.plugin", policy);
    require (info (manager).state == "Scheduled" && !info (manager).running, "Delayed startup was not scheduled correctly");
    policy.startup = QTMPluginPolicy::Startup::Manual;
    manager->configure ("fixture.plugin", policy);
    require (info (manager).state == "Stopped", "Manual startup left a scheduled state");
    QEventLoop cancelledDelay;
    QTimer::singleShot (1200, &cancelledDelay, &QEventLoop::quit);
    cancelledDelay.exec ();
    require (!info (manager).running, "Cancelled delayed startup still launched");
    policy.startup = QTMPluginPolicy::Startup::Delayed;
    manager->configure ("fixture.plugin", policy);
    until ([&] { return info (manager).connected; }, "Delayed plugin did not start");
    run (manager, "ignore-stop");
    until ([&] { return info (manager).log.contains ("IGNORING TERM"); }, "Fixture did not enter uncooperative mode");
    const auto stoppingAt = std::chrono::steady_clock::now ();
    manager->stop ("fixture.plugin");
    until ([&] { return !info (manager).running; }, "Stop did not escalate after grace period");
    require (std::chrono::steady_clock::now () - stoppingAt >= std::chrono::milliseconds (1700), "Grace period was not observed");
    policy.startup = QTMPluginPolicy::Startup::Automatic;
    manager->configure ("fixture.plugin", policy);
    until ([&] { return info (manager).connected; }, "Automatic plugin did not start");
    leader = info (manager).pid;
    page.reset (); qt_audmap_stop ();
    require (!alive (leader), "Service shutdown left plugin running");
    qt_audmap_start (); manager = qtm_plugin_manager ();
    require (info (manager).policy.startup == QTMPluginPolicy::Startup::Automatic, "Startup policy was not persisted");
    until ([&] { return info (manager).connected; }, "Persisted automatic startup failed");
    manager->stop ("fixture.plugin"); until ([&] { return !info (manager).running; }, "Final stop failed");
    manager->uninstall ("fixture.plugin"); until ([&] { return !manager->busy (); }, "Uninstall did not finish");
    require (manager->plugins ().empty (), "Uninstall left a registered plugin");
    require (std::filesystem::is_directory (profile.filePath ("home/plugins-data/fixture.plugin").toStdString ()),
             "Uninstall removed persistent plugin data");
    std::cout << "Plugin runtime tests passed\n";
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    if (auto* manager = qtm_plugin_manager ()) for (const auto& p: manager->plugins ())
      std::cerr << p.state.toStdString () << ' ' << p.error.toStdString () << '\n' << p.log.toStdString ();
    qt_audmap_stop (); return 1;
  }
}
