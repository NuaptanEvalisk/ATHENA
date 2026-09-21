
/******************************************************************************
* MODULE     : font_database.cpp
* DESCRIPTION: Database with the available fonts
* COPYRIGHT  : (C) 2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "font.hpp"
#include "message.hpp"
#include "boot.hpp"

#include "iterator.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "merge_sort.hpp"
#include "Freetype/tt_file.hpp"
#include "Freetype/tt_tools.hpp"
#include "data_cache.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <mutex>

void font_database_filter_features ();
static void font_database_guess_features ();

static bool
font_database_has_extension (string name, string ext) {
  return ends (locase_all (name), ext);
}

static bool
font_database_is_tt_file (string name) {
  return font_database_has_extension (name, ".ttf") ||
         font_database_has_extension (name, ".ttc") ||
         font_database_has_extension (name, ".otf");
}

#define GLOBAL_FEATURES "$ATHENA_PATH/fonts/font-features.scm"
#define GLOBAL_SUBSTITUTIONS "$ATHENA_PATH/fonts/font-substitutions.scm"
#define CHARACTERISTICS_CACHE \
  "$ATHENA_HOME_PATH/system/cache/font-characteristics.json"
#define CHARACTERISTICS_CACHE_VERSION 1

static string
font_database_cache_stamp (url u) {
  if (!exists (u)) return "-";
  return as_string (file_size (u)) * ":" *
         as_string (last_modified (u, false));
}

string
font_database_cache_signature () {
  array<url> files;
  files << url (GLOBAL_FEATURES)
        << url (GLOBAL_SUBSTITUTIONS);
  string r= "3;catalog=" * tt_font_catalog_signature ();
  for (int i=0; i<N(files); i++)
    r << ";" << font_database_cache_stamp (files[i]);
  return r;
}

/******************************************************************************
* Additional comparison operators
******************************************************************************/

bool
locase_less_eq (string s1, string s2) {
  string l1= locase_all (s1);
  string l2= locase_all (s2);
  return l1 <= l2 || (l1 == l2 && s1 <= s2);
}

struct locase_less_eq_operator {
  static bool leq (string s1, string s2) {
    return locase_less_eq (s1, s2);
  }
};

/******************************************************************************
* Global management of the font database
******************************************************************************/

static bool fonts_loaded= false;
static bool fonts_loading= false;
static std::recursive_mutex font_database_mutex;
hashmap<tree,tree> font_table (UNINIT);
hashmap<tree,tree> font_features (UNINIT);
hashmap<tree,tree> font_variants (UNINIT);
hashmap<tree,tree> font_characteristics (UNINIT);
static hashmap<tree,tree> font_catalog_characteristics (UNINIT);
hashmap<string,tree> font_substitutions (UNINIT);
static bool font_database_families_cached= false;
static array<string> font_database_families_cache;
static hashmap<string,tree> font_database_styles_cache (UNINIT);
static hashmap<tree,tree> font_database_characteristics_cache (UNINIT);

