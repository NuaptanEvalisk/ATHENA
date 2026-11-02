// Copyright (C) 1999 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "secure_expression.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "sys_utils.hpp"
#include "qt_utilities.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <unordered_set>

namespace {
std::unordered_set<std::string> secure_functions;
using environment= std::vector<object>;
bool is_pair (object value) { return tmscm_is_pair (object_to_tmscm (value)); }
bool secure (object expression, const environment& locals);
bool arguments (object values, const environment& locals) {
  for (; is_pair (values); values= cdr (values))
    if (!secure (car (values), locals)) return false;
  return is_null (values);
}
bool bind (object names, environment& locals) {
  while (is_pair (names)) {
    if (!is_symbol (car (names))) return false;
    locals.push_back (car (names));
    names= cdr (names);
  }
  if (is_null (names)) return true;
  if (!is_symbol (names)) return false;
  locals.push_back (names);
  return true;
}
bool quasiquote (object value, const environment& locals) {
  if (!is_pair (value)) return true;
  object head= car (value), tail= cdr (value);
  if ((head == symbol_object ("unquote") || head == symbol_object ("unquote-splicing")) &&
      is_pair (tail) && is_null (cdr (tail))) return secure (car (tail), locals);
  return quasiquote (head, locals) && quasiquote (tail, locals);
}
bool secure (object expression, const environment& locals) {
  if (!is_pair (expression))
    return is_symbol (expression) || scm_is_number (object_to_tmscm (expression)) || is_string (expression) ||
           is_tree (expression) || is_null (expression) || is_bool (expression);
  object head= car (expression), tail= cdr (expression);
  if (head == symbol_object ("and") || head == symbol_object ("begin") ||
      head == symbol_object ("if") || head == symbol_object ("or") ||
      head == symbol_object ("set!")) return arguments (tail, locals);
  if (head == symbol_object ("cond")) {
    for (; is_pair (tail); tail= cdr (tail)) {
      object clause= car (tail);
      if (!is_pair (clause) || !is_pair (cdr (clause))) return false;
      if (car (clause) != symbol_object ("else") && !secure (car (clause), locals)) return false;
      if (!secure (car (cdr (clause)), locals)) return false;
    }
    return is_null (tail);
  }
  if (head == symbol_object ("lambda")) {
    if (!is_pair (tail)) return false;
    environment scope= locals;
    return bind (car (tail), scope) && arguments (cdr (tail), scope);
  }
  if (std::find (locals.begin (), locals.end (), head) != locals.end ()) return arguments (tail, locals);
  if (head == symbol_object ("quote")) return true;
  if (head == symbol_object ("quasiquote")) return quasiquote (tail, locals);
  if (is_symbol (head))
    return native_secure_function (as_symbol (head)) && arguments (tail, locals);
  return arguments (expression, locals);
}
}

bool native_secure_expression (object expression) { return secure (expression, {}); }
bool native_secure_function (string name) {
  return secure_functions.count (std::string (name.data (), N(name))) != 0;
}
object native_secure_eval (object expression) {
  return native_secure_expression (expression) ? eval (expression) : object (false);
}
void native_secure_initialize () {
  QFile input (to_qstring (as_string (get_texmacs_path () * "misc/secure-symbols.json", URL_SYSTEM)));
  if (!input.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot load secure symbol registry");
  QJsonParseError error;
  const QJsonDocument document= QJsonDocument::fromJson (input.readAll (), &error);
  if (error.error != QJsonParseError::NoError || !document.isArray ())
    throw std::runtime_error ("Invalid secure symbol registry");
  std::unordered_set<std::string> names;
  for (const auto& entry: document.array ()) {
    if (!entry.isString ()) throw std::runtime_error ("Secure symbol name must be a string");
    names.insert (entry.toString ().toStdString ());
  }
  secure_functions= std::move (names);
}
