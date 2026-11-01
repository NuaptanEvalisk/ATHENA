/******************************************************************************
* MODULE     : native_keyboard_loader.cpp
* DESCRIPTION: Native demand loading and idle prewarming of keyboard modules
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#include "native_keyboard_loader.hpp"
#include "generic_keyboard_commands.hpp"
#include "file.hpp"
#include "tm_timer.hpp"

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
  std::once_flag once;
  std::atomic<bool> loaded {false};
  explicit keyboard_module (QJsonObject value): descriptor (std::move (value)) {}

  void load () {
    std::call_once (once, [this] {
      if (descriptor.contains ("domain"))
        generic_keyboard_load_domain (native_text (descriptor["domain"].toString ()));
      else {
        array<object> parts;
        for (const auto value: descriptor["module"].toArray ())
          parts << symbol_object (native_text (value.toString ()));
        object module= call ("module-provide", as_list_object (parts));
        if (!as_bool (call ("module?", module)))
          throw std::runtime_error ("Could not load keyboard editing module");
      }
      loaded.store (true, std::memory_order_release);
    });
  }
};

struct keyboard_loader {
  std::vector<std::unique_ptr<keyboard_module>> modules;
  std::atomic<bool> ready {false}, warming {false};
  std::atomic<time_t> warmup_after {0};

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
      ASSERT (descriptor["mode"].isString () &&
              (descriptor["domain"].isString () != descriptor["module"].isArray ()),
              "keyboard module requires mode and one domain or module");
      if (descriptor.contains ("module")) {
        ASSERT (!descriptor["module"].toArray ().isEmpty (), "empty keyboard module");
        for (const auto part: descriptor["module"].toArray ())
          ASSERT (part.isString (), "keyboard module component must be a string");
      }
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
    if (all || as_bool (call (native_text (entry->descriptor["mode"].toString ()))))
      entry->load ();
  }
}

void native_keyboard_start_warmup () {
  auto& state= loader ();
  if (state.warming.exchange (true)) return;
  state.warmup_after.store (texmacs_time () + 500);
  // The existing delayed queue retains the execution domain and owns the
  // callback lifetime; all scheduling policy and progress live here.
  exec_delayed_pause (eval ("(lambda () (native-keyboard-warmup-step))"));
}

object native_keyboard_warmup_step () {
  auto& state= loader ();
  const time_t remaining= state.warmup_after.load () - texmacs_time ();
  if (remaining > 0) return object ((int) remaining);
  if (loading) return object (10);
  loading_scope scope;
  for (const auto& entry: state.modules) {
    if (entry->loaded.load (std::memory_order_acquire)) continue;
    entry->load ();
    return object (1);
  }
  state.warming.store (false);
  call ("update-menus");
  return object (true);
}
