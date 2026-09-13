/******************************************************************************
* MODULE     : authorization_ui_test.cpp
* DESCRIPTION: Isolated Qt tests for connection decisions and remembered trust
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMAudmap.hpp"
#include "connection.hpp"
#include "resources.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <chrono>
#include <iostream>

namespace athena::interop {
static const std::string test_identity =
  "opaque:01234567-89ab-cdef-0123-456789abcdef:fedcba98-7654-3210-fedc-ba9876543210";
// Deliberately unrelated to any ATHENA domain: confirmation UI must only use
// the frozen protocol payload, never interpret types or fetch properties.
class opaque_resource final: public resource {
public:
  std::string type () const override { return "example.custom-type"; }
  std::string identity () const override { return test_identity; }
  value properties () const override { throw std::runtime_error ("Unexpected property lookup"); }
  value inspect () const override { return value::object (); }
  operation_result operate (const std::string& command, const value& parameters) const override {
    return {"OK", {{"command", command}, {"parameters", parameters}}};
  }
};
class opaque_resolver final: public resolver {
public:
  resolver_outcome resolve (const resolution_request& request, resolution_output& output) const override {
    if (request.basepoint) return resolver_outcome::irrelevant;
    output.publish (std::make_shared<opaque_resource> (), request.offset + 1);
    return resolver_outcome::resolved;
  }
};
std::shared_ptr<const resolver_registry> native_resolvers () {
  return std::make_shared<const resolver_registry> (resolver_registry {std::make_shared<opaque_resolver> ()});
}
}
using namespace athena::interop;

static void require (bool b, const char* message) { if (!b) throw std::runtime_error (message); }
static QDialog* dialog (const QString& title) {
  for (auto* widget: QApplication::topLevelWidgets ())
    if (widget->isVisible () && widget->windowTitle () == title)
      if (auto* d = qobject_cast<QDialog*> (widget)) return d;
  return nullptr;
}
static void until (const std::function<bool ()>& ready) {
  const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (3);
  while (!ready ()) {
    require (std::chrono::steady_clock::now () < deadline, "Authorization UI test timed out");
    QTest::qWait (5);
  }
}
static void choose (const char* label, int mode) {
  until ([] { return dialog ("ATHENA Interop Connection") != nullptr; });
  auto* d = dialog ("ATHENA Interop Connection");
  d->findChild<QComboBox*> ()->setCurrentIndex (mode);
  for (auto* button: d->findChildren<QPushButton*> ()) if (button->text () == label) {
    button->click (); return;
  }
  throw std::runtime_error ("Missing authorization choice");
}
static void expect (client_connection& client, transport_opcode expected) {
  bool done = false;
  until ([&] {
    client.tick ();
    if (auto msg = client.receive ()) {
      if (msg->at (0) == static_cast<unsigned> (transport_opcode::pending)) return false;
      require (msg->at (0) == static_cast<unsigned> (expected), "Wrong connection decision");
      done = true;
    }
    return done;
  });
}

static value terminal_reply (client_connection& client, opcode expected) {
  value result;
  until ([&] {
    client.tick ();
    if (auto msg = client.receive ()) {
      if (msg->at (0) == static_cast<unsigned> (opcode::ack)) return false;
      require (msg->at (0) == static_cast<unsigned> (expected), "Unexpected protocol response");
      result = std::move (*msg); return true;
    }
    return false;
  });
  return result;
}

static void screenshot (QDialog* d, const char* name) {
  const auto output = qEnvironmentVariable ("ATHENA_AUDMAP_TEST_SCREENSHOTS");
  if (output.isEmpty ()) return;
  QDir ().mkpath (output);
  QTest::qWait (20);
  require (d->grab ().save (QDir (output).filePath (name)), "Cannot save dialog screenshot");
}

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  app.setQuitOnLastWindowClosed (false);
  try {
    QTemporaryDir root;
    require (root.isValid (), "Cannot create isolated authorization test home");
    qputenv ("ATHENA_HOME_PATH", root.path ().toUtf8 ());
    qputenv ("XDG_RUNTIME_DIR", root.path ().toUtf8 ());
    QTMAudmap server;
    auto entries = QDir (root.path ()).entryList ({"athena-audmap-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    require (entries.size () == 1, "Missing test IPC descriptor");
    const std::filesystem::path endpoint = QDir (root.path ()).filePath (entries.front () + "/connection.json").toStdString ();
    const std::filesystem::path base = root.path ().toStdString ();
    const auto key = load_client_identity (base / "once.json");
    authorization_store store (base / "system/audmap/clients.json");
    {
      client_connection client (endpoint, base / "once.json", "same display name");
      choose ("Allow (this time only)", 1); expect (client, transport_opcode::welcome);
      require (!store.lookup (key.public_key), "Allow once persisted");
    }
    QTest::qWait (80);
    {
      client_connection client (endpoint, base / "once.json", "same display name");
      choose ("Reject (this time only)", 1); expect (client, transport_opcode::rejected);
      require (!store.lookup (key.public_key), "Reject once persisted");
    }
    QTest::qWait (80);
    {
      client_connection client (endpoint, base / "once.json", "same display name");
      choose ("Allow (always)", 2); expect (client, transport_opcode::welcome);
      require (store.lookup (key.public_key)->trust == trust_mode::confirm_requests, "Always lost selected trust mode");
    }
    QTest::qWait (80);
    {
      client_connection client (endpoint, base / "once.json", "changed display name");
      expect (client, transport_opcode::welcome);
      require (!dialog ("ATHENA Interop Connection"), "Remembered allow prompted again");
      client.send (value::array ({1, 1, "@", value::array ({0})}));
      until ([] { return dialog ("Confirm Interop Request") != nullptr; });
      // Disconnect must withdraw pending confirmation without touching a vault.
    }
    until ([] { return !dialog ("Confirm Interop Request"); });
    QTest::qWait (80);
    {
      client_connection client (endpoint, base / "once.json", "REPL <literal client name>");
      expect (client, transport_opcode::welcome);
      client.send (value::array ({1, 10, "@", value::array ({0})}));
      until ([] { return dialog ("Confirm Interop Request") != nullptr; });
      auto* request_dialog = dialog ("Confirm Interop Request");
      require (request_dialog->findChild<QLabel*> ("audmap_selection")->text () == "@", "Missing resolution selector");
      require (!request_dialog->findChild<QLabel*> ("audmap_accessor"), "Unresolved request invented an accessor");
      screenshot (request_dialog, "resolution.png");
      request_dialog->findChild<QPushButton*> ("audmap_allow")->click ();
      const auto resolved = terminal_reply (client, opcode::acx);
      const auto handle = resolved[2][0][0][0];
      const value arguments {{"enabled", true}, {"options", value::array ({1, "<tag>"})}};
      const value operation = value::array ({5, 10, 7, handle, "frobnicate", arguments});
      client.send (operation);
      until ([] { return dialog ("Confirm Interop Request") != nullptr; });
      auto* d = dialog ("Confirm Interop Request");
      auto field = [d] (const char* name) { auto* label = d->findChild<QLabel*> (name);
        require (label && label->textFormat () == Qt::PlainText, "Missing or rich-text request field"); return label->text (); };
      require (field ("audmap_client") == "REPL <literal client name>", "Client identity label is wrong");
      require (field ("audmap_command") == "frobnicate", "Command was interpreted or hidden");
      require (field ("audmap_accessor") == "Handle 1 (ticket 10)", "Accessor context is missing");
      require (field ("audmap_resource_type") == "example.custom-type", "Resource type was interpreted");
      require (field ("audmap_resource_id") == QString::fromStdString (test_identity), "Resource identity was interpreted");
      require (value::parse (d->findChild<QPlainTextEdit*> ("audmap_arguments")->toPlainText ().toStdString ()) == arguments,
        "Displayed arguments differ from the operation");
      auto* details = d->findChild<QWidget*> ("audmap_details");
      require (!details->isVisible (), "Raw protocol dominates the initial view");
      require (d->findChild<QPushButton*> ("audmap_reject")->isDefault (), "Enter must not silently approve");
      screenshot (d, "operation.png");
      d->findChild<QToolButton*> ("audmap_details_toggle")->click ();
      require (details->isVisible (), "Request details do not expand");
      require (field ("audmap_public_key") == QString::fromStdString (key.public_key), "Wrong authenticated key");
      const auto raw = value::parse (d->findChild<QPlainTextEdit*> ("audmap_raw_request")->toPlainText ().toStdString ());
      require (raw["request"] == operation, "Raw request changed during display");
      screenshot (d, "operation-details.png");
      d->findChild<QPushButton*> ("audmap_allow")->click ();
      const auto response = terminal_reply (client, opcode::rsp);
      require (response[4] == value ({{"command", "frobnicate"}, {"parameters", arguments}}),
        "Executed operation differs from displayed request");
    }
    {
      client_connection client (endpoint, base / "denied.json", "same display name");
      choose ("Reject (always)", 0); expect (client, transport_opcode::rejected);
    }
    QTest::qWait (80);
    {
      client_connection client (endpoint, base / "denied.json", "changed display name");
      expect (client, transport_opcode::rejected);
      require (!dialog ("ATHENA Interop Connection"), "Remembered reject prompted again");
    }
    std::cout << "Four authorization choices, remembered identity and trust, disconnect cleanup passed\n";
    return 0;
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