static QString
font_qstring (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
font_string (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

static void
font_database_invalidate_selectors () {
  font_database_families_cached= false;
  font_database_families_cache= array<string> ();
  font_database_styles_cache= hashmap<string,tree> (UNINIT);
  font_database_characteristics_cache= hashmap<tree,tree> (UNINIT);
}

void
tuple_insert (tree& t, tree x) {
  for (int i=0; i<N(t); i++)
    if (t[i] == x) return;
  t << x;
}

static void
font_database_load_catalog (bool refresh) {
  font_database_invalidate_selectors ();
  font_table= hashmap<tree,tree> (UNINIT);
  font_catalog_characteristics= hashmap<tree,tree> (UNINIT);
  tree catalog= tt_font_catalog (refresh);
  for (int i=0; i<N(catalog); i++)
    if (is_func (catalog[i], TUPLE) && N(catalog[i]) >= 4 &&
        is_atomic (catalog[i][0]) && is_atomic (catalog[i][1]) &&
        is_atomic (catalog[i][2]) && is_atomic (catalog[i][3])) {
      tree key= tuple (catalog[i][0], catalog[i][1]);
      tree im = tuple (catalog[i][2], catalog[i][3], "0");
      tree all (TUPLE);
      if (font_table->contains (key)) all= font_table[key];
      tuple_insert (all, im);
      font_table (key)= all;
      if (N(catalog[i]) >= 5 && is_func (catalog[i][4], TUPLE))
        font_catalog_characteristics (key)= catalog[i][4];
    }

  // Platforms without a native catalog still discover their actual font files.
  if (N(font_table) == 0) font_database_build (tt_font_path ());
}

void
font_database_load_features (url u) {
  if (!exists (u)) return;
  string s;
  if (!load_string (u, s, false)) {
    tree t= block_to_scheme_tree (s);
    for (int i=0; i<N(t); i++)
      if (is_func (t[i], TUPLE) && (N(t[i]) >= 2)) {
        tree key= t[i][0];
        tree im = t[i] (1, N(t[i]));
        font_features (key)= im;
        tree vars (TUPLE);
        if (font_variants->contains (t[i][1]))
          vars= font_variants [t[i][1]];
        tuple_insert (vars, t[i][0]);
        font_variants (t[i][1])= vars;
      }
  }
}

static bool
font_database_load_characteristics_cache () {
  url u= CHARACTERISTICS_CACHE;
  if (!exists (u)) return false;
  string source;
  if (load_string (u, source, false)) return false;

  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (as_charp (source), N(source)), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ())
    return false;

  QJsonObject root= document.object ();
  if (root.value ("format").toString () != "athena-font-characteristics" ||
      root.value ("version").toInt () != CHARACTERISTICS_CACHE_VERSION ||
      root.value ("catalogSignature").toString () !=
        font_qstring (tt_font_catalog_signature ()))
    return false;

  QJsonArray entries= root.value ("entries").toArray ();
  hashmap<tree,tree> loaded (UNINIT);
  for (const QJsonValue& value: entries) {
    if (!value.isObject ()) continue;
    QJsonObject entry= value.toObject ();
    QString family= entry.value ("family").toString ();
    QString style= entry.value ("style").toString ();
    if (family.isEmpty () || style.isEmpty ()) continue;
    tree characteristics (TUPLE);
    for (const QJsonValue& item: entry.value ("values").toArray ())
      if (item.isString ()) characteristics << tree (font_string (item.toString ()));
    loaded (tuple (font_string (family), font_string (style)))= characteristics;
  }
  font_characteristics= loaded;
  return true;
}

static void
font_database_seed_catalog_characteristics () {
  font_characteristics= hashmap<tree,tree> (UNINIT);
  iterator<tree> it= iterate (font_catalog_characteristics);
  while (it->busy ()) {
    tree key= it->next ();
    if (font_table->contains (key))
      font_characteristics (key)= copy (font_catalog_characteristics[key]);
  }
}

static void
font_database_save_characteristics_cache () {
  QJsonObject root;
  root.insert ("format", "athena-font-characteristics");
  root.insert ("version", CHARACTERISTICS_CACHE_VERSION);
  root.insert ("catalogSignature", font_qstring (tt_font_catalog_signature ()));

  QJsonArray entries;
  iterator<tree> it= iterate (font_characteristics);
  while (it->busy ()) {
    tree key= it->next ();
    if (!is_func (key, TUPLE, 2) || !is_atomic (key[0]) || !is_atomic (key[1]))
      continue;
    QJsonObject entry;
    entry.insert ("family", font_qstring (key[0]->label));
    entry.insert ("style", font_qstring (key[1]->label));
    QJsonArray values;
    tree characteristics= font_characteristics[key];
    if (is_func (characteristics, TUPLE))
      for (int i=0; i<N(characteristics); i++)
        if (is_atomic (characteristics[i]))
          values.append (font_qstring (characteristics[i]->label));
    entry.insert ("values", values);
    entries.append (entry);
  }
  root.insert ("entries", entries);

  QByteArray bytes= QJsonDocument (root).toJson (QJsonDocument::Indented);
  url u= CHARACTERISTICS_CACHE;
  mkdir (head (u));
  save_string (u, string (bytes.constData (), bytes.size ()), false);
}

void
font_database_load_substitutions (url u) {
  if (!exists (u)) return;
  string s;
  if (!load_string (u, s, false)) {
    tree t= block_to_scheme_tree (s);
    for (int i=0; i<N(t); i++)
      if (is_func (t[i], TUPLE, 2) &&
          is_func (t[i][0], TUPLE) &&
          is_func (t[i][1], TUPLE) &&
          N(t[i][0]) > 0 &&
          N(t[i][1]) > 0 &&
          is_atomic (t[i][0][0]) &&
          is_atomic (t[i][1][0])) {
        string key= t[i][0][0]->label;
        string im = t[i][1][0]->label;
        if (N(font_database_styles (im)) != 0) {
          if (!font_substitutions->contains (key))
            font_substitutions (key)= tree (TUPLE);
          font_substitutions (key) << t[i];
        }
      }
  }
}

void
font_database_load () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  if (fonts_loaded || fonts_loading) return;
  fonts_loading= true;
  system_wait ("Loading platform font catalog", "please wait...");
  font_database_load_catalog (false);

  // These files are derived metadata only.  The platform catalog above is the
  // sole authority for which font families, styles, and files are installed.
  font_database_load_features (GLOBAL_FEATURES);
  font_database_filter_features ();
  // Make catalog queries re-entrant while deriving metadata below.
  fonts_loaded= true;
  font_database_guess_features ();
  if (!font_database_load_characteristics_cache ()) {
    // Fontconfig is the normal source of characteristics.  Persist its compact
    // per-face metadata so a fresh profile gets a complete cache without the
    // expensive FreeType geometry analysis formerly shipped as a static table.
    font_database_seed_catalog_characteristics ();
    font_database_save_characteristics_cache ();
  }
  font_database_load_substitutions (GLOBAL_SUBSTITUTIONS);
  fonts_loading= false;
  system_wait ("");
}

