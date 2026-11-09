/******************************************************************************
* MODULE     : generic_keyboard_bindings.cpp
* DESCRIPTION: JSON generic keymaps and native action dispatch
* COPYRIGHT  : (C) 1999 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Editor/Commands/generic_keyboard_commands.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "Editor/Commands/native_keyboard_prefixes.hpp"
#include "Editor/Commands/native_keyboard_registry.hpp"
#include "native_dispatch.hpp"
#include "Editor/Commands/application_preferences.hpp"
#include "Editor/Commands/editor_modes.hpp"
#include "new_view.hpp"
#include "Editor/editor.hpp"
#include "App/Window/interactive_commands.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <climits>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace {

string binding_string (QJsonValue value) {
  ASSERT (value.isString (), "keymap string expected");
  QString text= value.toString ();
  QByteArray bytes= text.toUtf8 ();
  ASSERT (QString::fromUtf8 (bytes) == text, "invalid Unicode keymap string");
  return string (bytes.constData (), bytes.size ());
}

tree content_descriptor (QJsonValue value) {
  if (value.isString ()) return tree (binding_string (value));
  const QJsonObject descriptor= value.toObject ();
  ASSERT (descriptor.size () == 1 && descriptor["list"].isArray (),
          "keymap content must be text or a tagged tree");
  const QJsonArray values= descriptor["list"].toArray ();
  ASSERT (!values.isEmpty () && values[0].isObject (),
          "keymap content needs a tag");
  const QJsonObject tag= values[0].toObject ();
  ASSERT (tag.size () == 1 && tag["symbol"].isString (), "keymap content tag expected");
  tree result (as_tree_label (binding_string (tag["symbol"])));
  for (int i=1; i<values.size (); ++i) result << content_descriptor (values[i]);
  return result;
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
  if (item.contains ("prompt")) {
    ASSERT (item.size () == 2 && binding_string (item["prompt"]) != "" &&
            item["labels"].isArray (), "invalid interactive keymap action");
    for (QJsonValue label: item["labels"].toArray ()) (void) binding_string (label);
  }
  else if (item.contains ("call")) {
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
    if (kind == "focus") {
      ASSERT (data.isBool () && data.toBool (), "invalid focus expression");
    }
    else if (kind == "equal") {
      ASSERT (data.isArray () && data.toArray ().size () == 2,
              "equality needs two operands");
      for (QJsonValue arg: data.toArray ()) validate_expression (arg);
    }
    else if (kind == "symbol") {
      ASSERT (binding_string (data) != "", "empty keymap identifier");
    }
    else if (kind == "number") {
      ASSERT (data.isDouble (), "keymap real number expected");
    }
    else if (kind == "list") {
      (void) content_descriptor (value);
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
    add ("presentation", "presentation-keybindings.json");
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

// Syntax is retained only for inverse shortcut lookup and command display.
tree source_expression (QJsonValue value) {
  if (value.isString ()) return raw_quote (binding_string (value));
  if (value.isBool ()) return value.toBool () ? "#t" : "#f";
  if (value.isDouble ()) return as_string (value.toInt ());
  const auto item= value.toObject ();
  if (item.contains ("focus")) return tuple ("focus-tree");
  if (item.contains ("equal")) {
    const auto operands= item["equal"].toArray ();
    return tuple ("==", source_expression (operands[0]), source_expression (operands[1]));
  }
  if (item.contains ("prompt")) {
    tree result= tuple ("interactive", binding_string (item["prompt"]));
    for (QJsonValue label: item["labels"].toArray ())
      result << raw_quote (binding_string (label));
    return result;
  }
  if (item.contains ("number")) {
    char buffer[64];
    auto encoded= std::to_chars (buffer, buffer + sizeof (buffer), item["number"].toDouble ());
    ASSERT (encoded.ec == std::errc (), "cannot encode keymap number");
    string number (buffer, encoded.ptr - buffer);
    if (!occurs (".", number) && !occurs ("e", number)) number << ".0";
    return number;
  }
  if (item.contains ("symbol")) return tuple ("quote", binding_string (item["symbol"]));
  if (item.contains ("list")) return tuple ("quote", tree_to_scheme_tree (content_descriptor (value)));
  if (item.contains ("call")) {
    tree result= tuple (binding_string (item["call"]));
    for (QJsonValue arg: item["args"].toArray ()) result << source_expression (arg);
    return result;
  }
  if (item.contains ("if"))
    return tuple ("if", source_expression (item["if"]),
                  source_expression (item["then"]), source_expression (item["else"]));
  if (item.contains ("not")) return tuple ("not", source_expression (item["not"]));
  const bool all= item.contains ("all");
  tree result= tuple (all ? "and" : "or");
  for (QJsonValue arg: item[all ? "all" : "any"].toArray ()) result << source_expression (arg);
  return result;
}

command_value expression (QJsonValue value) {
  if (value.isString ()) return binding_string (value);
  if (value.isBool ()) return value.toBool ();
  if (value.isDouble ()) return value.toInt ();
  const auto item= value.toObject ();
  if (item.contains ("focus")) {
    editor ed= get_current_editor ();
    return ed->the_subtree (ed->focus_get ());
  }
  if (item.contains ("equal")) {
    const auto operands= item["equal"].toArray ();
    return command_equal (expression (operands[0]), expression (operands[1]));
  }
  if (item.contains ("prompt")) {
    array<string> labels;
    for (QJsonValue label: item["labels"].toArray ()) labels << binding_string (label);
    interactive_command (binding_string (item["prompt"]), labels);
    return true;
  }
  if (item.contains ("number")) return item["number"].toDouble ();
  if (item.contains ("symbol")) return as_tree_label (binding_string (item["symbol"]));
  if (item.contains ("list")) return content_descriptor (value);
  if (item.contains ("call")) {
    command_arguments args;
    for (QJsonValue arg: item["args"].toArray ()) args << expression (arg);
    const string name= binding_string (item["call"]);
    command_value result;
    if (native_dispatch_call (name, args, result)) return result;
    throw std::invalid_argument ("Unknown native keymap command: " +
                                std::string (name.data (), N(name)));
  }
  if (item.contains ("if"))
    return expression (item[command_truth (expression (item["if"])) ? "then" : "else"]);
  if (item.contains ("not")) return !command_truth (expression (item["not"]));
  const bool all= item.contains ("all");
  command_value result (all);
  for (QJsonValue arg: item[all ? "all" : "any"].toArray ()) {
    result= expression (arg);
    if (command_truth (result) != all) return result;
  }
  return result;
}

QJsonObject keymap_group (int group) {
  const auto& groups= keymap_groups ();
  ASSERT (group >= 0 && group < groups.size (), "invalid generic keymap group");
  return groups[group].toObject ();
}

} // namespace

command_value generic_keyboard_run (int group, int binding) {
  auto descriptor= keymap_group (group);
  auto entries= descriptor["bindings"].toArray ();
  ASSERT (binding >= 0 && binding < entries.size (), "invalid generic key binding");
  auto commands= entries[binding].toObject ()["commands"].toArray ();
  command_value result (false);
  for (QJsonValue command: commands) result= expression (command);
  return result;
}

bool generic_keyboard_condition (int group) {
  auto descriptor= keymap_group (group)["require"];
  ASSERT (!descriptor.isUndefined (), "generic keymap group has no condition");
  return command_truth (expression (descriptor));
}

void register_group (int g) {
  const auto& groups= keymap_groups ();
  ASSERT (g >= 0 && g < groups.size (), "invalid native keymap group");
  auto group= groups[g].toObject ();
  if (group.contains ("profiles")) {
    array<string> profiles;
    for (QJsonValue profile: group["profiles"].toArray ())
      profiles << binding_string (profile);
    if (!application_look_and_feel_matches (profiles)) return;
  }
  native_keyboard_context context;
  std::function<bool()> mode;
  if (group.contains ("mode")) {
    context.identity= group["mode"].toString ().toStdString ();
    mode= editor_mode_resolve (group["mode"].toString ().toStdString ());
  }
  const bool required= group.contains ("require");
  if (required) context.identity += "/require:" + std::to_string (g);
  if (mode || required)
    context.matches= [mode, required, g] {
      return (!mode || mode ()) && (!required || generic_keyboard_condition (g));
    };
  auto entries= group.contains ("bindings") ? group["bindings"].toArray () : QJsonArray ();
  for (int b= 0; b < entries.size (); ++b) {
    auto binding= entries[b].toObject ();
    native_key_binding action;
    string source;
    if (binding.contains ("text")) {
      action.text= binding["text"].toString ().toStdString ();
      source= scm_quote (binding_string (binding["text"]));
    }
    else {
      tree terms (TUPLE, "begin");
      for (QJsonValue command: binding["commands"].toArray ())
        terms << source_expression (command);
      source= scheme_tree_to_string (N(terms) == 2 ? terms[1] : terms);
      action.action= [g, b] { (void) generic_keyboard_run (g, b); };
    }
    action.help= binding["help"].toString ().toStdString ();
    // Use the shared registry: it owns prefix rewriting, partial sequences,
    // inverse lookup and interaction with domain-specific/user bindings.
    native_keyboard_bind (context, binding_string (binding["key"]), std::move (action), source);
  }
  if (group.contains ("unmap"))
    for (QJsonValue key: group["unmap"].toArray ())
      native_keyboard_unbind (context.identity, binding_string (key));
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
