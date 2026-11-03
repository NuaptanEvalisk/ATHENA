// Copyright (C) 1999 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "native_command_security.hpp"
#include "sys_utils.hpp"
#include "qt_utilities.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <stdexcept>
#include <unordered_set>

namespace {
std::unordered_set<std::string> secure_functions;
}

bool native_secure_function (string name) {
  return secure_functions.count (std::string (name.data (), N(name))) != 0;
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