void
font_database_save () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_save_characteristics_cache ();
  font_closest_cache_invalidate ();
}

/******************************************************************************
* Building the database
******************************************************************************/

bool
on_blacklist (string name) {
  return
    name == "AppleMyungjo.ttf" ||
    name == "NISC18030.ttf" ||
    name == "Gungseouche.ttf" ||
    name == "blex.ttf" ||
    name == "blsy.ttf" ||
    name == "rblmi.ttf" ||
    starts (name, "FonetikaDania");
}

void
font_database_build (url u) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_invalidate_selectors ();
  if (is_none (u));
  else if (is_or (u)) {
    font_database_build (u[1]);
    font_database_build (u[2]);
  }
  else if (is_directory (u)) {
    bool err;
    array<string> a= read_directory (u, err);
    for (int i=0; i<N(a); i++)
      if (!starts (a[i], "."))
        if (font_database_is_tt_file (a[i]))
          font_database_build (u * url (a[i]));
  }
  else if (is_regular (u)) {
    if (on_blacklist (as_string (tail (u)))) return;
    cout << "Process " << u << "\n";
    scheme_tree t= tt_font_name (u);
    for (int i=0; i<N(t); i++)
      if (is_func (t[i], TUPLE, 2) &&
          is_atomic (t[i][0]) &&
          is_atomic (t[i][1]))
        {
          int  sz = file_size (u);
          tree key= t[i];
          tree im = tuple (as_string (tail (u)), as_string (i), as_string (sz));
          tree all= tree (TUPLE);
          if (font_table->contains (key))
            all= font_table [key];
          tuple_insert (all, im);
          font_table (key)= all;
        }
  }
}

static void
font_database_guess_features () {
  array<string> families= font_database_families ();
  for (int i=0; i<N(families); i++)
    if (!font_features->contains (families[i])) {
      string master= family_to_master (families[i]);
      array<string> features= family_features (families[i]);
      tree entry (TUPLE);
      entry << tree (master);
      for (int j=0; j<N(features); j++)
        entry << tree (encode_feature (features[j]));
      font_features (families[i])= entry;
      tree variants (TUPLE);
      if (font_variants->contains (master)) variants= font_variants[master];
      tuple_insert (variants, families[i]);
      font_variants (master)= variants;
    }
}

