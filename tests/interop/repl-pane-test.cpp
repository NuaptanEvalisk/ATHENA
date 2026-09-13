/******************************************************************************
* MODULE     : repl-pane-test.cpp
* DESCRIPTION: Exercise the embedded PTY against the real standalone AUDMAP REPL
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMAudmapTerminal.hpp"
#include "audmap_server.hpp"
#include <qtermwidget.h>
#include <QApplication>
#include <QBuffer>
#include <QLabel>
#include <QTest>
#include <QToolButton>
#include <chrono>
#include <iostream>
#include <csignal>

using namespace athena::interop;
namespace {
class test_resource final: public resource {
public:
  std::string type () const override { return "test"; }
  std::string identity () const override { return "pty-fixture"; }
  value properties () const override { return {{"name", "root"}, {"type", "test"}}; }
  value inspect () const override { return {{"get", value::object ()}}; }
  operation_result operate (const std::string&, const value&) const override {
    return {"OK", "PTY_OPERATION_RESULT"};
  }
};
class test_resolver final: public resolver {
public:
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (req.basepoint) return resolver_outcome::irrelevant;
    out.publish (std::make_shared<test_resource> (), req.offset + 1);
    return resolver_outcome::resolved;
  }
};
void require (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}
void until (std::function<bool ()> ready, const char* failure= "PTY test timed out") {
  const auto end= std::chrono::steady_clock::now () + std::chrono::seconds (5);
  while (!ready ()) {
    require (std::chrono::steady_clock::now () < end, failure);
    QTest::qWait (10);
  }
}
QString history (QTermWidget* terminal) {
  QBuffer buffer;
  buffer.open (QIODevice::WriteOnly);
  terminal->saveHistory (&buffer);
  return QString::fromUtf8 (buffer.data ());
}
void wait_text (QTermWidget* terminal, const char* text) {
  try { until ([&] { return history (terminal).contains (text); }); }
  catch (...) {
    throw std::runtime_error (std::string ("Missing terminal output: ") + text +
      "\nReceived:\n" + history (terminal).toStdString ());
  }
}
}
int main (int argc, char** argv) {
  QApplication app (argc, argv);
  try {
    authorization_ui ui;
    ui.connect= [] (auto, auto, auto, auto reply) { reply (trust_mode::full_access); };
    ui.confirm= [] (auto, auto, auto reply) { reply (true); };
    ui.disconnect= [] (auto) {};
    auto registry= std::make_shared<const resolver_registry> (
      resolver_registry {std::make_shared<test_resolver> ()});
    local_server server (registry, std::move (ui), 2);
    auto pane= std::make_unique<QTMAudmapTerminal> (AUDMAP_REPL_PATH,
      QString::fromStdString (server.discovery_file ().string ()));
    pane->resize (850, 400);
    pane->show ();
    auto* terminal= pane->findChild<QTermWidget*> ("audmap-repl-pty");
    require (terminal, "Missing real PTY widget");
    wait_text (terminal, "audm>");
    const auto first_pid= terminal->getShellPID ();
    require (first_pid > 0, "Missing REPL child process");
    terminal->sendText ("hep\x1b[D" "l\r");
    wait_text (terminal, "Enter an AUDM selector");
    terminal->sendText ("@\r");
    wait_text (terminal, "handle 1>");
    terminal->sendText ("get\r");
    wait_text (terminal, "PTY_OPERATION_RESULT");
    pane->findChild<QToolButton*> ("audmap-repl-restart")->click ();
    terminal= pane->findChild<QTermWidget*> ("audmap-repl-pty");
    wait_text (terminal, "audm>");
    require (terminal->getShellPID () != first_pid, "Restart reused a live child");
    until ([&] { return ::kill (first_pid, 0) == -1 && errno == ESRCH; }, "Restart left its previous process alive");
    terminal->sendText ("@\r");
    wait_text (terminal, "handle 1>");
    const auto prompts= history (terminal).count ("audm>");
    terminal->sendText ("exit\r");
    until ([&] { return history (terminal).count ("audm>") > prompts; }, "Exit did not return to selector mode");
    terminal->sendText ("exit\r");
    until ([&] { return terminal->getShellPID () == 0; }, "Two exit commands did not end REPL");
    try { until ([&] { return pane->findChild<QLabel*> ("audmap-repl-status")->text () == "REPL exited"; }); }
    catch (...) { throw std::runtime_error ("Missing exit status; output:\n" + history (terminal).toStdString ()); }
    pane->findChild<QToolButton*> ("audmap-repl-restart")->click ();
    terminal= pane->findChild<QTermWidget*> ("audmap-repl-pty");
    wait_text (terminal, "audm>");
    require (::kill (terminal->getShellPID (), SIGKILL) == 0, "Cannot simulate abnormal child exit");
    until ([&] { return pane->findChild<QLabel*> ("audmap-repl-status")->text () == "REPL exited"; }, "Abnormal exit was not observed");
    pane->findChild<QToolButton*> ("audmap-repl-restart")->click ();
    terminal= pane->findChild<QTermWidget*> ("audmap-repl-pty");
    wait_text (terminal, "audm>");
    const auto close_pid= terminal->getShellPID ();
    pane.reset ();
    until ([&] { return ::kill (close_pid, 0) == -1 && errno == ESRCH; }, "Pane destruction left its process alive");
    std::cout << "PASS: real PTY, Readline editing, resolution, operation, restart and child cleanup\n";
    return 0;
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
