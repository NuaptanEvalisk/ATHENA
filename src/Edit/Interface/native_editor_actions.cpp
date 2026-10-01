/******************************************************************************
* MODULE     : native_editor_actions.cpp
* DESCRIPTION: Validated parameterized native editor actions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "native_editor_actions.hpp"

#include "format_commands.hpp"
#include "scheme.hpp"

#include <QJsonArray>
#include <QJsonValue>

namespace {

string
native_action_string (const QJsonValue& value) {
  ASSERT (value.isString (), "native editor action string expected");
  const QString text= value.toString ();
  const QByteArray bytes= text.toUtf8 ();
  ASSERT (QString::fromUtf8 (bytes) == text,
          "invalid UTF-8 native editor action string");
  return string (bytes.constData (), bytes.size ());
}

tree
native_action_tree (const QJsonValue& value) {
  if (value.isString ()) return tree (native_action_string (value));
  ASSERT (value.isArray (),
          "native editor action tree must be string or array");
  const QJsonArray values= value.toArray ();
  ASSERT (!values.isEmpty () && values[0].isString (),
          "native editor action compound tree needs a tag");
  tree result (as_tree_label (native_action_string (values[0])),
               values.size () - 1);
  for (int i= 1; i < values.size (); ++i)
    result[i - 1]= native_action_tree (values[i]);
  return result;
}

bool
has_string (const QJsonObject& object, const char* name) {
  return object.value (QString::fromLatin1 (name)).isString ();
}

bool
has_bool (const QJsonObject& object, const char* name) {
  return object.value (QString::fromLatin1 (name)).isBool ();
}

bool
valid_large_mode (const QJsonValue& value) {
  return value.isBool () ||
         (value.isString () && value.toString () == "default");
}

int
native_action_large_mode (const QJsonValue& value) {
  if (value.isBool ()) return value.toBool () ? 1 : 0;
  ASSERT (value.isString () && value.toString () == "default",
          "native editor bracket large mode expected");
  return -1;
}

bool
fail_validation (QString* error, const QString& message) {
  if (error != nullptr) *error= message;
  return false;
}

} // namespace

bool
native_editor_action_validate (const QJsonObject& action, QString* error) {
  const QJsonValue opValue= action.value ("op");
  if (!opValue.isString () || opValue.toString ().trimmed ().isEmpty ())
    return fail_validation (error, "editor action requires non-empty op");
  const QString op= opValue.toString ();

  if (op == "insert-tree")
    return action.contains ("value") &&
           (action.value ("value").isString () ||
            action.value ("value").isArray ()) ?
             true : fail_validation (error, "insert-tree requires value");
  if (op == "make")
    return has_string (action, "tag") &&
           (!action.contains ("arity") || action.value ("arity").isDouble ()) ?
             true : fail_validation (error, "make requires tag and optional arity");
  if (op == "make-with" || op == "make-style-with" ||
      op == "make-line-with")
    return has_string (action, "var") && has_string (action, "value") ?
             true : fail_validation (error, op + " requires var/value");
  if (op == "interactive-line-with")
    return has_string (action, "var") ?
             true : fail_validation (error, "interactive-line-with requires var");
  if (op == "make-mod-active")
    return has_string (action, "tag") ?
             true : fail_validation (error, "make-mod-active requires tag");
  if (op == "make-script")
    return has_bool (action, "sup") && has_bool (action, "right") ?
             true : fail_validation (error, "make-script requires sup/right");
  if (op == "make-wide" || op == "make-wide-under")
    return has_string (action, "value") &&
           (!action.contains ("stretch") || action.value ("stretch").isBool ()) ?
             true : fail_validation (error, op + " requires value");
  if (op == "insert-big")
    return action.contains ("value") ?
             true : fail_validation (error, "insert-big requires value");
  if (op == "emulate-keyboard")
    return has_string (action, "keys") ?
             true : fail_validation (error, "emulate-keyboard requires keys");
  if (op == "make-space")
    return has_string (action, "value") ?
             true : fail_validation (error, "make-space requires value");
  if (op == "math-bracket-open" || op == "math-bracket-close")
    return has_string (action, "left") && has_string (action, "right") &&
           valid_large_mode (action.value ("large")) ?
             true : fail_validation (
               error, op + " requires left/right/large");
  if (op == "math-separator")
    return has_string (action, "value") &&
           valid_large_mode (action.value ("large")) ?
             true : fail_validation (
               error, "math-separator requires value/large");
  if (op == "insert-long-arrow")
    return has_string (action, "value") &&
           (!action.contains ("below") || has_bool (action, "below")) ?
             true : fail_validation (
               error, "insert-long-arrow requires value and optional below");
  if (op == "insert-alphabet")
    return has_string (action, "tag") && has_string (action, "value") ?
             true : fail_validation (
               error, "insert-alphabet requires tag/value");

  if (op == "make-fraction" || op == "make-sqrt" ||
      op == "make-var-sqrt" || op == "make-neg" ||
      op == "make-above" || op == "make-below")
    return true;

  return fail_validation (error, "unsupported native editor action op: " + op);
}

void
native_editor_action_execute (editor ed, const QJsonObject& action) {
  ASSERT (!is_nil (ed), "native editor action without editor");
  QString error;
  ASSERT (native_editor_action_validate (action, &error),
          "invalid native editor action");
  const string op= native_action_string (action.value ("op"));

  if (op == "insert-tree")
    ed->insert_tree (native_action_tree (action.value ("value")));
  else if (op == "make") {
    int arity= action.contains ("arity") ?
      action.value ("arity").toInt (-1) : -1;
    const string tag= native_action_string (action.value ("tag"));
    if (arity < 0) (void) call ("make", symbol_object (tag));
    else (void) call ("make", symbol_object (tag), object (arity));
  }
  else if (op == "make-with")
    (void) call (
      "make-with",
      object (native_action_string (action.value ("var"))),
      object (native_action_string (action.value ("value"))));
  else if (op == "make-style-with")
    ed->make_style_with (native_action_string (action.value ("var")),
                         native_action_string (action.value ("value")));
  else if (op == "make-line-with")
    format_make_line_with (
      native_action_string (action.value ("var")),
      tree (native_action_string (action.value ("value"))));
  else if (op == "interactive-line-with")
    (void) call (
      "make-interactive-line-with",
      object (native_action_string (action.value ("var"))));
  else if (op == "make-mod-active")
    ed->make_mod_active (
      as_tree_label (native_action_string (action.value ("tag"))));
  else if (op == "make-script")
    ed->make_script (action.value ("sup").toBool (),
                     action.value ("right").toBool ());
  else if (op == "make-fraction") ed->make_fraction ();
  else if (op == "make-sqrt") ed->make_sqrt ();
  else if (op == "make-var-sqrt") ed->make_var_sqrt ();
  else if (op == "make-neg") ed->make_neg ();
  else if (op == "make-above") ed->make_above ();
  else if (op == "make-below") ed->make_below ();
  else if (op == "make-wide" || op == "make-wide-under") {
    const string value= native_action_string (action.value ("value"));
    const bool stretch= action.value ("stretch").toBool (false);
    if (op == "make-wide") ed->make_wide (value, stretch);
    else ed->make_wide_under (value, stretch);
  }
  else if (op == "insert-big")
    ed->insert_tree (tree (BIG, native_action_tree (action.value ("value"))));
  else if (op == "emulate-keyboard")
    ed->emulate_keyboard (native_action_string (action.value ("keys")));
  else if (op == "make-space")
    ed->make_space (native_action_string (action.value ("value")));
  else if (op == "math-bracket-open")
    ed->math_bracket_open (
      native_action_string (action.value ("left")),
      native_action_string (action.value ("right")),
      native_action_large_mode (action.value ("large")));
  else if (op == "math-bracket-close")
    ed->math_bracket_close (
      native_action_string (action.value ("right")),
      native_action_string (action.value ("left")),
      native_action_large_mode (action.value ("large")));
  else if (op == "math-separator")
    ed->math_separator (
      native_action_string (action.value ("value")),
      native_action_large_mode (action.value ("large")));
  else if (op == "insert-long-arrow") {
    const string value= native_action_string (action.value ("value"));
    const bool below= action.value ("below").toBool (false);
    tree arrow= below ? tree (LONG_ARROW, tree (value), "", "") :
                        tree (LONG_ARROW, tree (value), "");
    ed->insert_tree (arrow, below ? path (2, 0) : path (1, 0));
  }
  else if (op == "insert-alphabet")
    ed->insert_tree (
      tree (as_tree_label (native_action_string (action.value ("tag"))),
            tree (native_action_string (action.value ("value")))));
  else
    FAILED ("unhandled validated native editor action");
}