void
font_database_build_local () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load ();
  font_database_load_catalog (true);
  font_database_filter_features ();
  font_database_guess_features ();
  font_database_seed_catalog_characteristics ();
  font_database_save ();
}

void
font_database_extend_local (url u) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  tt_extend_font_path (u);
  font_database_load ();
  font_database_load_catalog (true);
  font_database_filter_features ();
  font_database_guess_features ();
  font_database_seed_catalog_characteristics ();
  font_database_save ();
}

void
font_database_build_global (url u) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_extend_local (u);
}

void
font_database_build_global () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_build_global (tt_font_path ());
}

void
font_database_filter () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load_catalog (true);
  font_database_filter_features ();
  font_database_guess_features ();
  font_database_seed_catalog_characteristics ();
  font_database_save ();
}

void
font_database_filter_features () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  hashmap<string,bool> families;
  iterator<tree> it= iterate (font_table);
  while (it->busy ()) {
    tree key= it->next ();
    if (is_func (key, TUPLE, 2) && is_atomic (key[0]))
      families (key[0]->label)= true;
  }
  hashmap<tree,tree> new_font_features (UNINIT);
  it= iterate (font_features);
  while (it->busy ()) {
    tree key= it->next ();
    if (is_atomic (key) && families->contains (key->label))
      new_font_features (key)= font_features [key];
  }
  font_features= new_font_features;
  font_variants= hashmap<tree,tree> (UNINIT);
  it= iterate (font_features);
  while (it->busy ()) {
    tree family= it->next ();
    tree features= font_features[family];
    if (!is_atomic (family) || !is_func (features, TUPLE) || N(features) == 0)
      continue;
    tree variants (TUPLE);
    if (font_variants->contains (features[0]))
      variants= font_variants[features[0]];
    tuple_insert (variants, family);
    font_variants (features[0])= variants;
  }
}

/******************************************************************************
* Additional font characteristics (automatically generated)
******************************************************************************/

static bool
font_database_build_characteristics_for (tree key, bool force) {
  if (!is_func (key, TUPLE, 2) || !font_table->contains (key)) return false;
  if (!force && font_characteristics->contains (key)) return false;
  tree im= font_table[key];
  for (int i=0; i<N(im); i++)
    if (is_func (im[i], TUPLE, 3)) {
      string name= as_string (im[i][0]);
      string nr  = as_string (im[i][1]);
      if (font_database_has_extension (name, ".ttc"))
        name= name (0, N(name)-4) * "." * nr * ".ttf";
      else if (font_database_has_extension (name, ".ttf") ||
               font_database_has_extension (name, ".otf"))
        name= name (0, N(name)-4);
      else continue;
      if (!tt_font_exists (name) && ends (name, "10"))
        name= name (0, N(name)-2);
      if (!tt_font_exists (name)) continue;
      array<string> a= tt_analyze (name);
      tree t (TUPLE, N(a));
      for (int j=0; j<N(a); j++) t[j]= a[j];
      font_characteristics (key)= t;
      return true;
    }
  return false;
}

void
font_database_build_characteristics (bool force) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  iterator<tree> it= iterate (font_table);
  while (it->busy ())
    (void) font_database_build_characteristics_for (it->next (), force);
  font_database_save_characteristics_cache ();
  font_database_characteristics_cache= hashmap<tree,tree> (UNINIT);
}

/******************************************************************************
* Querying the database
******************************************************************************/

