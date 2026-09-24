/******************************************************************************
* MODULE     : native_math_keyboard.cpp
* DESCRIPTION: Native JSON math shortcut registry
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "native_math_keyboard.hpp"
#include "editor.hpp"
#include "edit_interface.hpp"
#include "structured_commands.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "language.hpp"
#include "utf8_edit.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <unordered_map>
#include <vector>

namespace {

string native_string (const QJsonValue& value) {
  ASSERT (value.isString (), "native math keymap string expected");
  const QString text= value.toString ();
  const QByteArray bytes= text.toUtf8 ();
  ASSERT (QString::fromUtf8 (bytes) == text, "invalid UTF-8 math keymap string");
  return string (bytes.constData (), bytes.size ());
}

tree native_tree (const QJsonValue& value) {
  if (value.isString ()) return tree (native_string (value));
  ASSERT (value.isArray (), "native math keymap tree must be string or array");
  const QJsonArray values= value.toArray ();
  ASSERT (!values.isEmpty () && values[0].isString (),
          "native math keymap compound tree needs a tag");
  tree result (as_tree_label (native_string (values[0])),
               values.size () - 1);
  for (int i=1; i<values.size (); ++i) result[i-1]= native_tree (values[i]);
  return result;
}

struct math_binding {
  string key;
  string text;
  string help;
  QJsonArray actions;
};

struct math_group {
  string context;
  std::vector<math_binding> bindings;
};

struct binding_ref { int group= -1; int binding= -1; };

struct native_math_registry {
  std::vector<math_group> groups;
  std::unordered_map<std::string,std::vector<binding_ref>> exact;
  std::unordered_map<std::string,std::vector<binding_ref>> prefixes;

  native_math_registry () {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/input/math-keybindings.json"),
                     source, false))
      return;
    QJsonParseError error;
    QJsonDocument document= QJsonDocument::fromJson (
      QByteArray (source.data (), N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid math-keybindings.json");
    const QJsonObject root= document.object ();
    ASSERT (root.value ("version").toInt () == 1 &&
            root.value ("string_encoding") == "utf-8" &&
            root.value ("groups").isArray (),
            "unsupported native math keymap schema");
    const QJsonArray json_groups= root.value ("groups").toArray ();
    groups.reserve ((std::size_t) json_groups.size ());
    for (const QJsonValue& group_value: json_groups) {
      ASSERT (group_value.isObject (), "native math keymap group expected");
      const QJsonObject object= group_value.toObject ();
      ASSERT (object.size () == 2 && object.value ("context").isString () &&
              object.value ("bindings").isArray (),
              "invalid native math keymap group");
      math_group group;
      group.context= native_string (object.value ("context"));
      const QJsonArray bindings= object.value ("bindings").toArray ();
      group.bindings.reserve ((std::size_t) bindings.size ());
      for (const QJsonValue& binding_value: bindings) {
        ASSERT (binding_value.isObject (), "native math key binding expected");
        const QJsonObject binding_object= binding_value.toObject ();
        ASSERT (binding_object.value ("key").isString (),
                "native math key binding requires a key");
        math_binding binding;
        binding.key= native_string (binding_object.value ("key"));
        binding.help= binding_object.contains ("help") ?
          native_string (binding_object.value ("help")) : string ("");
        const bool text= binding_object.contains ("text");
        const bool actions= binding_object.contains ("actions");
        ASSERT (text != actions, "native math key binding needs text or actions");
        if (text) binding.text= native_string (binding_object.value ("text"));
        else {
          ASSERT (binding_object.value ("actions").isArray () &&
                  !binding_object.value ("actions").toArray ().isEmpty (),
                  "native math key actions must be a nonempty array");
          binding.actions= binding_object.value ("actions").toArray ();
        }
        group.bindings.push_back (std::move (binding));
      }
      groups.push_back (std::move (group));
    }
    for (int g=0; g<(int) groups.size (); ++g)
      for (int b=0; b<(int) groups[(std::size_t) g].bindings.size (); ++b) {
        const string& key= groups[(std::size_t) g].bindings[(std::size_t) b].key;
        const std::string std_key (key.data (), (std::size_t) N(key));
        exact[std_key].push_back ({g,b});
        for (int i=0; i<N(key); ++i)
          if (key[i] == ' ' && i > 0)
            prefixes[std::string (key.data (), (std::size_t) i)].push_back ({g,b});
      }
  }
};

native_math_registry& registry () {
  static native_math_registry value;
  return value;
}

bool context_active (const string& context) {
  editor ed= get_current_editor ();
  if (is_nil (ed)) return false;
  const string mode= ed->get_env_string (MODE);
  const bool math= mode == "math" && !ed->inside_graphics ();
  const bool hybrid= ed->inside (HYBRID);
  if (context == "math") return math;
  if (context == "math-or-hybrid") return math || hybrid;
  if (context == "math-not-hybrid") return math && !hybrid;
  if (context == "math-english") {
    const string language= ed->get_env_string (LANGUAGE);
    return math && (language == "english" || language == "british");
  }
  if (context == "math-dutch") return math && ed->get_env_string (LANGUAGE) == "dutch";
  if (context == "math-french") return math && ed->get_env_string (LANGUAGE) == "french";
  if (context == "math-german") return math && ed->get_env_string (LANGUAGE) == "german";
  if (context == "math-macos") return math && os_macos ();
  static const string inside_prefix= "inside:";
  if (starts (context, inside_prefix))
    return ed->inside (context (N(inside_prefix), N(context)));
  FAILED ("unknown native math keymap context");
  return false;
}

void insert_content (const QJsonArray& pieces) {
  editor ed= get_current_editor ();
  ASSERT (!is_nil (ed), "native math action without editor");
  if (pieces.size () == 1) {
    ed->insert_tree (native_tree (pieces[0]));
    return;
  }
  tree content (CONCAT, pieces.size ());
  for (int i=0; i<pieces.size (); ++i) content[i]= native_tree (pieces[i]);
  ed->insert_tree (content);
}

int native_large_mode (const QJsonValue& value) {
  if (value.isBool ()) return value.toBool () ? 1 : 0;
  ASSERT (value.isString () && value.toString () == "default",
          "native math bracket large mode expected");
  return -1;
}

tree math_previous_item (editor ed) {
  path p= ed->selection_get_cursor_path ();
  if (is_nil (p)) return tree ();
  int i= last_item (p);
  path q= path_up (p);
  tree root= ed->the_root ();
  if (!has_subtree (root, q)) return tree ();
  tree current= subtree (root, q);
  if (is_atomic (current)) {
    if (i <= 0) return tree ();
    int begin= utf8_grapheme_previous (current->label, min (i, N(current->label)));
    return tree (current->label (begin, min (i, N(current->label))));
  }
  if (i <= 0 || i > N(current)) return tree ();
  int child= i - 1;
  tree previous= current[child];
  while (child > 0 && (is_func (previous, RSUB) || is_func (previous, RSUP) ||
                       is_func (previous, RPRIME) || is_func (previous, make_tree_label ("suppressed")))) {
    previous= current[--child];
  }
  return previous;
}

bool native_allow_math_space_after (tree previous) {
  if (is_nil (previous) || is_func (previous, BIG)) return false;
  if (!is_atomic (previous) && !is_func (previous, NAMED_SYMBOL, 1)) return true;
  const string type= math_symbol_type (previous);
  return type != "prefix" && type != "infix" && type != "separator" &&
         type != "prefix-infix" && type != "opening-bracket" &&
         type != "middle-bracket";
}

void native_kbd_space () {
  editor ed= get_current_editor ();
  ASSERT (!is_nil (ed), "native math action without editor");
  const string preference= get_preference ("math spacebar");
  tree previous= math_previous_item (ed);
  if (preference == "allow spurious spaces") { ed->insert_tree (tree (" ")); return; }
  if (previous == " ") {
    if (preference == "no spurious spaces") return;
    ed->remove_text (false);
    ed->make_space (string ("1em"));
    return;
  }
  if (is_func (previous, SPACE, 1)) { ed->make_space (string ("1em")); return; }
  if (native_allow_math_space_after (previous)) ed->insert_tree (tree (" "));
}

void execute_action (const QJsonObject& action) {
  editor ed= get_current_editor ();
  ASSERT (!is_nil (ed), "native math action without editor");
  const string op= native_string (action.value ("op"));
  if (op == "insert-tree") ed->insert_tree (native_tree (action.value ("value")));
  else if (op == "insert-content") {
    ASSERT (action.value ("pieces").isArray (), "insert-content needs pieces");
    insert_content (action.value ("pieces").toArray ());
  }
  else if (op == "make-lprime") ed->make_lprime (native_string (action.value ("value")));
  else if (op == "make-rprime") ed->make_rprime (native_string (action.value ("value")));
  else if (op == "make-script") {
    ASSERT (action.value ("sup").isBool () && action.value ("right").isBool (),
            "make-script needs booleans");
    ed->make_script (action.value ("sup").toBool (), action.value ("right").toBool ());
  }
  else if (op == "make-fraction") ed->make_fraction ();
  else if (op == "make-sqrt") ed->make_sqrt ();
  else if (op == "make-neg") ed->make_neg ();
  else if (op == "make-above") ed->make_above ();
  else if (op == "make-below") ed->make_below ();
  else if (op == "make-wide" || op == "make-wide-under") {
    ASSERT (action.value ("value").isString (), "wide action needs value");
    const string value= native_string (action.value ("value"));
    const bool stretch= action.value ("stretch").toBool (false);
    if (op == "make-wide") ed->make_wide (value, stretch);
    else ed->make_wide_under (value, stretch);
  }
  else if (op == "make-with")
    ed->make_with (native_string (action.value ("var")),
                   native_string (action.value ("value")));
  else if (op == "insert-long-arrow") {
    const tree value= native_tree (action.value ("value"));
    const bool below= action.value ("below").toBool (false);
    tree arrow= below ? tree (LONG_ARROW, value, "", "") :
                        tree (LONG_ARROW, value, "");
    ed->insert_tree (arrow, below ? path (2, 0) : path (1, 0));
  }
  else if (op == "insert-big")
    ed->insert_tree (tree (BIG, native_tree (action.value ("value"))));
  else if (op == "make") {
    ASSERT (action.value ("tag").isString (), "make action needs tag");
    ed->make_compound (as_tree_label (native_string (action.value ("tag"))));
  }
  else if (op == "make-hybrid") ed->make_hybrid ();
  else if (op == "math-make-math") {
    if (ed->inside ("math")) ed->go_end_of (make_tree_label ("math"));
    else ed->set_message ("Warning: already inside mathematics", "mathematics");
  }
  else if (op == "math-make-above") ed->math_make_above ();
  else if (op == "math-make-below") ed->math_make_below ();
  else if (op == "math-jump-out") generic_structured_exit_right ();
  else if (op == "math-kbd-select-enlarge") ed->math_kbd_select_enlarge ();
  else if (op == "math-evaluation-bar") ed->math_evaluation_bar ();
  else if (op == "kbd-space") native_kbd_space ();
  else if (op == "bracket-open")
    ed->math_bracket_open (native_string (action.value ("left")),
                           native_string (action.value ("right")),
                           native_large_mode (action.value ("large")));
  else if (op == "bracket-close")
    ed->math_bracket_close (native_string (action.value ("right")),
                            native_string (action.value ("left")),
                            native_large_mode (action.value ("large")));
  else if (op == "separator")
    ed->math_separator (native_string (action.value ("value")),
                        native_large_mode (action.value ("large")));
  else if (op == "equation-to-eqnarray") {
    ASSERT (ed->inside ("equation") || ed->inside ("equation*"),
            "equation conversion outside equation");
    ed->equation_to_eqnarray (ed->inside ("equation*") ?
      make_tree_label ("equation*") : make_tree_label ("equation"));
  }
  else if (op == "eqnarray-to-equation") ed->eqnarray_to_equation ();
  else FAILED ("unknown native math keyboard action");
}

void run_binding (int group, int binding) {
  const auto& r= registry ();
  ASSERT (group >= 0 && group < (int) r.groups.size (), "invalid native math group");
  const auto& bindings= r.groups[(std::size_t) group].bindings;
  ASSERT (binding >= 0 && binding < (int) bindings.size (), "invalid native math binding");
  for (const QJsonValue& value: bindings[(std::size_t) binding].actions) {
    ASSERT (value.isObject (), "native math action must be an object");
    execute_action (value.toObject ());
  }
}

class native_math_command_rep final: public command_rep {
  int group, binding;
public:
  native_math_command_rep (int g, int b): group (g), binding (b) {}
  void apply () override { run_binding (group, binding); }
  tm_ostream& print (tm_ostream& out) override {
    return out << "<native-math-keyboard-command>";
  }
};

bool active_ref (const native_math_registry& r, const binding_ref& ref) {
  return ref.group >= 0 && ref.group < (int) r.groups.size () &&
         context_active (r.groups[(std::size_t) ref.group].context);
}

} // namespace

bool native_math_keyboard_get_keycomb (
  string combination, int& status, command& cmd, string& shorthand, string& help) {
  auto& r= registry ();
  const std::string key (combination.data (), (std::size_t) N(combination));
  auto exact= r.exact.find (key);
  if (exact != r.exact.end ()) {
    for (auto it= exact->second.rbegin (); it != exact->second.rend (); ++it) {
      if (!active_ref (r, *it)) continue;
      const auto& binding= r.groups[(std::size_t) it->group].bindings[(std::size_t) it->binding];
      help= binding.help;
      if (N(binding.text) > 0) {
        status= 2; cmd= command (); shorthand= binding.text;
      }
      else {
        status= 1;
        cmd= command (tm_new<native_math_command_rep> (it->group, it->binding));
        shorthand= combination;
      }
      return true;
    }
  }
  auto prefix= r.prefixes.find (key);
  if (prefix != r.prefixes.end ())
    for (auto it= prefix->second.rbegin (); it != prefix->second.rend (); ++it)
      if (active_ref (r, *it)) {
        status= 2; cmd= command (); shorthand= combination; help= "";
        return true;
      }
  return false;
}

int native_math_keyboard_binding_count () {
  int total= 0;
  for (const auto& group: registry ().groups) total += (int) group.bindings.size ();
  return total;
}

bool native_math_keyboard_context_active () {
  editor ed= get_current_editor ();
  return !is_nil (ed) && ed->get_env_string (MODE) == "math" &&
         !ed->inside_graphics ();
}

bool native_math_disable_pre_edit (string key) {
  if (!native_math_keyboard_context_active ()) return false;
  return key == "^" || key == "~" || key == "`" || key == "'" || key == "\"";
}

string native_math_downgrade_pre_edit (string key) {
  ASSERT (native_math_keyboard_context_active (),
          "native math pre-edit downgrade outside mathematics");
  static const std::unordered_map<std::string,std::string> downgrade {
    {"à","a"},{"á","a"},{"â","a"},{"ã","a"},{"ä","a"},
    {"ĉ","c"},{"è","e"},{"é","e"},{"ê","e"},{"ẽ","e"},{"ë","e"},
    {"ĝ","g"},{"ĥ","h"},{"ì","i"},{"í","i"},{"î","i"},{"ĩ","i"},{"ï","i"},
    {"ĵ","j"},{"m̂","m"},{"ǹ","n"},{"ń","n"},{"n̂","n"},{"ñ","n"},
    {"ò","o"},{"ó","o"},{"ô","o"},{"õ","o"},{"ö","o"},{"ŝ","s"},
    {"ù","u"},{"ú","u"},{"û","u"},{"ũ","u"},{"ü","u"},{"ṽ","v"},
    {"ẁ","w"},{"ŵ","w"},{"ỳ","y"},{"ý","y"},{"ŷ","y"},{"ỹ","y"},{"ÿ","y"},
    {"ẑ","z"},{"À","A"},{"Á","A"},{"Â","A"},{"Ã","A"},{"Ä","A"},
    {"Ĉ","C"},{"È","E"},{"É","E"},{"Ê","E"},{"Ẽ","E"},{"Ë","E"},
    {"Ĝ","G"},{"Ĥ","H"},{"Ì","I"},{"Í","I"},{"Î","I"},{"Ĩ","I"},{"Ï","I"},
    {"Ĵ","J"},{"M̂","M"},{"Ǹ","N"},{"Ń","N"},{"N̂","N"},{"Ñ","N"},
    {"Ò","O"},{"Ó","O"},{"Ô","O"},{"Õ","O"},{"Ö","O"},{"Ŝ","S"},
    {"Ù","U"},{"Ú","U"},{"Û","U"},{"Ũ","U"},{"Ü","U"},{"Ṽ","V"},
    {"Ẁ","W"},{"Ŵ","W"},{"Ỳ","Y"},{"Ý","Y"},{"Ŷ","Y"},{"Ỹ","Y"},{"Ÿ","Y"},
    {"Ẑ","Z"}
  };
  const std::string std_key (key.data (), (std::size_t) N(key));
  auto found= downgrade.find (std_key);
  if (found != downgrade.end ())
    return string (found->second.data (), (int) found->second.size ());
  if (os_macos () && native_math_disable_pre_edit (key)) return "";
  return utf8_grapheme_count (key) == 1 ? key : string ("");
}
