/******************************************************************************
* MODULE     : document_style_commands.cpp
* DESCRIPTION: Actor-owned generic document style commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "document_style_commands.hpp"
#include "boot.hpp"
#include "analyze.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "new_buffer.hpp"
#include "new_style.hpp"
#include "sys_utils.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

QString
style_qstring (string value) {
  return QString::fromUtf8 (value.data (), N(value));
}

string
style_string (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

struct style_catalog_data {
  QJsonObject menu_names;
  QJsonObject synopses;
  QJsonObject categories;
  QJsonObject includes;
  QJsonObject defaults;
  QJsonArray themes;
  QJsonArray poster_title_styles;
  QJsonArray precedes;

  style_catalog_data () {
    string source;
    ASSERT (!load_string (url ("$ATHENA_PATH/misc/styles/catalog.json"),
                          source, false),
            "cannot read style catalog");
    c_string bytes (source);
    QJsonParseError error;
    QJsonDocument document=
      QJsonDocument::fromJson (QByteArray (bytes, N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid style catalog");
    QJsonObject root= document.object ();
    ASSERT (root.value ("version").toInt () == 1 &&
            root.value ("menu_names").isObject () &&
            root.value ("synopses").isObject () &&
            root.value ("categories").isObject () &&
            root.value ("includes").isObject () &&
            root.value ("defaults").isObject () &&
            root.value ("themes").isArray () &&
            root.value ("poster_title_styles").isArray () &&
            root.value ("precedes").isArray (),
            "unsupported style catalog schema");
    menu_names= root.value ("menu_names").toObject ();
    synopses= root.value ("synopses").toObject ();
    categories= root.value ("categories").toObject ();
    includes= root.value ("includes").toObject ();
    defaults= root.value ("defaults").toObject ();
    themes= root.value ("themes").toArray ();
    poster_title_styles= root.value ("poster_title_styles").toArray ();
    precedes= root.value ("precedes").toArray ();
  }

  bool array_contains (const QJsonArray& values, string value) const {
    const QString target= style_qstring (value);
    for (const QJsonValue& candidate: values)
      if (candidate.isString () && candidate.toString () == target) return true;
    return false;
  }

  bool theme (string value) const { return array_contains (themes, value); }

  bool category (string category_name, string value) const {
    QJsonValue raw= categories.value (style_qstring (category_name));
    return raw.isArray () && array_contains (raw.toArray (), value);
  }

  bool includes_style (string owner, string included) const {
    QJsonValue raw= includes.value (style_qstring (owner));
    return raw.isArray () && array_contains (raw.toArray (), included);
  }

  bool precedes_category (string left, string right) const {
    const QString qleft= style_qstring (left);
    const QString qright= style_qstring (right);
    for (const QJsonValue& value: precedes) {
      if (!value.isArray ()) continue;
      QJsonArray pair= value.toArray ();
      if (pair.size () == 2 && pair[0].isString () && pair[1].isString () &&
          pair[0].toString () == qleft && pair[1].toString () == qright)
        return true;
    }
    return false;
  }

  string lookup (const QJsonObject& object, string key) const {
    QJsonValue value= object.value (style_qstring (key));
    return value.isString () ? style_string (value.toString ()) : string ("");
  }

  array<string> strings (const QJsonArray& source) const {
    array<string> result;
    for (const QJsonValue& value: source)
      if (value.isString ()) result << style_string (value.toString ());
    return result;
  }
};

style_catalog_data&
style_catalog () {
  static style_catalog_data result;
  return result;
}

editor_rep*
current_style_editor () {
  editor ed= get_current_editor ();
  return is_nil (ed) ? nullptr : ed.operator-> ();
}

string owner_style_category (editor_rep* ed, string style);
bool owner_style_includes (string style, string included);
bool owner_style_overrides (editor_rep* ed, string left, string right);
bool owner_style_precedes (editor_rep* ed, string left, string right);

array<string>
style_strings_from_tree (tree style) {
  array<string> result;
  if (is_atomic (style)) {
    result << as_string (style);
    return result;
  }
  if (!is_compound (style, "tuple")) return result;
  for (int i= 0; i < N (style); ++i)
    if (is_atomic (style[i])) result << as_string (style[i]);
  return result;
}

array<string> current_style_strings () {
  return style_strings_from_tree (get_current_editor ()->get_style ());
}

array<string> current_style_strings (editor_rep* ed) {
  return ed == nullptr ? array<string> () :
                         style_strings_from_tree (ed->get_style ());
}

object style_list_object (array<string> styles) {
  array<object> objects;
  for (int i= 0; i < N (styles); ++i) objects << object (styles[i]);
  return as_list_object (objects);
}

bool contains_style (array<string> styles, string value);

bool style_arrays_equal (array<string> left, array<string> right) {
  if (N (left) != N (right)) return false;
  for (int i= 0; i < N (left); ++i)
    if (left[i] != right[i]) return false;
  return true;
}

array<string> style_tail (array<string> styles) {
  array<string> result;
  for (int i= 1; i < N (styles); ++i) result << styles[i];
  return result;
}

enum class style_relation { overrides, precedes, includes };

bool style_relation_any (array<string> styles, style_relation relation,
                         string value) {
  editor_rep* ed= current_style_editor ();
  for (int i= 0; i < N (styles); ++i)
    if ((relation == style_relation::overrides &&
         owner_style_overrides (ed, styles[i], value)) ||
        (relation == style_relation::precedes &&
         owner_style_precedes (ed, styles[i], value)) ||
        (relation == style_relation::includes &&
         owner_style_includes (styles[i], value)))
      return true;
  return false;
}

array<string> normalize_style_list_star (array<string> styles) {
  if (N (styles) == 0) return styles;
  string first= styles[0];
  array<string> tail= style_tail (styles);

  if (style_relation_any (tail, style_relation::overrides, first))
    return normalize_style_list_star (tail);

  if (style_relation_any (tail, style_relation::precedes, first)) {
    // Legacy Scheme computes (list-delete tail predicate). Style entries are
    // strings and the deleted value is a procedure, so the tail is unchanged.
    array<string> normalized= normalize_style_list_star (tail);
    array<string> result;
    result << normalized[0];
    array<string> reordered;
    reordered << first;
    for (int i= 1; i < N (normalized); ++i) reordered << normalized[i];
    array<string> remainder= normalize_style_list_star (reordered);
    for (int i= 0; i < N (remainder); ++i) result << remainder[i];
    return result;
  }

  array<string> result;
  result << first;
  array<string> remainder= normalize_style_list_star (tail);
  for (int i= 0; i < N (remainder); ++i) result << remainder[i];
  return result;
}

array<string> normalize_style_list_starstar (array<string> styles,
                                             array<string> before) {
  if (N (styles) == 0) return styles;
  string first= styles[0];
  array<string> next_before= before;
  next_before << first;
  array<string> remainder=
    normalize_style_list_starstar (style_tail (styles), next_before);
  if (style_relation_any (before, style_relation::includes, first)) return remainder;

  array<string> result;
  result << first;
  for (int i= 0; i < N (remainder); ++i) result << remainder[i];
  return result;
}

array<string> normalize_style_list (array<string> styles) {
  array<string> unique;
  for (int i= 0; i < N (styles); ++i)
    if (!contains_style (unique, styles[i])) unique << styles[i];
  if (N (unique) == 0) return unique;

  array<string> before;
  before << unique[0];
  array<string> normalized=
    normalize_style_list_starstar (normalize_style_list_star (style_tail (unique)),
                                   before);
  array<string> result;
  result << unique[0];
  for (int i= 0; i < N (normalized); ++i) result << normalized[i];
  return result;
}

bool object_to_style_strings (object value, array<string>& styles) {
  if (!is_list (value)) return false;
  array<object> items= as_array_object (value);
  for (int i= 0; i < N (items); ++i) {
    if (!is_string (items[i])) return false;
    styles << as_string (items[i]);
  }
  return true;
}

void set_style_strings (array<string> styles) {
  array<string> normalized= normalize_style_list (styles);
  if (style_arrays_equal (normalized, current_style_strings ())) return;
  array<tree> children;
  for (int i= 0; i < N (normalized); ++i) children << tree (normalized[i]);
  get_current_editor ()->change_style (tree (TUPLE, children));
}

bool contains_style (array<string> styles, string value) {
  for (int i= 0; i < N (styles); ++i)
    if (styles[i] == value) return true;
  return false;
}

bool
owner_style_includes (string style, string included) {
  return style_catalog ().includes_style (style, included);
}

string
owner_style_category (editor_rep* ed, string style) {
  style_catalog_data& catalog= style_catalog ();
  for (const char* category: {"program-theme", "theorem-decorations",
                              "beamer-title-theme", "poster-title-style"})
    if (catalog.category (category, style)) return ":" * string (category);
  if (catalog.theme (style)) {
    if (ed != nullptr && ed->defined_at_init ("poster-style"))
      return ":poster-theme";
    if (ed != nullptr && ed->defined_at_init ("beamer-style"))
      return ":beamer-theme";
    return ":basic-theme";
  }
  return style;
}

bool
owner_style_overrides (editor_rep* ed, string left, string right) {
  return owner_style_category (ed, left) == owner_style_category (ed, right);
}

bool
owner_style_precedes (editor_rep* ed, string left, string right) {
  return style_catalog ().precedes_category (
    owner_style_category (ed, left), owner_style_category (ed, right));
}

string
style_category_object_name (object value) {
  if (is_symbol (value)) return as_symbol (value);
  if (is_string (value)) return as_string (value);
  return "";
}

} // namespace

object
document_style_category (string style) {
  string category= owner_style_category (current_style_editor (), style);
  return starts (category, ":") ? symbol_object (category) : object (category);
}

bool
document_style_category_overrides (object left, object right) {
  string l= style_category_object_name (left);
  string r= style_category_object_name (right);
  return l != "" && l == r;
}

bool
document_style_category_precedes (object left, object right) {
  return style_catalog ().precedes_category (
    style_category_object_name (left), style_category_object_name (right));
}

bool
document_style_includes (string style, string included) {
  return owner_style_includes (style, included);
}

bool
document_style_overrides (string left, string right) {
  return owner_style_overrides (current_style_editor (), left, right);
}

bool
document_style_precedes (string left, string right) {
  return owner_style_precedes (current_style_editor (), left, right);
}

object
document_style_get_documentation (object style) {
  // Menu help receives unevaluated command arguments, not just style strings.
  if (!is_string (style)) return object (false);
  string synopsis= style_catalog ().lookup (style_catalog ().synopses,
                                            as_string (style));
  return synopsis == "" ? object (false) : object (synopsis);
}

string
document_style_get_menu_name (string style) {
  string value= style_catalog ().lookup (style_catalog ().menu_names, style);
  return value == "" ? upcase_first (style) : value;
}

string
document_custom_style_file_name (url name) {
  string file= as_system_string (tail (name));
  if (ends (file, ".ats")) return file (0, N (file) - 4);
  if (ends (file, ".ts")) return file (0, N (file) - 3);
  return file;
}

url
document_url_resolve_package (string name) {
  url search= head (get_current_buffer_safe ()) | url ("$ATHENA_STYLE_PATH");
  return resolve_style_file (name, search, true);
}

bool
document_install_custom_style (url source) {
  string source_s= as_string (concretize (source), URL_SYSTEM);
  string home_s= get_env ("ATHENA_HOME_PATH");
  if (N(source_s) == 0 || N(home_s) == 0) return false;
  std::filesystem::path input (
    std::string (source_s.data (), (std::size_t) N(source_s)));
  std::filesystem::path target (
    std::string (home_s.data (), (std::size_t) N(home_s)));
  target/= "styles";
  target/= input.stem ();
  target.replace_extension (".ats");
  std::string error;
  if (!install_style_file (input, target, error)) {
    std_warning << "Could not install custom style: " << string (error.c_str ()) << LF;
    return false;
  }
  style_invalidate_cache ();
  return true;
}

object
document_get_style_list () {
  return style_list_object (current_style_strings ());
}

object
document_get_style_list (editor_rep* ed) {
  return style_list_object (current_style_strings (ed));
}

void
document_set_style_list (object value) {
  array<string> styles;
  if (!object_to_style_strings (value, styles)) return;
  set_style_strings (styles);
}

object
document_embedded_style_list (object extra_packages) {
  array<string> result= current_style_strings ();
  if (is_list (extra_packages)) {
    array<object> extras= as_array_object (extra_packages);
    for (int i= 0; i < N (extras); ++i) {
      if (!is_string (extras[i])) continue;
      string package= as_string (extras[i]);
      if (!contains_style (result, package)) result << package;
    }
  }
  return style_list_object (result);
}

bool
document_has_no_style () {
  return N (current_style_strings ()) == 0;
}

void
document_set_no_style () {
  set_style_strings (array<string> ());
}

bool
document_has_main_style (string style) {
  array<string> styles= current_style_strings ();
  return N (styles) > 0 && styles[0] == style;
}

bool
document_has_main_style (editor_rep* ed, string style) {
  array<string> styles= current_style_strings (ed);
  return N (styles) > 0 && styles[0] == style;
}

void
document_set_main_style (string style) {
  array<string> styles= current_style_strings ();
  if (N(styles) == 0) styles << style;
  else styles[0]= style;
  set_style_strings (styles);
}

void
document_notify_new_style (string) {
}

bool
document_has_style_package (string package) {
  array<string> styles= current_style_strings ();
  if (contains_style (styles, package)) return true;

  bool included= false;
  bool overridden= false;
  for (int i= 0; i < N (styles); ++i) {
    if (owner_style_includes (styles[i], package)) included= true;
    if (owner_style_overrides (current_style_editor (), styles[i], package))
      overridden= true;
  }
  return included && !overridden;
}

bool
document_has_style_package (editor_rep* ed, string package) {
  array<string> styles= current_style_strings (ed);
  if (contains_style (styles, package)) return true;

  bool included= false;
  bool overridden= false;
  for (int i= 0; i < N (styles); ++i) {
    if (owner_style_includes (styles[i], package)) included= true;
    if (owner_style_overrides (ed, styles[i], package)) overridden= true;
  }
  return included && !overridden;
}

string
document_current_basic_theme (editor_rep* ed) {
  array<string> styles= current_style_strings (ed);
  for (int i= 0; i < N(styles); ++i)
    if (style_catalog ().theme (styles[i])) return styles[i];
  return "plain";
}

array<string>
document_theme_names () {
  return style_catalog ().strings (style_catalog ().themes);
}

array<string>
document_poster_title_styles () {
  return style_catalog ().strings (style_catalog ().poster_title_styles);
}

bool
document_not_has_style_package (string package) {
  return !document_has_style_package (package);
}

void
document_add_style_package (string package) {
  array<string> styles= current_style_strings ();
  styles << package;
  set_style_strings (styles);
}

void
document_remove_style_package (string package) {
  array<string> styles= current_style_strings ();
  array<string> filtered;
  for (int i= 0; i < N (styles); ++i)
    if (styles[i] != package) filtered << styles[i];
  set_style_strings (filtered);
}

void
document_remove_style_package_star (string package) {
  document_remove_style_package (package);
}

void
document_toggle_style_package (string package) {
  if (document_has_style_package (package))
    document_remove_style_package (package);
  else
    document_add_style_package (package);
}
