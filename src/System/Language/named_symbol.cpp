/******************************************************************************
* MODULE     : named_symbol.cpp
* DESCRIPTION: Validated symbol recipes independent of legacy text encodings
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "named_symbol.hpp"
#include "unicode_text.hpp"
#include "language.hpp"
#include "file.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <stdexcept>

namespace athena::text {
namespace {
constexpr int maximum_bytes= 4 * 1024 * 1024;

void require (bool valid, const char* message) {
  if (!valid) throw std::invalid_argument (message);
}

int math_class (const QString& name) {
  static const std::map<QString, int> classes {
    {"symbol", OP_SYMBOL}, {"unary", OP_UNARY}, {"binary", OP_BINARY},
    {"n-ary", OP_N_ARY}, {"prefix", OP_PREFIX}, {"postfix", OP_POSTFIX},
    {"infix", OP_INFIX}, {"prefix-infix", OP_PREFIX_INFIX},
    {"separator", OP_SEPARATOR}, {"opening-bracket", OP_OPENING_BRACKET},
    {"middle-bracket", OP_MIDDLE_BRACKET}, {"closing-bracket", OP_CLOSING_BRACKET}
  };
  const auto found= classes.find (name);
  require (found != classes.end (), "Unknown named-symbol math class");
  return found->second;
}

std::string text_field (const QJsonObject& object, const char* key) {
  const auto value= object.value (key);
  require (value.isString (), "Missing named-symbol string field");
  const auto bytes= value.toString ().toUtf8 ();
  std::string result (bytes.constData (), bytes.size ());
  require_utf8 (result);
  require (!result.empty () && result.size () <= 1024 &&
           result.find ('\0') == std::string::npos,
           "Invalid named-symbol string field");
  return result;
}
}

named_symbol_registry::named_symbol_registry (std::string_view json) {
  require (json.size () <= maximum_bytes, "Named-symbol registry exceeds size limit");
  require_utf8 (json);
  QJsonParseError error;
  const auto document= QJsonDocument::fromJson (
    QByteArray (json.data (), static_cast<int> (json.size ())), &error);
  require (error.error == QJsonParseError::NoError && document.isObject (),
           "Invalid named-symbol registry JSON");
  const auto root= document.object ();
  require (root.value ("version") == QJsonValue (1) &&
           root.value ("symbols").isArray (), "Unsupported named-symbol registry");
  for (const auto& entry: root.value ("symbols").toArray ()) {
    require (entry.isObject (), "Invalid named-symbol entry");
    const auto object= entry.toObject ();
    named_symbol_definition symbol;
    symbol.identity= text_field (object, "identity");
    symbol.glyph_utf8= text_field (object, "glyph");
    symbol.op_type= math_class (QString::fromStdString (text_field (object, "math_class")));
    const auto slant= text_field (object, "slant");
    require (slant == "upright" || slant == "italic", "Invalid named-symbol slant");
    symbol.italic= slant == "italic";
    require (definitions_.emplace (symbol.identity, symbol).second,
             "Duplicate named-symbol identity");
  }
}

const named_symbol_definition* named_symbol_registry::lookup (
  std::string_view identity) const {
  const auto found= definitions_.find (identity);
  return found == definitions_.end () ? nullptr : &found->second;
}

const named_symbol_registry& standard_named_symbols () {
  static const named_symbol_registry registry= [] {
    const auto name= concretize (url ("$ATHENA_PATH/misc/symbols/named-symbols.json"));
    QFile file (QString::fromUtf8 (name.data (), N(name)));
    if (!file.open (QIODevice::ReadOnly) || file.size () > maximum_bytes)
      throw std::runtime_error ("Cannot read named-symbol registry");
    const auto bytes= file.readAll ();
    return named_symbol_registry (std::string_view (bytes.constData (), bytes.size ()));
  } ();
  return registry;
}

} // namespace athena::text