static void
font_database_build_selectors () {
  hashmap<string,tree> styles (UNINIT);
  iterator<tree> it= iterate (font_table);
  while (it->busy ()) {
    tree key= it->next ();
    if (!is_func (key, TUPLE, 2) || !is_atomic (key[0]) ||
        !is_atomic (key[1]))
      continue;
    string family= key[0]->label;
    tree family_styles (TUPLE);
    if (styles->contains (family)) family_styles= styles[family];
    family_styles << key[1];
    styles (family)= family_styles;
  }

  font_database_families_cache= array<string> ();
  font_database_styles_cache= hashmap<string,tree> (UNINIT);
  iterator<string> families= iterate (styles);
  while (families->busy ()) {
    string family= families->next ();
    array<string> family_styles= tuple_as_array (styles[family]);
    merge_sort_leq<string,locase_less_eq_operator> (family_styles);
    font_database_families_cache << family;
    font_database_styles_cache (family)= array_as_tuple (family_styles);
  }
  merge_sort_leq<string,locase_less_eq_operator>
    (font_database_families_cache);
  font_database_families_cached= true;
}

array<string>
font_database_families () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load ();
  if (!font_database_families_cached) font_database_build_selectors ();
  return copy (font_database_families_cache);
}

array<string>
font_database_delta_families () {
  return font_database_families ();
}

array<string>
font_database_styles (string family) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  family= upgrade_family_name (family);
  font_database_load ();
  if (!font_database_families_cached) font_database_build_selectors ();
  if (font_database_styles_cache->contains (family)) {
    tree cached= font_database_styles_cache[family];
    if (is_func (cached, TUPLE))
      return tuple_as_array (cached);
  }
  return array<string> ();
}

array<string>
font_database_global_styles (string family) {
  return font_database_styles (family);
}

array<string>
font_database_search (string family, string style) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  family= upgrade_family_name (family);
  font_database_load ();
  array<string> r;
  tree key= tuple (family, style);
  if (font_table->contains (key)) {
    tree im= font_table [key];
    for (int i=0; i<N(im); i++)
      if (is_func (im[i], TUPLE, 3)) {
        string name= im[i][0]->label;
        string nr  = im[i][1]->label;
        if (!ends (name, ".ttc")) r << name;
        else r << (name (0, N(name)-4) * "." * nr * ".ttf");
      }
  }
  return r;
}

array<string>
font_database_search (string fam, string var, string series, string shape) {
  //cout << "Database search: " << fam << ", " << var
  //     << ", " << series << ", " << shape << "\n";
  array<string> lfn= logical_font (fam, var, series, shape);
  array<string> pfn= search_font (lfn);
  //cout << "Physical font: " << pfn << "\n";
  return font_database_search (pfn[0], pfn[1]);
}

array<string>
font_database_characteristics (string family, string style) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  family= upgrade_family_name (family);
  font_database_load ();
  tree key= tuple (family, style);
  if (font_database_characteristics_cache->contains (key)) {
    tree cached= font_database_characteristics_cache[key];
    if (is_func (cached, TUPLE)) return tuple_as_array (cached);
  }
  array<string> r;
  if (font_characteristics->contains (key)) {
    tree im= font_characteristics [key];
    for (int i=0; i<N(im); i++)
      if (is_atomic (im[i]))
	r << im[i]->label;
  }
  else if (font_catalog_characteristics->contains (key)) {
    tree im= font_catalog_characteristics[key];
    for (int i=0; i<N(im); i++)
      if (is_atomic (im[i])) r << im[i]->label;
  }
  font_database_characteristics_cache (key)= array_as_tuple (r);
  return r;
}

array<string>
font_database_feature_entry (string family) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load ();
  tree key (family);
  if (!font_features->contains (key)) return array<string> ();
  tree entry= copy (font_features[key]);
  if (!is_func (entry, TUPLE)) return array<string> ();
  return tuple_as_array (entry);
}

array<string>
font_database_master_variants (string master) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load ();
  tree key (master);
  if (!font_variants->contains (key)) return array<string> ();
  tree entry= copy (font_variants[key]);
  if (!is_func (entry, TUPLE)) return array<string> ();
  return tuple_as_array (entry);
}

tree
font_database_substitutions (string family) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  family= upgrade_family_name (family);
  font_database_load ();
  if (font_substitutions->contains (family))
    return copy (font_substitutions [family]);
  else return tree (TUPLE);
}
