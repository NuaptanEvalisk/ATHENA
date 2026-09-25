
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
#include "font_database.hpp"
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
#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

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
  files << url (GLOBAL_SUBSTITUTIONS);
  string r= "4;catalog=" * tt_font_catalog_signature ();
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
hashmap<tree,tree> font_characteristics (UNINIT);
static hashmap<tree,tree> font_catalog_characteristics (UNINIT);
hashmap<string,tree> font_substitutions (UNINIT);
static bool font_database_families_cached= false;
static array<string> font_database_families_cache;
static hashmap<string,tree> font_database_styles_cache (UNINIT);
static hashmap<tree,tree> font_database_characteristics_cache (UNINIT);
static std::shared_ptr<const athena::text::font_database_view> font_native_view;
static std::uint64_t font_native_generation= 0;

static QString
font_qstring (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

namespace athena::text {

std::string
font_database_family_key (std::string_view family) {
  const QString value= QString::fromUtf8 (family.data (), (int) family.size ());
  const QByteArray folded= value.toCaseFolded ().toUtf8 ();
  return std::string (folded.constData (), (std::size_t) folded.size ());
}

std::string
font_database_face_key (const font_file_source& source) {
  return source.file_utf8 + std::string (1, '\0') +
         std::to_string (source.face_index);
}

std::string
font_database_physical_key (const font_file_source& source) {
  std::string result= font_database_face_key (source);
  for (std::int32_t coordinate: source.design_coords) {
    result.push_back ('\0');
    result+= std::to_string (coordinate);
  }
  return result;
}

bool
font_database_supports (const font_database_face& face, char32_t scalar) {
  if (scalar > 0x10ffff) return false;
  const std::uint32_t value= static_cast<std::uint32_t> (scalar);
  const std::uint32_t page= value & ~std::uint32_t (0xff);
  const auto found= std::lower_bound (
    face.coverage.begin (), face.coverage.end (), page,
    [] (const font_database_coverage_page& item, std::uint32_t first) {
      return item.first < first;
    });
  if (found == face.coverage.end () || found->first != page) return false;
  const std::uint32_t bit= value - page;
  return (found->bits[bit >> 5] & (std::uint32_t (1) << (bit & 31))) != 0;
}

std::optional<font_file_source>
font_database_match_family (
    std::string_view family, int weight, int slant, int width, int spacing) {
  auto view= font_database_native_view ();
  const auto found= view->families.find (font_database_family_key (family));
  if (found == view->families.end ()) return std::nullopt;
  std::optional<std::size_t> best;
  long long best_score= std::numeric_limits<long long>::max ();
  for (std::size_t candidate: found->second) {
    const auto& face= view->faces[candidate];
    const long long score=
      1000LL * std::llabs (static_cast<long long> (face.weight) - weight) +
      100LL * std::llabs (static_cast<long long> (face.width) - width) +
      (face.slant == slant ? 0 : 10000000LL) +
      (face.spacing == spacing ? 0 : 1000000LL) +
      (face.scalable ? 0 : 100000LL) +
      static_cast<long long> (candidate);
    if (!best || score < best_score) {
      best= candidate;
      best_score= score;
    }
  }
  if (!best) return std::nullopt;
  return view->faces[*best].file;
}

std::optional<font_file_source>
font_database_match_style (
    std::string_view family, std::string_view style) {
  auto view= font_database_native_view ();
  const auto found= view->families.find (font_database_family_key (family));
  if (found == view->families.end ()) return std::nullopt;
  const auto wanted= font_database_family_key (style);
  std::optional<std::size_t> best;
  for (std::size_t candidate: found->second) {
    const auto& face= view->faces[candidate];
    if (font_database_family_key (face.style) != wanted) continue;
    if (!best || (!view->faces[*best].scalable && face.scalable) ||
        (view->faces[*best].scalable == face.scalable && candidate < *best))
      best= candidate;
  }
  if (!best) return std::nullopt;
  return view->faces[*best].file;
}

} // namespace athena::text

static std::shared_ptr<const athena::text::font_database_view>
font_database_make_native_view (
  const std::vector<tt_font_catalog_record>& records) {
  using namespace athena::text;
  auto view= std::make_shared<font_database_view> ();
  view->generation= ++font_native_generation;
  std::unordered_map<std::string,std::size_t> unique;
  for (const auto& record: records) {
    const std::string key= font_database_face_key (record.file);
    auto found= unique.find (key);
    std::size_t index;
    if (found == unique.end ()) {
      index= view->faces.size ();
      unique.emplace (key, index);
      font_database_face face;
      face.file= record.file;
      face.style= record.style;
      face.weight= record.weight;
      face.width= record.width;
      face.slant= record.slant;
      face.spacing= record.spacing;
      face.scalable= record.scalable;
      face.color= record.color;
      face.coverage.reserve (record.coverage.size ());
      for (const auto& page: record.coverage)
        face.coverage.push_back ({page.first, page.bits});
      view->faces.push_back (std::move (face));
      view->physical_faces.emplace (key, index);
    }
    else index= found->second;

    auto& face= view->faces[index];
    for (const auto& family: record.families) {
      if (std::find (face.families.begin (), face.families.end (), family) ==
          face.families.end ())
        face.families.push_back (family);
    }
  }

  for (std::size_t index=0; index<view->faces.size (); ++index) {
    const auto& face= view->faces[index];
    for (const auto& family: face.families) {
      auto& members= view->families[font_database_family_key (family)];
      if (std::find (members.begin (), members.end (), index) == members.end ())
        members.push_back (index);
    }
    for (const auto& page: face.coverage)
      view->unicode_pages[page.first].push_back (index);
  }
  const auto style_class= [] (const font_database_face& face) {
    const unsigned weight=
      face.weight < 50 ? 0U :
      face.weight < 90 ? 1U :
      face.weight < 140 ? 2U :
      face.weight < 180 ? 3U : 4U;
    const unsigned width= face.width < 90 ? 0U : face.width > 110 ? 2U : 1U;
    const unsigned slant= static_cast<unsigned> (
      std::clamp (face.slant, 0, 2));
    const unsigned spacing= face.spacing == 0 ? 0U : 1U;
    const unsigned color= face.color ? 1U : 0U;
    return ((((weight * 3U + width) * 3U + slant) * 2U + spacing) * 2U +
            color);
  };
  constexpr std::size_t style_classes= 5 * 3 * 3 * 2 * 2;
  constexpr std::size_t shortlist_limit= 256;
  for (const auto& [page, candidates]: view->unicode_pages) {
    if (candidates.size () <= shortlist_limit) continue;
    font_database_page_shortlist shortlist;
    for (std::uint32_t offset=0; offset<256; ++offset) {
      shortlist.offsets[offset]=
        static_cast<std::uint32_t> (shortlist.faces.size ());
      const char32_t scalar= static_cast<char32_t> (page + offset);
      std::array<std::vector<std::uint32_t>, style_classes> buckets;
      for (std::size_t candidate: candidates)
        if (font_database_supports (view->faces[candidate], scalar))
          buckets[style_class (view->faces[candidate])].push_back (
            static_cast<std::uint32_t> (candidate));
      for (auto& bucket: buckets)
        std::stable_sort (
          bucket.begin (), bucket.end (),
          [&] (std::uint32_t left, std::uint32_t right) {
            const bool l= view->faces[left].scalable;
            const bool r= view->faces[right].scalable;
            return l != r ? l : left < right;
          });
      for (std::size_t round=0;
           shortlist.faces.size () -
               static_cast<std::size_t> (shortlist.offsets[offset]) <
             shortlist_limit;
           ++round) {
        bool added= false;
        for (const auto& bucket: buckets) {
          if (round >= bucket.size ()) continue;
          shortlist.faces.push_back (bucket[round]);
          added= true;
          if (shortlist.faces.size () -
                static_cast<std::size_t> (shortlist.offsets[offset]) >=
              shortlist_limit)
            break;
        }
        if (!added) break;
      }
    }
    shortlist.offsets[256]=
      static_cast<std::uint32_t> (shortlist.faces.size ());
    view->unicode_shortlists.emplace (page, std::move (shortlist));
  }
  for (const char* generic: {"serif", "sans-serif", "monospace"}) {
    string matched= tt_font_match_family (string (generic));
    if (matched != "")
      view->generic_families.emplace (
        generic, std::string (matched.data (), N(matched)));
  }
  return view;
}

std::shared_ptr<const athena::text::font_database_view>
athena::text::font_database_native_view () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load ();
  if (!font_native_view)
    font_native_view= font_database_make_native_view ({});
  return font_native_view;
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

static tree
font_database_record_characteristics (const tt_font_catalog_record& record) {
  athena::text::font_database_face face;
  face.weight= record.weight;
  face.width= record.width;
  face.slant= record.slant;
  face.spacing= record.spacing;
  face.scalable= record.scalable;
  face.color= record.color;
  for (const auto& page: record.coverage)
    face.coverage.push_back ({page.first, page.bits});
  const auto has= [&] (char32_t scalar) {
    return athena::text::font_database_supports (face, scalar);
  };
  tree result (TUPLE);
  if (has (0x0041) && has (0x0061)) result << "Ascii";
  if (has (0x00e9)) result << "Latin";
  if (has (0x03b1)) result << "Greek";
  if (has (0x0430)) result << "Cyrillic";
  if (has (0x4e00)) result << "CJK";
  if (has (0xac00)) result << "Hangul";
  if (has (0x2200) || has (0x2211)) result << "MathSymbols";
  if (has (0x1d400)) result << "MathExtra";
  if (has (0x1d44e)) result << "MathLetters";
  result << (record.spacing == 0 ? "mono=no" : "mono=yes");
  result << (string ("weight=") * as_string (record.weight));
  result << (string ("width=") * as_string (record.width));
  result << (string ("slant=") * as_string (record.slant));
  result << (record.slant == 1 ? "italic=yes" : "italic=no");
  return result;
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
  const auto records= tt_font_catalog_records (refresh);
  for (const auto& record: records) {
    string path (record.file.file_utf8.data (), record.file.file_utf8.size ());
    string base= as_string (tail (url_system (path)));
    string style (record.style.data (), record.style.size ());
    for (const auto& raw_family: record.families) {
      string family (raw_family.data (), raw_family.size ());
      tree key= tuple (family, style);
      tree im= tuple (
        base, as_string (static_cast<int> (record.file.face_index)), "0", path);
      tree all (TUPLE);
      if (font_table->contains (key)) all= font_table[key];
      tuple_insert (all, im);
      font_table (key)= all;
      font_catalog_characteristics (key)=
        font_database_record_characteristics (record);
    }
  }
  tree catalog= tt_font_catalog (false);
  for (int i=0; i<N(catalog); i++)
    if (is_func (catalog[i], TUPLE) && N(catalog[i]) >= 4 &&
        is_atomic (catalog[i][0]) && is_atomic (catalog[i][1]) &&
        is_atomic (catalog[i][2]) && is_atomic (catalog[i][3])) {
      tree key= tuple (catalog[i][0], catalog[i][1]);
      if (records.empty ()) {
        tree im=
          N(catalog[i]) >= 6 && is_atomic (catalog[i][5]) ?
            tuple (catalog[i][2], catalog[i][3], "0", catalog[i][5]) :
            tuple (catalog[i][2], catalog[i][3], "0");
        tree all (TUPLE);
        if (font_table->contains (key)) all= font_table[key];
        tuple_insert (all, im);
        font_table (key)= all;
      }
      if (N(catalog[i]) >= 5 && is_func (catalog[i][4], TUPLE))
        font_catalog_characteristics (key)= catalog[i][4];
    }

  // Platforms without a native catalog still discover their actual font files.
  if (N(font_table) == 0) font_database_build (tt_font_path ());
  font_native_view= font_database_make_native_view (records);
  if (refresh) invalidate_font_configuration ();
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

  // Make catalog queries re-entrant while deriving metadata below.
  fonts_loaded= true;
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
          tree im = tuple (as_string (tail (u)), as_string (i), as_string (sz),
                           concretize (u));
          tree all= tree (TUPLE);
          if (font_table->contains (key))
            all= font_table [key];
          tuple_insert (all, im);
          font_table (key)= all;
        }
  }
}

