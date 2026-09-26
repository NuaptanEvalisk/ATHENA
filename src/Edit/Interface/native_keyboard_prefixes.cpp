/******************************************************************************
* MODULE     : native_keyboard_prefixes.cpp
* DESCRIPTION: Native JSON keyboard wildcard/prefix configuration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "native_keyboard_prefixes.hpp"
#include "file.hpp"
#include "server.hpp"
#include "scheme.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

string prefix_string (QJsonValue value) {
  ASSERT (value.isString (), "keyboard prefix string expected");
  QByteArray bytes= value.toString ().toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

bool profiles_active (QJsonValue value) {
  if (value.isUndefined ()) return true;
  ASSERT (value.isArray (), "keyboard prefix profiles must be an array");
  array<object> profiles;
  for (QJsonValue profile: value.toArray ())
    profiles << symbol_object (prefix_string (profile));
  return as_bool (call ("has-look-and-feel?", as_list_object (profiles)));
}

void load_wildcards (QJsonValue value, bool post) {
  if (value.isUndefined ()) return;
  ASSERT (value.isArray (), "keyboard wildcard list expected");
  for (QJsonValue item: value.toArray ()) {
    ASSERT (item.isObject (), "keyboard wildcard object expected");
    QJsonObject object= item.toObject ();
    ASSERT (object.value ("key").isString () &&
            object.value ("replacement").isString (),
            "keyboard wildcard needs key and replacement");
    get_server ()->insert_kbd_wildcard (
      prefix_string (object.value ("key")),
      prefix_string (object.value ("replacement")), post,
      object.value ("left").toBool (false),
      object.value ("right").toBool (true));
  }
}

} // namespace

void
native_keyboard_prefixes_load () {
  static bool loaded= false;
  if (loaded) return;
  loaded= true;

  string source;
  ASSERT (!load_string (url ("$ATHENA_PATH/misc/input/keyboard-prefixes.json"),
                        source, false), "cannot read keyboard-prefixes.json");
  c_string bytes (source);
  QJsonParseError error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (bytes, N(source)), &error);
  ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
          "invalid keyboard-prefixes.json");
  QJsonObject root= document.object ();
  ASSERT (root.value ("version").toInt () == 1 &&
          root.value ("variant_key").isString () &&
          root.value ("unvariant_key").isString (),
          "unsupported keyboard prefix schema");
  get_server ()->set_variant_keys (prefix_string (root.value ("variant_key")),
                                   prefix_string (root.value ("unvariant_key")));
  if (root.contains ("profiles")) {
    ASSERT (root.value ("profiles").isArray (), "keyboard prefix profiles expected");
    for (QJsonValue value: root.value ("profiles").toArray ()) {
      ASSERT (value.isObject (), "keyboard prefix profile expected");
      QJsonObject group= value.toObject ();
      if (!profiles_active (group.value ("profiles"))) continue;
      load_wildcards (group.value ("pre"), false);
      load_wildcards (group.value ("post"), true);
    }
  }
  load_wildcards (root.value ("pre"), false);
  load_wildcards (root.value ("post"), true);
}
