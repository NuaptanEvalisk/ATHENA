/******************************************************************************
* MODULE     : auto_close.cpp
* DESCRIPTION: Native smart quotes, apostrophes and paired delimiters
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "auto_close.hpp"

#include "analyze.hpp"
#include "file.hpp"
#include "utf8_edit.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace athena::auto_close {
namespace {

string
native_string (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

QString
qstring (const string& value) {
  return QString::fromUtf8 (value.data (), N(value));
}

struct quote_catalog {
  quote_style base;
  QJsonObject styles;

  quote_catalog () {
    string source;
    ASSERT (!load_string (url ("$ATHENA_PATH/misc/input/auto-close.json"),
                          source, false),
            "cannot read auto-close.json");
    c_string bytes (source);
    QJsonParseError error;
    QJsonDocument document=
      QJsonDocument::fromJson (QByteArray (bytes, N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid auto-close.json");
    QJsonObject root= document.object ();
    ASSERT (root.value ("version").toInt () == 1 &&
            root.value ("open_after").isArray () &&
            root.value ("default").isObject () &&
            root.value ("styles").isObject (),
            "unsupported auto-close.json schema");

    for (const QJsonValue& value: root.value ("open_after").toArray ()) {
      ASSERT (value.isString (), "auto-close open_after must contain strings");
      base.open_after << native_string (value.toString ());
    }
    QJsonObject fallback= root.value ("default").toObject ();
    ASSERT (fallback.value ("open").isString () &&
            fallback.value ("close").isString () &&
            fallback.value ("open_suffix").isString () &&
            fallback.value ("close_prefix").isString () &&
            fallback.value ("apostrophe_opens").isBool () &&
            fallback.value ("paired").isBool (),
            "invalid auto-close default quote style");
    base.open= native_string (fallback.value ("open").toString ());
    base.close= native_string (fallback.value ("close").toString ());
    base.open_suffix=
      native_string (fallback.value ("open_suffix").toString ());
    base.close_prefix=
      native_string (fallback.value ("close_prefix").toString ());
    base.apostrophe_opens= fallback.value ("apostrophe_opens").toBool ();
    base.paired= fallback.value ("paired").toBool ();
    styles= root.value ("styles").toObject ();

    for (auto it= styles.begin (); it != styles.end (); ++it) {
      ASSERT (it.value ().isObject (), "quote style must be an object");
      const QJsonObject style= it.value ().toObject ();
      for (auto field= style.begin (); field != style.end (); ++field)
        ASSERT (field.key () == "open" || field.key () == "close" ||
                field.key () == "open_suffix" ||
                field.key () == "close_prefix" ||
                field.key () == "apostrophe_opens" ||
                field.key () == "paired",
                "unknown auto-close quote style field");
    }
  }
};

quote_catalog&
catalog () {
  static quote_catalog result;
  return result;
}

void
apply_string_override (const QJsonObject& source, const char* name,
                       string& target) {
  QJsonValue value= source.value (name);
  if (value.isUndefined ()) return;
  ASSERT (value.isString (), "quote style string expected");
  target= native_string (value.toString ());
}

void
apply_bool_override (const QJsonObject& source, const char* name,
                     bool& target) {
  QJsonValue value= source.value (name);
  if (value.isUndefined ()) return;
  ASSERT (value.isBool (), "quote style boolean expected");
  target= value.toBool ();
}

string
open_text (const quote_style& style) {
  return style.open * style.open_suffix;
}

string
close_text (const quote_style& style) {
  return style.close_prefix * style.close;
}

bool
contains (const array<string>& values, const string& value) {
  for (int i= 0; i < N(values); ++i)
    if (values[i] == value) return true;
  return false;
}

bool
find_opening_quote_in_text (const string& text, int byte) {
  const int limit= std::clamp (byte, 0, N(text));
  const string open= "‘";
  const string close= "’";
  const int open_at= search_backwards (open, limit - N(open), text);
  const int close_at= search_backwards (close, limit - N(close), text);
  return open_at >= 0 && (close_at < 0 || close_at < open_at);
}

bool
find_opening_quote_in_concat (const tree& concat, int child, int byte) {
  if (!is_func (concat, CONCAT) || child < 0 || child >= N(concat)) return false;
  const tree current= concat[child];
  if (is_atomic (current)) {
    const string text= as_string (current);
    if (find_opening_quote_in_text (text, byte)) return true;
    if (search_forwards ("’", 0, text) >= 0) return false;
  }
  if (child == 0) return false;
  const tree previous= concat[child - 1];
  const int previous_end= is_atomic (previous) ? N(as_string (previous)) : 0;
  return find_opening_quote_in_concat (concat, child - 1, previous_end);
}

bool
string_arg (const array<object>& args, int index, string& result) {
  if (index < 0 || index >= N(args) || !is_string (args[index])) return false;
  result= as_string (args[index]);
  return true;
}

bool
bool_arg (const array<object>& args, int index, bool& result) {
  if (index < 0 || index >= N(args) || !is_bool (args[index])) return false;
  result= as_bool (args[index]);
  return true;
}

bool
large_arg (const array<object>& args, int index) {
  return index < N(args) && is_bool (args[index]) && as_bool (args[index]);
}

} // namespace

bool
rest_large_option (object options) {
  if (!is_list (options)) return false;
  const array<object> values= as_array_object (options);
  return N(values) == 1 && is_bool (values[0]) && as_bool (values[0]);
}

quote_style
quote_style_for_language (string language) {
  quote_catalog& data= catalog ();
  quote_style result= data.base;
  QJsonValue value= data.styles.value (qstring (language));
  if (!value.isObject ()) return result;
  const QJsonObject style= value.toObject ();
  apply_string_override (style, "open", result.open);
  apply_string_override (style, "close", result.close);
  apply_string_override (style, "open_suffix", result.open_suffix);
  apply_string_override (style, "close_prefix", result.close_prefix);
  apply_bool_override (style, "apostrophe_opens", result.apostrophe_opens);
  apply_bool_override (style, "paired", result.paired);
  return result;
}

bool
quote_should_close (tree root, path cursor, string language) {
  if (is_nil (cursor)) return false;
  path parent= path_up (cursor);
  if (!has_subtree (root, parent)) return false;
  const int position= last_item (cursor);
  const tree current= subtree (root, parent);
  if (!is_atomic (current)) return position > 0;
  const string text= as_string (current);
  if (text == "" || position <= 0 || position > N(text)) return false;
  const int previous= utf8_grapheme_previous (text, position);
  if (previous < 0 || previous >= position) return false;
  const string grapheme= utf8_byte_slice (text, previous, position);
  quote_style style= quote_style_for_language (language);
  if (contains (style.open_after, grapheme)) return false;
  if (style.apostrophe_opens && grapheme == "'") return false;
  return true;
}

bool
has_opening_single_quote (tree root, path cursor) {
  if (is_nil (cursor)) return false;
  const path atom_path= path_up (cursor);
  if (!has_subtree (root, atom_path)) return false;
  const tree atom= subtree (root, atom_path);
  const int position= last_item (cursor);
  const path parent_path= path_up (atom_path);
  if (has_subtree (root, parent_path)) {
    const tree parent= subtree (root, parent_path);
    if (is_func (parent, CONCAT))
      return find_opening_quote_in_concat (
        parent, last_item (atom_path), position);
  }
  return is_atomic (atom) &&
         find_opening_quote_in_text (as_string (atom), position);
}

void
insert_quote (editor_rep* ed) {
  if (ed == nullptr) return;
  string language= get_preference ("automatic quotes", "default");
  if (language == "default") language= ed->get_env_string ("language");
  const quote_style style= quote_style_for_language (language);
  if (get_preference ("automatic brackets", "mathematics") == "on") {
    if (!style.paired) {
      ed->insert_tree (tree (style.open));
      return;
    }
    const string open= open_text (style);
    ed->insert_tree (tree (open * close_text (style)), path (N(open)));
    return;
  }
  if (quote_should_close (ed->the_root (), ed->the_path (), language))
    ed->insert_tree (tree (close_text (style)));
  else
    ed->insert_tree (tree (open_text (style)));
}

void
insert_apostrophe (editor_rep* ed, bool alternate) {
  if (ed == nullptr) return;
  const bool opening= has_opening_single_quote (ed->the_root (), ed->the_path ());
  ed->insert_tree (tree ((alternate != opening) ? string ("’") : string ("'")));
}

void
make_bracket_open (editor_rep* ed, string left, string right, bool large) {
  if (ed == nullptr) return;
  if (get_preference ("automatic brackets", "mathematics") != "on") {
    ed->insert_tree (large ? tree (LEFT, left) : tree (left));
    return;
  }

  const bool selected= ed->selection_active_normal ();
  tree selection= "";
  if (selected) selection= ed->selection_get_cut ();
  if (large)
    ed->insert_tree (
      tree (CONCAT, tree (LEFT, left), tree (RIGHT, right)), path (1, 0));
  else
    ed->insert_tree (tree (left * right), path (N(left)));
  if (selected) ed->insert_tree (selection, end (selection));
}

void
make_separator (editor_rep* ed, string separator, bool large) {
  if (ed == nullptr) return;
  ed->insert_tree (large ? tree (MID, separator) : tree (separator));
}

void
make_bracket_close (editor_rep* ed, string right, string left, bool large) {
  (void) left;
  if (ed == nullptr) return;
  ed->insert_tree (large ? tree (RIGHT, right) : tree (right));
}

bool
test_matching_brackets () {
  return get_preference ("automatic brackets", "mathematics") != "off";
}

void
toggle_matching_brackets () {
  set_preference (
    "automatic brackets", test_matching_brackets () ? string ("off") :
                                                       string ("mathematics"));
}

void
make_big_operator (editor_rep* ed, string op) {
  if (ed == nullptr) return;
  if (get_preference ("automatic brackets", "mathematics") == "on" &&
      op != ".") {
    const bool selected= ed->selection_active_normal ();
    tree selection= "";
    if (selected) selection= ed->selection_get_cut ();
    ed->insert_tree (
      tree (CONCAT, tree (BIG, op), tree (BIG, ".")), path (1, 0));
    if (selected) ed->insert_tree (selection, end (selection));
    return;
  }
  ed->insert_tree (tree (BIG, op));
}

bool
keyboard_call (string name, const array<object>& args, object& result) {
  editor ed= get_current_editor ();
  editor_rep* target= is_nil (ed) ? nullptr : ed.operator-> ();
  if (name == "insert-quote" && N(args) == 0) {
    insert_quote (target);
    result= object (true);
    return true;
  }
  if (name == "insert-apostrophe" && N(args) == 1) {
    bool alternate;
    if (!bool_arg (args, 0, alternate)) return false;
    insert_apostrophe (target, alternate);
    result= object (true);
    return true;
  }
  if (name == "make-bracket-open" && (N(args) == 2 || N(args) == 3)) {
    string left, right;
    if (!string_arg (args, 0, left) || !string_arg (args, 1, right))
      return false;
    make_bracket_open (target, left, right, large_arg (args, 2));
    result= object (true);
    return true;
  }
  if (name == "make-separator" && (N(args) == 1 || N(args) == 2)) {
    string separator;
    if (!string_arg (args, 0, separator)) return false;
    make_separator (target, separator, large_arg (args, 1));
    result= object (true);
    return true;
  }
  if (name == "make-bracket-close" && (N(args) == 2 || N(args) == 3)) {
    string right, left;
    if (!string_arg (args, 0, right) || !string_arg (args, 1, left))
      return false;
    make_bracket_close (target, right, left, large_arg (args, 2));
    result= object (true);
    return true;
  }
  if (name == "test-matching-brackets?" && N(args) == 0) {
    result= object (test_matching_brackets ());
    return true;
  }
  if (name == "toggle-matching-brackets" && N(args) == 0) {
    toggle_matching_brackets ();
    result= object (true);
    return true;
  }
  if (name == "make-big-operator" && N(args) == 1) {
    string op;
    if (!string_arg (args, 0, op)) return false;
    make_big_operator (target, op);
    result= object (true);
    return true;
  }
  return false;
}

} // namespace athena::auto_close

void
native_insert_quote () {
  editor ed= get_current_editor ();
  athena::auto_close::insert_quote (is_nil (ed) ? nullptr : ed.operator-> ());
}

void
native_insert_apostrophe (bool alternate) {
  editor ed= get_current_editor ();
  athena::auto_close::insert_apostrophe (
    is_nil (ed) ? nullptr : ed.operator-> (), alternate);
}

void
native_make_bracket_open (string left, string right, object options) {
  editor ed= get_current_editor ();
  athena::auto_close::make_bracket_open (
    is_nil (ed) ? nullptr : ed.operator-> (), left, right,
    athena::auto_close::rest_large_option (options));
}

void
native_make_separator (string separator, object options) {
  editor ed= get_current_editor ();
  athena::auto_close::make_separator (
    is_nil (ed) ? nullptr : ed.operator-> (), separator,
    athena::auto_close::rest_large_option (options));
}

void
native_make_bracket_close (string right, string left, object options) {
  editor ed= get_current_editor ();
  athena::auto_close::make_bracket_close (
    is_nil (ed) ? nullptr : ed.operator-> (), right, left,
    athena::auto_close::rest_large_option (options));
}

bool
native_test_matching_brackets () {
  return athena::auto_close::test_matching_brackets ();
}

void
native_toggle_matching_brackets () {
  athena::auto_close::toggle_matching_brackets ();
}

void
native_make_big_operator (string op) {
  editor ed= get_current_editor ();
  athena::auto_close::make_big_operator (
    is_nil (ed) ? nullptr : ed.operator-> (), op);
}
