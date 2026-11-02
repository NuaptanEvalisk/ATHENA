/******************************************************************************
* MODULE     : native_keyboard_loader.cpp
* DESCRIPTION: Native demand loading and idle prewarming of keyboard modules
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#include "native_keyboard_loader.hpp"
#include "generic_keyboard_commands.hpp"
#include "editor_modes.hpp"
#include "editor.hpp"
#include "qt_utilities.hpp"
#include "file.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {

string native_text (const QString& value) {
  const auto bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

struct keyboard_module {
  const QJsonObject descriptor;
  const editor_mode_predicate mode;
  std::once_flag once;
  std::atomic<bool> loaded {false};
  explicit keyboard_module (QJsonObject value): descriptor (std::move (value)),
    mode (descriptor.contains ("mode") ?
      editor_mode_resolve (descriptor["mode"].toString ().toStdString ()) : nullptr) {}

  void load () {
    std::call_once (once, [this] {
      generic_keyboard_load_domain (native_text (descriptor["domain"].toString ()));
      loaded.store (true, std::memory_order_release);
    });
  }
};

struct keyboard_loader {
  std::vector<std::unique_ptr<keyboard_module>> modules;
  std::atomic<bool> ready {false}, warming {false};

  keyboard_loader () {
    string source;
    ASSERT (!load_string (url ("$ATHENA_PATH/misc/input/keyboard-modules.json"),
                          source, false), "cannot read keyboard-modules.json");
    QJsonParseError error;
    const auto document= QJsonDocument::fromJson (QByteArray (source.data (), N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid keyboard-modules.json");
    const auto root= document.object ();
    ASSERT (root["version"].toInt () == 1 && root["modules"].isArray (),
            "unsupported keyboard module schema");
    for (const auto value: root["modules"].toArray ()) {
      const auto descriptor= value.toObject ();
      ASSERT ((!descriptor.contains ("mode") || descriptor["mode"].isString ()) &&
              descriptor["domain"].isString () && !descriptor.contains ("module"),
              "keyboard module requires a native domain and an optional mode");
      modules.push_back (std::make_unique<keyboard_module> (descriptor));
    }
  }
};

keyboard_loader& loader () {
  static keyboard_loader value;
  return value;
}

// Module publication can ask for shortcut labels. Do not recursively load
// another keyboard module while publishing the current one's definitions.
thread_local bool loading= false;
struct loading_scope {
  loading_scope () { loading= true; }
  ~loading_scope () { loading= false; }
};

} // namespace

void native_keyboard_initialize () {
  loader ().ready.store (true, std::memory_order_release);
}

void native_keyboard_force (bool all) {
  auto& state= loader ();
  if (!state.ready.load (std::memory_order_acquire) || loading) return;
  loading_scope scope;
  for (const auto& entry: state.modules) {
    if (entry->loaded.load (std::memory_order_acquire)) continue;
    if (all || !entry->mode || entry->mode ())
      entry->load ();
  }
}

void native_keyboard_start_warmup () {
  auto& state= loader ();
  if (state.warming.exchange (true)) return;
  qt_schedule_editor_task (500, [] {
    auto& state= loader ();
    if (loading) return 10;
    loading_scope scope;
    for (const auto& entry: state.modules) {
      if (entry->loaded.load (std::memory_order_acquire)) continue;
      entry->load ();
      return 1;
    }
    state.warming.store (false);
    get_current_editor ()->update_menus ();
    return -1;
  });
}