void
font_database_build_local () {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  font_database_load ();
  font_database_load_catalog (true);
  font_database_seed_catalog_characteristics ();
  font_database_save ();
}

void
font_database_extend_local (url u) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  tt_extend_font_path (u);
  font_database_load ();
  font_database_load_catalog (true);
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
  font_database_seed_catalog_characteristics ();
  font_database_save ();
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
    if (is_func (im[i], TUPLE) && N(im[i]) >= 3) {
      if (N(im[i]) >= 4 && is_atomic (im[i][3])) {
        string path= as_string (im[i][3]);
        const int face= as_int (as_string (im[i][1]));
        if (path != "" && face >= 0) {
          athena::text::font_file_source source {
            std::string (path.data (), N(path)), face};
          array<string> a= tt_analyze (source, key[0]->label);
          tree t (TUPLE, N(a));
          for (int j=0; j<N(a); j++) t[j]= a[j];
          font_characteristics (key)= t;
          return true;
        }
      }
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
      if (is_func (im[i], TUPLE) && N(im[i]) >= 3) {
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

tree
font_database_substitutions (string family) {
  std::lock_guard<std::recursive_mutex> guard (font_database_mutex);
  family= upgrade_family_name (family);
  font_database_load ();
  if (font_substitutions->contains (family))
    return copy (font_substitutions [family]);
  else return tree (TUPLE);
}
