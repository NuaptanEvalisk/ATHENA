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
#include <cmath>
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

std::string optional_text_field (const QJsonObject& object, const char* key) {
  const auto value= object.value (key);
  if (value.isUndefined () || value.isNull ()) return {};
  require (value.isString (), "Invalid optional named-symbol string field");
  const auto bytes= value.toString ().toUtf8 ();
  std::string result (bytes.constData (), bytes.size ());
  require_utf8 (result);
  require (result.size () <= 1024 && result.find ('\0') == std::string::npos,
           "Invalid optional named-symbol string field");
  return result;
}

std::shared_ptr<const named_symbol_recipe>
parse_recipe (const QJsonValue& value, int depth= 0) {
  require (depth <= 16, "Named-symbol recipe nesting is too deep");
  auto recipe= std::make_shared<named_symbol_recipe> ();
  if (value.isString ()) {
    const auto bytes= value.toString ().toUtf8 ();
    recipe->kind= named_symbol_recipe_kind::glyph;
    recipe->glyph_utf8.assign (bytes.constData (), bytes.size ());
    require_utf8 (recipe->glyph_utf8);
    require (!recipe->glyph_utf8.empty () && recipe->glyph_utf8.size () <= 64 &&
             recipe->glyph_utf8.find ('\0') == std::string::npos,
             "Invalid named-symbol recipe glyph");
    return recipe;
  }
  require (value.isArray (), "Invalid named-symbol recipe");
  const auto array= value.toArray ();
  require (!array.empty () && array[0].isString (), "Invalid named-symbol recipe operation");
  const QString operation= array[0].toString ();
  if (operation == "rotate") {
    require (array.size () == 3 && array[1].isDouble (), "Invalid rotate recipe");
    const double degrees= array[1].toDouble ();
    require (std::isfinite (degrees) && std::abs (degrees) <= 360.0,
             "Invalid rotate angle");
    recipe->kind= named_symbol_recipe_kind::rotate;
    recipe->parameter= degrees;
    recipe->first= parse_recipe (array[2], depth + 1);
    return recipe;
  }
  if (operation == "scale-x") {
    require (array.size () == 3 && array[1].isDouble (), "Invalid scale-x recipe");
    const double factor= array[1].toDouble ();
    require (std::isfinite (factor) && factor >= 0.25 && factor <= 4.0,
             "Invalid scale-x factor");
    recipe->kind= named_symbol_recipe_kind::scale_x;
    recipe->parameter= factor;
    recipe->first= parse_recipe (array[2], depth + 1);
    return recipe;
  }
  require (array.size () == 4 && array[1].isDouble (),
           "Invalid binary named-symbol recipe");
  const double overlap= array[1].toDouble ();
  require (std::isfinite (overlap) && std::abs (overlap) <= 2.0,
           "Invalid named-symbol recipe overlap");
  if (operation == "stack") recipe->kind= named_symbol_recipe_kind::stack;
  else if (operation == "glue-above") recipe->kind= named_symbol_recipe_kind::glue_above;
  else if (operation == "glue-below") recipe->kind= named_symbol_recipe_kind::glue_below;
  else throw std::invalid_argument ("Unknown named-symbol recipe operation");
  recipe->parameter= overlap;
  recipe->first= parse_recipe (array[2], depth + 1);
  recipe->second= parse_recipe (array[3], depth + 1);
  return recipe;
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
    symbol.glyph_utf8= optional_text_field (object, "glyph");
    symbol.virtual_font= optional_text_field (object, "virtual_font");
    symbol.virtual_symbol= optional_text_field (object, "virtual_symbol");
    const auto recipe_value= object.value ("recipe");
    if (!recipe_value.isUndefined () && !recipe_value.isNull ())
      symbol.recipe= parse_recipe (recipe_value);
    const bool native= !symbol.glyph_utf8.empty ();
    const bool virtual_recipe= !symbol.virtual_font.empty () || !symbol.virtual_symbol.empty ();
    const int recipe_count= (native ? 1 : 0) + (virtual_recipe ? 1 : 0) +
                            (symbol.recipe ? 1 : 0);
    require (recipe_count == 1, "Named symbol needs exactly one rendering recipe");
    require (!virtual_recipe || (!symbol.virtual_font.empty () && !symbol.virtual_symbol.empty ()),
             "Incomplete virtual named-symbol recipe");
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
