/******************************************************************************
* MODULE     : generic_keyboard_bindings.cpp
* DESCRIPTION: JSON generic keymaps and native action dispatch
* COPYRIGHT  : (C) 1999 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "generic_keyboard_commands.hpp"
#include "file.hpp"
#include "native_keyboard_prefixes.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <climits>
#include <cmath>
#include <unordered_map>

namespace {

string binding_string (QJsonValue value) {
  ASSERT (value.isString (), "keymap string expected");
  QString text= value.toString ();
  QByteArray bytes= text.toUtf8 ();
  ASSERT (QString::fromUtf8 (bytes) == text, "invalid Unicode keymap string");
  return string (bytes.constData (), bytes.size ());
}

void validate_expression (QJsonValue value) {
  if (value.isString ()) { (void) binding_string (value); return; }
  if (value.isBool ()) return;
  if (value.isDouble ()) {
    double number= value.toDouble ();
    ASSERT (number >= INT_MIN && number <= INT_MAX && std::floor (number) == number,
            "keymap integer expected; use a number descriptor for real values");
    return;
  }
  ASSERT (value.isObject (), "invalid keymap expression");
  QJsonObject item= value.toObject ();
  if (item.contains ("call")) {
    ASSERT (item.size () == 2 && binding_string (item["call"]) != "" &&
            item["args"].isArray (), "invalid keymap call");
    for (QJsonValue arg: item["args"].toArray ()) validate_expression (arg);
  }
  else if (item.contains ("if")) {
    ASSERT (item.size () == 3 && item.contains ("then") && item.contains ("else"),
            "invalid conditional keymap action");
    validate_expression (item["if"]);
    validate_expression (item["then"]);
    validate_expression (item["else"]);
  }
  else {
    ASSERT (item.size () == 1, "invalid keymap value descriptor");
    QString kind= item.begin ().key ();
    QJsonValue data= item.begin ().value ();
    if (kind == "symbol" || kind == "procedure" || kind == "keyword") {
      ASSERT (binding_string (data) != "", "empty keymap identifier");
    }
    else if (kind == "number") {
      ASSERT (data.isDouble (), "keymap real number expected");
    }
    else if (kind == "list") {
      ASSERT (data.isArray (), "keymap list must be an array");
      for (QJsonValue arg: data.toArray ()) validate_expression (arg);
    }
    else if (kind == "not") validate_expression (data);
    else {
      ASSERT ((kind == "all" || kind == "any") && data.isArray (),
              "unknown keymap expression kind");
      for (QJsonValue arg: data.toArray ()) validate_expression (arg);
    }
  }
}

void validate_group (QJsonObject group) {
  for (auto i= group.begin (); i != group.end (); ++i)
    ASSERT (i.key () == "profiles" || i.key () == "mode" ||
             i.key () == "require" || i.key () == "bindings" ||
             i.key () == "unmap",
             "unknown keymap group field");
  if (group.contains ("profiles")) {
    ASSERT (group["profiles"].isArray (), "keymap profiles must be an array");
    for (QJsonValue profile: group["profiles"].toArray ())
      (void) binding_string (profile);
  }
  if (group.contains ("mode")) (void) binding_string (group["mode"]);
  if (group.contains ("require")) validate_expression (group["require"]);
  ASSERT (group.contains ("bindings") || group.contains ("unmap"),
          "keymap group needs bindings or unmap");
  QJsonArray entries= group.contains ("bindings") ?
    group["bindings"].toArray () : QJsonArray ();
  for (QJsonValue entry: entries) {
    ASSERT (entry.isObject (), "keymap binding expected");
    auto binding= entry.toObject ();
    ASSERT (binding_string (binding["key"]) != "", "empty shortcut");
    if (binding.contains ("text")) {
      ASSERT (binding.size () == (binding.contains ("help") ? 3 : 2),
              "invalid text key binding");
      (void) binding_string (binding["text"]);
      if (binding.contains ("help")) (void) binding_string (binding["help"]);
    }
    else {
      ASSERT (binding.size () == 2 && binding["commands"].isArray () &&
              !binding["commands"].toArray ().isEmpty (), "invalid key commands");
      for (QJsonValue command: binding["commands"].toArray ())
        validate_expression (command);
    }
  }
  if (group.contains ("unmap")) {
    ASSERT (group["unmap"].isArray (), "keymap unmap must be an array");
    for (QJsonValue key: group["unmap"].toArray ())
      ASSERT (binding_string (key) != "", "empty unmap shortcut");
  }
}

struct keymap_range { int first= 0; int last= 0; };

struct native_keymap_registry {
  QJsonArray groups;
  std::unordered_map<std::string,keymap_range> ranges;

  native_keymap_registry () {
    add ("generic", "generic-keybindings.json");
    add ("prefix", "prefix-keybindings.json");
    add ("text", "text-keybindings.json");
    add ("prog", "prog-keybindings.json");
    add ("source", "source-keybindings.json");
    add ("table", "table-keybindings.json");
    add ("graphics", "graphics-keybindings.json");
    add ("fold", "fold-keybindings.json");
    add ("tmdoc", "tmdoc-keybindings.json");
    add ("automate", "automate-keybindings.json");
  }

  void add (const char* domain, const char* file) {
    const int first= groups.size ();
    string source;
    const string path= "$ATHENA_PATH/misc/input/" * string (file);
    ASSERT (!load_string (url (path), source, false), "cannot read native keymap JSON");
    c_string bytes (source);
    QJsonParseError error;
    auto document= QJsonDocument::fromJson (QByteArray (bytes, N (source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid native keymap JSON");
    auto root= document.object ();
    ASSERT (root["version"].toInt () == 2 && root["string_encoding"] == "utf-8" &&
            root["groups"].isArray (), "unsupported native keymap schema");
    for (QJsonValue value: root["groups"].toArray ()) {
      ASSERT (value.isObject (), "keymap group expected");
      validate_group (value.toObject ());
      groups.append (value);
    }
    ranges.emplace (domain, keymap_range {first, (int) groups.size ()});
  }
};

native_keymap_registry& keymap_registry () {
  static native_keymap_registry registry;
  return registry;
}

const QJsonArray& keymap_groups () {
  return keymap_registry ().groups;
}

bool truth (object value) {
  return !(is_bool (value) && !as_bool (value));
}

// Source reconstruction is only for inverse key lookup and command metadata.
// At invocation, this evaluator calls the current public procedure so mode
// overrides and later user redefinitions still apply.
object expression (QJsonValue value, bool source) {
  if (value.isString ()) return object (binding_string (value));
  if (value.isBool ()) return object (value.toBool ());
  if (value.isDouble ()) return object (value.toInt ());
  auto item= value.toObject ();
  if (item.contains ("number")) return object (item["number"].toDouble ());
  if (item.contains ("procedure")) {
    object name= symbol_object (binding_string (item["procedure"]));
    return source ? name : eval (name);
  }
  if (item.contains ("symbol")) {
    object name= symbol_object (binding_string (item["symbol"]));
    return source ? list_object (symbol_object ("quote"), name) : name;
  }
  if (item.contains ("keyword"))
    return keyword_object (binding_string (item["keyword"]));
  if (item.contains ("list")) {
    array<object> values;
    for (QJsonValue arg: item["list"].toArray ())
      values << expression (arg, false);
    object value= as_list_object (values);
    return source ? list_object (symbol_object ("quote"), value) : value;
  }
  if (item.contains ("call")) {
    array<object> args;
    for (QJsonValue arg: item["args"].toArray ()) args << expression (arg, source);
    string name= binding_string (item["call"]);
    return source ? cons (symbol_object (name), as_list_object (args)) : call (name, args);
  }
  if (item.contains ("if")) {
    if (!source) return expression (item[truth (expression (item["if"], false))
                                       ? "then" : "else"], false);
    array<object> terms;
    terms << symbol_object ("if") << expression (item["if"], true)
          << expression (item["then"], true) << expression (item["else"], true);
    return as_list_object (terms);
  }
  if (item.contains ("not")) {
    object arg= expression (item["not"], source);
    return source ? list_object (symbol_object ("not"), arg) : object (!truth (arg));
  }
  bool all= item.contains ("all");
  array<object> terms;
  object result (all);
  for (QJsonValue arg: item[all ? "all" : "any"].toArray ()) {
    result= expression (arg, source);
    if (source) terms << result;
    else if (truth (result) != all) return result;
  }
  return source ? cons (symbol_object (all ? "and" : "or"), as_list_object (terms)) : result;
}

object native_callback (const char* command, array<object> arguments,
                        array<object> source_body) {
  object body= cons (symbol_object (command), as_list_object (arguments));
  object lambda= list_object (symbol_object ("lambda"), null_object (), body);
  object callback= eval (lambda);
  object original= cons (symbol_object ("lambda"),
                        cons (null_object (), as_list_object (source_body)));
  call ("set-procedure-property!", callback, symbol_object ("source"), original);
  return callback;
}

QJsonObject keymap_group (int group) {
  const auto& groups= keymap_groups ();
  ASSERT (group >= 0 && group < groups.size (), "invalid generic keymap group");
  return groups[group].toObject ();
}

} // namespace

object generic_keyboard_run (int group, int binding) {
  auto descriptor= keymap_group (group);
  auto entries= descriptor["bindings"].toArray ();
  ASSERT (binding >= 0 && binding < entries.size (), "invalid generic key binding");
  auto commands= entries[binding].toObject ()["commands"].toArray ();
  object result= object (false);
  for (QJsonValue command: commands) result= expression (command, false);
  return result;
}

bool generic_keyboard_condition (int group) {
  auto descriptor= keymap_group (group)["require"];
  ASSERT (!descriptor.isUndefined (), "generic keymap group has no condition");
  return truth (expression (descriptor, false));
}

void register_group (int g) {
  const auto& groups= keymap_groups ();
  ASSERT (g >= 0 && g < groups.size (), "invalid native keymap group");
  auto group= groups[g].toObject ();
  if (group.contains ("profiles")) {
    array<object> profiles;
    for (QJsonValue profile: group["profiles"].toArray ())
      profiles << symbol_object (binding_string (profile));
    if (!as_bool (call ("has-look-and-feel?", as_list_object (profiles)))) return;
  }
  array<object> conditions;
  if (group.contains ("mode"))
    conditions << eval (symbol_object (binding_string (group["mode"])));
  if (group.contains ("require")) {
    array<object> args, source;
    args << object (g);
    source << expression (group["require"], true);
    conditions << native_callback ("generic-keyboard-condition?", args, source);
  }
  auto entries= group.contains ("bindings") ? group["bindings"].toArray () : QJsonArray ();
  for (int b= 0; b < entries.size (); ++b) {
    auto binding= entries[b].toObject ();
    object action;
    if (binding.contains ("text")) action= object (binding_string (binding["text"]));
    else {
      array<object> args, source;
      args << object (g) << object (b);
      for (QJsonValue command: binding["commands"].toArray ())
        source << expression (command, true);
      action= native_callback ("generic-keyboard-run", args, source);
    }
    string help= binding.contains ("help") ? binding_string (binding["help"]) : string ("");
    // Use the shared registry: it owns prefix rewriting, partial sequences,
    // inverse lookup and interaction with domain-specific/user bindings.
    call ("kbd-binding", as_list_object (conditions),
          object (binding_string (binding["key"])), action, object (help));
  }
  if (group.contains ("unmap"))
    for (QJsonValue key: group["unmap"].toArray ())
      call ("kbd-delete-key-binding2", as_list_object (conditions),
            object (binding_string (key)));
}

void generic_keyboard_load_domain (string domain) {
  const std::string key (domain.data (), (std::size_t) N(domain));
  const auto& ranges= keymap_registry ().ranges;
  auto found= ranges.find (key);
  ASSERT (found != ranges.end (), "unknown native keymap domain");
  for (int g= found->second.first; g < found->second.last; ++g)
    register_group (g);
}

void generic_keyboard_load () {
  native_keyboard_prefixes_load ();
  generic_keyboard_load_domain ("generic");
  generic_keyboard_load_domain ("prefix");
}
