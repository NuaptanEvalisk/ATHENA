
/******************************************************************************
* MODULE     : tt_file.cpp
* DESCRIPTION: Finding a True Type font
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tt_file.hpp"
#include "font_domain.hpp"
#include <mutex>
#include <unordered_map>
#include <map>
#include <cctype>
#include "tt_tools.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "hashmap.hpp"
#include "tm_timer.hpp"
#include "data_cache.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"

extern bool use_locate;

#ifdef USE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#else
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#endif

namespace {
struct local_font_state {
  hashmap<string,string> tt_fonts{"no"};
  bool tt_font_file_index_ready= false;
  bool tt_font_file_index_building= false;
};
local_font_state& font_state () {
  return font_domain_local<local_font_state> ();
}
}

#define TT_FONT_PATH_CACHE "font_path_cache_v2.scm"
#define TT_FONT_FILE_INDEX_CACHE "font_file_index_v2.scm"
#define TT_FONT_CACHE_VERSION "2"

static bool tt_font_file_index_warmup_disabled= false;

static string
tt_font_cache_component (string s) {
  return as_string (N(s)) * ":" * s;
}

static string
tt_font_cache_signature (string xtt, string ximp) {
  return string (TT_FONT_CACHE_VERSION) * ":" *
         tt_font_cache_component (xtt) *
         tt_font_cache_component (ximp);
}

static string
tt_font_cache_signature () {
  return tt_font_cache_signature (get_env ("ATHENA_FONT_PATH"),
                                  get_preference ("imported fonts", ""));
}

static string
tt_font_cache_key (string kind, string signature, string name= "") {
  return kind * ":" * tt_font_cache_component (signature) *
         tt_font_cache_component (name);
}

static bool
tt_font_file_extension (string name) {
  string l= locase_all (name);
  return ends (l, ".ttf") || ends (l, ".ttc") || ends (l, ".otf") ||
         ends (l, ".dfont");
}

url
add_to_path (url u, url d) {
  if (is_or (d)) return add_to_path (add_to_path (u, d[1]), d[2]);
  if (is_none (u) || u == d) return d;
  if (is_or (u) && u[1] == d) return u;
  if (is_or (u)) return u[1] | add_to_path (u[2], d);
  return u | d;
}

void
tt_extend_font_path (url u) {
  if (!is_directory (u)) u= head (u);
  string old= get_preference ("imported fonts", "");
  if (old == "") set_preference ("imported fonts", as_unix_string (u));
  else {
    url dirs= add_to_path (url_unix (old), u);
    set_preference ("imported fonts", as_unix_string (dirs));
  }
  font_state ().tt_fonts= hashmap<string,string> ("no");
  font_state ().tt_font_file_index_ready= false;
  invalidate_font_configuration ();
}

static void
tt_collect_font_dirs (url u, array<url>& dirs) {
  if (is_none (u)) return;
  if (is_or (u)) {
    tt_collect_font_dirs (u[1], dirs);
    tt_collect_font_dirs (u[2], dirs);
  }
  else if (is_directory (u)) dirs << u;
}

static string
tt_font_path_encode (url u) {
  array<url> dirs;
  tt_collect_font_dirs (u, dirs);
  string r;
  for (int i=0; i<N(dirs); i++)
    r << concretize (dirs[i]) << "\n";
  return r;
}

static url
tt_font_path_decode (string s) {
  url r= url_none ();
  int i= 0;
  string line;
  while (read_line (s, i, line)) {
    line= trim_spaces (line);
    if (line != "") r= add_to_path (r, url_system (line));
    if (i >= N(s)) break;
  }
  return r;
}

static url
tt_private_font_roots (string xtt, string ximp) {
  url roots= url_none ();
  if (xtt != "") roots= roots | url_unix (xtt);
  if (ximp != "") roots= roots | url_unix (ximp);
  roots=
    roots |
    url ("$ATHENA_HOME_PATH/fonts/truetype") |
    url ("$ATHENA_PATH/fonts/truetype");
#if defined OS_MINGW
  roots= roots | url ("$windir/Fonts");
#elif defined OS_MACOS
  roots=
    roots |
    url ("$HOME/Library/Fonts") |
    url ("/Library/Fonts") |
    url ("/Library/Application Support/Apple/Fonts/iLife") |
    url ("/Library/Application Support/Apple/Fonts/iWork") |
    url ("/System/Library/Fonts") |
    url ("/System/Library/PrivateFrameworks/FontServices.framework/Versions/A/Resources/Fonts/ApplicationSupport");
#endif
  return roots;
}

url tt_private_font_path () {
  return tt_private_font_roots (get_env ("ATHENA_FONT_PATH"),
                                get_preference ("imported fonts", ""));
}

#ifdef USE_FONTCONFIG
static hashmap<string,string> tt_platform_fonts ("");
static url tt_platform_dirs= url_none ();
static tree tt_platform_catalog (TUPLE);
static std::vector<tt_font_catalog_record> tt_platform_records;
static std::unordered_map<std::string,std::string> tt_platform_generic_families;
static string tt_platform_request_signature;
static string tt_platform_catalog_digest;
static bool tt_platform_initialized= false;
static std::mutex platform_catalog_mutex;

static void tt_platform_font_catalog (bool refresh);

static string
tt_font_basename (string path) {
  int pos= N(path);
  while (pos > 0 && path[pos-1] != '/' && path[pos-1] != '\\') pos--;
  return path (pos, N(path));
}

static string
tt_font_strip_extension (string name) {
  string lower= locase_all (name);
  if (ends (lower, ".dfont")) return name (0, N(name) - 6);
  if (ends (lower, ".ttf") || ends (lower, ".ttc") ||
      ends (lower, ".otf"))
    return name (0, N(name) - 4);
  return name;
}

static void
tt_platform_add_name (string name, string path) {
  if (name != "" && !tt_platform_fonts->contains (name))
    tt_platform_fonts (name)= path;
}

static void
tt_platform_add_pattern_names (FcPattern* pattern, const char* field,
                               string path) {
  for (int i=0; ; i++) {
    FcChar8* value= nullptr;
    if (FcPatternGetString (pattern, field, i, &value) != FcResultMatch) break;
    tt_platform_add_name (string ((const char*) value), path);
  }
}

static tree
tt_platform_characteristics (FcPattern* pattern) {
  tree result (TUPLE);
  FcCharSet* charset= nullptr;
  if (FcPatternGetCharSet (pattern, FC_CHARSET, 0, &charset) == FcResultMatch) {
    if (FcCharSetHasChar (charset, 0x0041) &&
        FcCharSetHasChar (charset, 0x0061)) result << "Ascii";
    if (FcCharSetHasChar (charset, 0x00e9)) result << "Latin";
    if (FcCharSetHasChar (charset, 0x03b1)) result << "Greek";
    if (FcCharSetHasChar (charset, 0x0430)) result << "Cyrillic";
    if (FcCharSetHasChar (charset, 0x4e00)) result << "CJK";
    if (FcCharSetHasChar (charset, 0xac00)) result << "Hangul";
    if (FcCharSetHasChar (charset, 0x2200) ||
        FcCharSetHasChar (charset, 0x2211)) result << "MathSymbols";
    if (FcCharSetHasChar (charset, 0x1d400)) result << "MathExtra";
    if (FcCharSetHasChar (charset, 0x1d44e)) result << "MathLetters";
  }

  int spacing= FC_PROPORTIONAL;
  if (FcPatternGetInteger (pattern, FC_SPACING, 0, &spacing) == FcResultMatch)
    result << (spacing == FC_MONO || spacing == FC_DUAL?
               "mono=yes": "mono=no");
  int weight= FC_WEIGHT_REGULAR;
  if (FcPatternGetInteger (pattern, FC_WEIGHT, 0, &weight) == FcResultMatch)
    result << (string ("weight=") * as_string (weight));
  int width= FC_WIDTH_NORMAL;
  if (FcPatternGetInteger (pattern, FC_WIDTH, 0, &width) == FcResultMatch)
    result << (string ("width=") * as_string (width));
  int slant= FC_SLANT_ROMAN;
  if (FcPatternGetInteger (pattern, FC_SLANT, 0, &slant) == FcResultMatch) {
    result << (string ("slant=") * as_string (slant));
    result << (slant == FC_SLANT_ITALIC? "italic=yes": "italic=no");
  }
  return result;
}

static std::vector<tt_font_coverage_page>
tt_platform_coverage (FcPattern* pattern) {
  std::vector<tt_font_coverage_page> result;
  FcCharSet* charset= nullptr;
  if (FcPatternGetCharSet (pattern, FC_CHARSET, 0, &charset) != FcResultMatch ||
      charset == nullptr)
    return result;
  FcChar32 map[FC_CHARSET_MAP_SIZE];
  FcChar32 next= 0;
  for (FcChar32 page= FcCharSetFirstPage (charset, map, &next);
       page != FC_CHARSET_DONE;
       page= FcCharSetNextPage (charset, map, &next)) {
    tt_font_coverage_page out;
    out.first= page;
    for (int i=0; i<FC_CHARSET_MAP_SIZE; ++i) out.bits[i]= map[i];
    result.push_back (out);
  }
  return result;
}

static void
tt_platform_collect_set (FcFontSet* set) {
  if (set == nullptr) return;
  for (int i=0; i<set->nfont; i++) {
    FcChar8* file= nullptr;
    FcPattern* pattern= set->fonts[i];
    if (FcPatternGetString (pattern, FC_FILE, 0, &file) != FcResultMatch)
      continue;
    string path ((const char*) file);
    string base= tt_font_basename (path);
    if (!tt_font_file_extension (base)) continue;
    tt_platform_add_name (base, path);
    tt_platform_add_name (tt_font_strip_extension (base), path);
    tt_platform_add_pattern_names (pattern, FC_FAMILY, path);
    tt_platform_add_pattern_names (pattern, FC_FULLNAME, path);
    tt_platform_add_pattern_names (pattern, FC_POSTSCRIPT_NAME, path);

    FcChar8* style_value= nullptr;
    string style= "Regular";
    if (FcPatternGetString (pattern, FC_STYLE, 0, &style_value) ==
        FcResultMatch)
      style= string ((const char*) style_value);
    int face_index= 0;
    (void) FcPatternGetInteger (pattern, FC_INDEX, 0, &face_index);
    int weight= FC_WEIGHT_REGULAR;
    int width= FC_WIDTH_NORMAL;
    int slant= FC_SLANT_ROMAN;
    int spacing= FC_PROPORTIONAL;
    (void) FcPatternGetInteger (pattern, FC_WEIGHT, 0, &weight);
    (void) FcPatternGetInteger (pattern, FC_WIDTH, 0, &width);
    (void) FcPatternGetInteger (pattern, FC_SLANT, 0, &slant);
    (void) FcPatternGetInteger (pattern, FC_SPACING, 0, &spacing);
    FcBool scalable= FcTrue;
    FcBool color= FcFalse;
    (void) FcPatternGetBool (pattern, FC_SCALABLE, 0, &scalable);
#ifdef FC_COLOR
    (void) FcPatternGetBool (pattern, FC_COLOR, 0, &color);
#endif
    tree characteristics= tt_platform_characteristics (pattern);
    tt_font_catalog_record record;
    record.file.file_utf8.assign (path.data (), N(path));
    record.file.face_index= face_index;
    record.style.assign (style.data (), N(style));
    record.weight= FcWeightToOpenType (weight);
    if (record.weight <= 0) record.weight= 400;
    record.width= width;
    record.slant=
      slant == FC_SLANT_ITALIC ? 1 :
      slant == FC_SLANT_OBLIQUE ? 2 : 0;
    record.spacing=
      spacing == FC_MONO ? 2 :
      spacing == FC_DUAL ? 1 : 0;
    record.scalable= scalable;
    record.color= color;
    record.coverage= tt_platform_coverage (pattern);
    for (int family_index=0; ; family_index++) {
      FcChar8* family_value= nullptr;
      if (FcPatternGetString (pattern, FC_FAMILY, family_index,
                              &family_value) != FcResultMatch)
        break;
      string family ((const char*) family_value);
      if (family != "") {
        record.families.emplace_back (family.data (), N(family));
        tree entry (TUPLE);
        entry << family << style << base << as_string (face_index)
              << characteristics << path;
        tt_platform_catalog << entry;
      }
    }
    if (!record.families.empty ()) tt_platform_records.push_back (std::move (record));
  }
}

static void
tt_fontconfig_add_dir (FcConfig* config, url u) {
  if (is_none (u)) return;
  if (is_or (u)) {
    tt_fontconfig_add_dir (config, u[1]);
    tt_fontconfig_add_dir (config, u[2]);
    return;
  }
  if (!is_directory (u)) return;
  string path= concretize (u);
  FcConfigAppFontAddDir (config, (const FcChar8*) path.c_str ());
}

static std::string
tt_platform_resolve_family (FcConfig* config, const char* family) {
  FcPattern* pattern= FcPatternCreate ();
  std::string result;
  if (pattern != nullptr) {
    FcPatternAddString (pattern, FC_FAMILY,
                        (const FcChar8*) family);
    FcConfigSubstitute (config, pattern, FcMatchPattern);
    FcDefaultSubstitute (pattern);
    FcResult match_result= FcResultNoMatch;
    FcPattern* match= FcFontMatch (config, pattern, &match_result);
    if (match != nullptr) {
      FcChar8* value= nullptr;
      if (FcPatternGetString (match, FC_FAMILY, 0, &value) == FcResultMatch)
        result= (const char*) value;
      FcPatternDestroy (match);
    }
    FcPatternDestroy (pattern);
  }
  return result;
}

static string
tt_platform_match_family (string family) {
  std::lock_guard<std::mutex> guard (platform_catalog_mutex);
  tt_platform_font_catalog (false);
  std::string key (family.data (), N(family));
  auto found= tt_platform_generic_families.find (key);
  if (found == tt_platform_generic_families.end ()) return "";
  return string (found->second.data (), found->second.size ());
}

static void
tt_platform_font_catalog (bool refresh) {
  string signature= tt_font_cache_signature ();
  if (!refresh && tt_platform_initialized &&
      signature == tt_platform_request_signature) return;

  bench_start ("platform font catalog");
  tt_platform_fonts= hashmap<string,string> ("");
  tt_platform_dirs= url_none ();
  tt_platform_catalog= tree (TUPLE);
  tt_platform_records.clear ();
  tt_platform_generic_families.clear ();

  // This published catalog must not mutate Qt's concurrently used FcConfig.
  FcConfig* config= FcInitLoadConfigAndFonts ();
  if (config != nullptr) {
    if (refresh) (void) FcConfigBuildFonts (config);
    string xtt= get_env ("ATHENA_FONT_PATH");
    string ximp= get_preference ("imported fonts", "");
    tt_fontconfig_add_dir (config, tt_private_font_roots (xtt, ximp));

    // Prefer bundled and imported fonts over an older system duplicate.
    tt_platform_collect_set (FcConfigGetFonts (config, FcSetApplication));
    tt_platform_collect_set (FcConfigGetFonts (config, FcSetSystem));
    for (const char* generic: {"serif", "sans-serif", "monospace"})
      tt_platform_generic_families.emplace (
        generic, tt_platform_resolve_family (config, generic));

    FcStrList* dirs= FcConfigGetFontDirs (config);
    if (dirs != nullptr) {
      const FcChar8* dir;
      while ((dir= FcStrListNext (dirs)) != nullptr)
        tt_platform_dirs= add_to_path (
          tt_platform_dirs, url_system (string ((const char*) dir)));
      FcStrListDone (dirs);
    }
    FcConfigDestroy (config);
  }

  string digest_source;
  for (const auto& record: tt_platform_records) {
    digest_source << string (record.file.file_utf8.data (),
                             record.file.file_utf8.size ()) << "\n"
                  << as_string (record.file.face_index) << "\n"
                  << string (record.style.data (), record.style.size ()) << "\n"
                  << as_string (record.weight) << ":"
                  << as_string (record.width) << ":"
                  << as_string (record.slant) << ":"
                  << as_string (record.spacing) << ":"
                  << as_string (record.scalable ? 1 : 0) << ":"
                  << as_string (record.color ? 1 : 0) << "\n";
    for (const auto& family: record.families)
      digest_source << string (family.data (), family.size ()) << "\n";
    for (const auto& page: record.coverage) {
      digest_source << as_string (page.first);
      for (std::uint32_t bits: page.bits)
        digest_source << ":" << as_string (bits);
      digest_source << "\n";
    }
  }
  tt_platform_catalog_digest=
    as_string (N(tt_platform_catalog)) * ":" * as_string (hash (digest_source));
  tt_platform_request_signature= signature;
  tt_platform_initialized= true;
  bench_cumul ("platform font catalog");
}

static url
tt_platform_font_find (string name) {
  std::lock_guard<std::mutex> guard (platform_catalog_mutex);
  tt_platform_font_catalog (false);
  if (!tt_platform_fonts->contains (name)) return url_none ();
  url u= url_system (tt_platform_fonts[name]);
  return exists (u)? u: url_none ();
}

static url
tt_platform_font_path () {
  std::lock_guard<std::mutex> guard (platform_catalog_mutex);
  tt_platform_font_catalog (false);
  return as_url (copy (tt_platform_dirs->t));
}

static tree
tt_platform_font_entries (bool refresh) {
  std::lock_guard<std::mutex> guard (platform_catalog_mutex);
  tt_platform_font_catalog (refresh);
  return copy (tt_platform_catalog);
}

static std::vector<tt_font_catalog_record>
tt_platform_font_records (bool refresh) {
  std::lock_guard<std::mutex> guard (platform_catalog_mutex);
  tt_platform_font_catalog (refresh);
  return tt_platform_records;
}

static string
tt_platform_font_signature () {
  std::lock_guard<std::mutex> guard (platform_catalog_mutex);
  tt_platform_font_catalog (false);
  return tt_platform_catalog_digest;
}
#else
static std::mutex directory_catalog_mutex;
static std::vector<tt_font_catalog_record> directory_catalog_records;
static string directory_catalog_signature;
static string directory_catalog_request_signature;
static bool directory_catalog_initialized= false;

static int
directory_width_percent (unsigned int width_class) {
  static const int widths[]= {0, 50, 62, 75, 87, 100, 112, 125, 150, 200};
  return width_class >= 1 && width_class <= 9 ? widths[width_class] : 100;
}

static std::vector<tt_font_coverage_page>
directory_face_coverage (FT_Face face) {
  std::map<std::uint32_t,std::array<std::uint32_t,8>> pages;
  if (FT_Select_Charmap (face, FT_ENCODING_UNICODE) != 0) return {};
  FT_UInt glyph= 0;
  for (FT_ULong scalar= FT_Get_First_Char (face, &glyph);
       glyph != 0;
       scalar= FT_Get_Next_Char (face, scalar, &glyph)) {
    if (scalar > 0x10ffff) continue;
    const std::uint32_t value= static_cast<std::uint32_t> (scalar);
    const std::uint32_t page= value & ~std::uint32_t (0xff);
    const std::uint32_t bit= value - page;
    pages[page][bit >> 5] |= std::uint32_t (1) << (bit & 31);
  }
  std::vector<tt_font_coverage_page> result;
  result.reserve (pages.size ());
  for (const auto& [first, bits]: pages)
    result.push_back ({first, bits});
  return result;
}

static void
directory_catalog_add_file (
    FT_Library library, url file, std::vector<tt_font_catalog_record>& out) {
  const string native= concretize (file);
  std::string path (native.data (), N(native));
  FT_Face probe= nullptr;
  if (FT_New_Face (library, path.c_str (), 0, &probe) != 0 || probe == nullptr)
    return;
  const FT_Long count= probe->num_faces;
  FT_Done_Face (probe);
  for (FT_Long index=0; index<count; ++index) {
    FT_Face face= nullptr;
    if (FT_New_Face (library, path.c_str (), index, &face) != 0 || face == nullptr)
      continue;
    tt_font_catalog_record record;
    record.file.file_utf8= path;
    record.file.face_index= index;
    if (face->family_name != nullptr)
      record.families.emplace_back (face->family_name);
    record.style= face->style_name == nullptr ? "Regular" : face->style_name;
    const TT_OS2* os2= static_cast<const TT_OS2*> (
      FT_Get_Sfnt_Table (face, ft_sfnt_os2));
    record.weight=
      os2 != nullptr && os2->usWeightClass != 0 ?
        os2->usWeightClass :
        (face->style_flags & FT_STYLE_FLAG_BOLD ? 700 : 400);
    record.width=
      os2 == nullptr ? 100 : directory_width_percent (os2->usWidthClass);
    std::string style_lower= record.style;
    std::transform (
      style_lower.begin (), style_lower.end (), style_lower.begin (),
      [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    record.slant=
      (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0 ?
        (style_lower.find ("oblique") != std::string::npos ? 2 : 1) : 0;
    record.spacing= FT_IS_FIXED_WIDTH (face) ? 2 : 0;
    record.scalable= FT_IS_SCALABLE (face);
#ifdef FT_HAS_COLOR
    record.color= FT_HAS_COLOR (face);
#endif
    record.coverage= directory_face_coverage (face);
    if (!record.families.empty ()) out.push_back (std::move (record));
    FT_Done_Face (face);
  }
}

static void
directory_catalog_collect (
    FT_Library library, url root, std::vector<tt_font_catalog_record>& out) {
  if (is_none (root)) return;
  if (is_or (root)) {
    directory_catalog_collect (library, root[1], out);
    directory_catalog_collect (library, root[2], out);
    return;
  }
  if (is_directory (root)) {
    bool error= false;
    array<string> entries= read_directory (root, error);
    if (error) return;
    for (int i=0; i<N(entries); ++i)
      if (!starts (entries[i], ".")) {
        url child= root * url (entries[i]);
        if (is_directory (child))
          directory_catalog_collect (library, child, out);
        else if (is_regular (child) && tt_font_file_extension (entries[i]))
          directory_catalog_add_file (library, child, out);
      }
  }
  else if (is_regular (root) &&
           tt_font_file_extension (as_string (tail (root))))
    directory_catalog_add_file (library, root, out);
}

static void
directory_catalog_build (bool refresh) {
  const string request= tt_font_cache_signature ();
  if (!refresh && directory_catalog_initialized &&
      request == directory_catalog_request_signature)
    return;
  directory_catalog_records.clear ();
  FT_Library library= nullptr;
  if (FT_Init_FreeType (&library) == 0 && library != nullptr) {
    directory_catalog_collect (
      library,
      tt_private_font_roots (
        get_env ("ATHENA_FONT_PATH"),
        get_preference ("imported fonts", "")),
      directory_catalog_records);
    FT_Done_FreeType (library);
  }
  string digest;
  for (const auto& record: directory_catalog_records) {
    digest << string (record.file.file_utf8.data (), record.file.file_utf8.size ())
           << ":" << as_string (record.file.face_index) << ":"
           << string (record.style.data (), record.style.size ()) << "\n";
    for (const auto& family: record.families)
      digest << string (family.data (), family.size ()) << "\n";
    for (const auto& page: record.coverage) {
      digest << as_string (page.first);
      for (std::uint32_t bits: page.bits)
        digest << ":" << as_string (bits);
      digest << "\n";
    }
  }
  directory_catalog_signature=
    as_string (directory_catalog_records.size ()) * ":" *
    as_string (hash (digest));
  directory_catalog_request_signature= request;
  directory_catalog_initialized= true;
}

static url
tt_platform_font_find (string name) {
  (void) name;
  return url_none ();
}

static url
tt_platform_font_path () {
  return url_none ();
}

static tree
tt_platform_font_entries (bool refresh) {
  (void) refresh;
  return tree (TUPLE);
}

static std::vector<tt_font_catalog_record>
tt_platform_font_records (bool refresh) {
  std::lock_guard<std::mutex> guard (directory_catalog_mutex);
  directory_catalog_build (refresh);
  return directory_catalog_records;
}

static string
tt_platform_font_signature () {
  std::lock_guard<std::mutex> guard (directory_catalog_mutex);
  directory_catalog_build (false);
  return directory_catalog_signature;
}

static string
tt_platform_match_family (string family) {
  (void) family;
  return "";
}
#endif

static url
tt_fontconfig_path () {
  return tt_platform_font_path ();
}

tree
tt_font_catalog (bool refresh) {
  return tt_platform_font_entries (refresh);
}

std::vector<tt_font_catalog_record>
tt_font_catalog_records (bool refresh) {
  return tt_platform_font_records (refresh);
}

string
tt_font_catalog_signature () {
  return tt_platform_font_signature ();
}

string
tt_font_match_family (string family) {
  return tt_platform_match_family (family);
}

url
tt_font_path () {
  struct path_cache {
    bool initialized= false;
    string xtt;
    string imported;
    url path= url_none ();
  };
  auto& state= font_domain_local<path_cache> ();
  auto& initialized= state.initialized;
  auto& cached_xtt= state.xtt;
  auto& cached_imported= state.imported;
  auto& cached_path= state.path;
  string xtt= get_env ("ATHENA_FONT_PATH");
  string ximp= get_preference ("imported fonts", "");
  if (initialized && xtt == cached_xtt && ximp == cached_imported)
    return cached_path;

  string key= tt_font_cache_key ("path", tt_font_cache_signature (xtt, ximp));
  if (is_cached (TT_FONT_PATH_CACHE, key)) {
    url cached= tt_font_path_decode (cache_get (TT_FONT_PATH_CACHE, key)->label);
    if (!is_none (cached)) {
      cached_path= cached;
      cached_xtt= xtt;
      cached_imported= ximp;
      initialized= true;
      return cached_path;
    }
  }

  bench_start ("tt font path");
#ifdef USE_FONTCONFIG
  cached_path= tt_platform_font_path () |
               search_sub_dirs (tt_private_font_roots (xtt, ximp));
#else
  url xu= url_none ();
  if (xtt != "") xu= search_sub_dirs (xtt);
  if (ximp != "") xu= xu | search_sub_dirs (url_unix (ximp));
  cached_path =
    xu |
    search_sub_dirs ("$ATHENA_HOME_PATH/fonts/truetype") |
    search_sub_dirs ("$ATHENA_PATH/fonts/truetype") |
#if defined OS_MINGW
    search_sub_dirs ("$windir/Fonts");
#elif defined OS_MACOS
    search_sub_dirs ("$HOME/Library/Fonts") |
    search_sub_dirs ("/Library/Fonts") |
    search_sub_dirs ("/Library/Application Support/Apple/Fonts/iLife") |
    search_sub_dirs ("/Library/Application Support/Apple/Fonts/iWork") |
    search_sub_dirs ("/System/Library/Fonts") |
    search_sub_dirs ("/System/Library/PrivateFrameworks/FontServices.framework/Versions/A/Resources/Fonts/ApplicationSupport");
#else
    tt_fontconfig_path () |
    search_sub_dirs ("$HOME/.fonts") |
    search_sub_dirs ("$HOME/.local/share/fonts") |
    search_sub_dirs ("/usr/share/fonts") |
    search_sub_dirs ("/usr/share/fonts/opentype") |
    search_sub_dirs ("/usr/share/fonts/truetype") |
    search_sub_dirs ("/usr/local/share/fonts") |
    search_sub_dirs ("/usr/local/share/fonts/opentype") |
    search_sub_dirs ("/usr/local/share/fonts/truetype");
#endif
#endif
  bench_cumul ("tt font path");
  cache_set (TT_FONT_PATH_CACHE, key, tt_font_path_encode (cached_path));
  cache_save (TT_FONT_PATH_CACHE);
  cached_xtt= xtt;
  cached_imported= ximp;
  initialized= true;
  return cached_path;
}

void
tt_font_cache_set_warmup_disabled (bool disabled) {
  tt_font_file_index_warmup_disabled= disabled;
}

void
tt_font_cache_warmup () {
  if (tt_font_file_index_warmup_disabled) return;
  if (font_state ().tt_font_file_index_ready || font_state ().tt_font_file_index_building) return;
  font_state ().tt_font_file_index_building= true;

  string sig= tt_font_cache_signature ();
  string ready_key= tt_font_cache_key ("ready", sig);
  if (is_cached (TT_FONT_FILE_INDEX_CACHE, ready_key) &&
      cache_get (TT_FONT_FILE_INDEX_CACHE, ready_key)->label == "yes") {
    font_state ().tt_font_file_index_ready= true;
    font_state ().tt_font_file_index_building= false;
    return;
  }

  bench_start ("tt font file index");
  string xtt= get_env ("ATHENA_FONT_PATH");
  string ximp= get_preference ("imported fonts", "");
  url font_path= search_sub_dirs (tt_private_font_roots (xtt, ximp));
  array<url> dirs;
  tt_collect_font_dirs (font_path, dirs);
  int indexed= 0;
  for (int i=0; i<N(dirs); i++) {
    bool error= false;
    array<string> files= read_directory (dirs[i], error);
    if (error) continue;
    for (int j=0; j<N(files); j++) {
      if (!tt_font_file_extension (files[j])) continue;
      string key= tt_font_cache_key ("file", sig, files[j]);
      if (!is_cached (TT_FONT_FILE_INDEX_CACHE, key)) {
        cache_set (TT_FONT_FILE_INDEX_CACHE, key,
                   concretize (dirs[i] * url (files[j])));
        indexed++;
      }
    }
  }
  cache_set (TT_FONT_FILE_INDEX_CACHE, ready_key, "yes");
  cache_save (TT_FONT_PATH_CACHE);
  cache_save (TT_FONT_FILE_INDEX_CACHE);
  bench_cumul ("tt font file index");
  cout << "ATHENA] font cache: warmed font file index"
       << ", directories=" << N(dirs)
       << ", new-files=" << indexed << LF;

  font_state ().tt_font_file_index_ready= true;
  font_state ().tt_font_file_index_building= false;
}

static url
tt_font_index_find (string name) {
  if (!font_state ().tt_font_file_index_ready && !font_state ().tt_font_file_index_building)
    tt_font_cache_warmup ();

  string key= tt_font_cache_key ("file", tt_font_cache_signature (), name);
  if (!is_cached (TT_FONT_FILE_INDEX_CACHE, key)) return url_none ();
  url u= url_system (cache_get (TT_FONT_FILE_INDEX_CACHE, key)->label);
  if (exists (u)) return u;
  cache_reset (TT_FONT_FILE_INDEX_CACHE, key);
  return url_none ();
}

static url
tt_locate (string name) {
  url platform= tt_platform_font_find (name);
  if (!is_none (platform)) return platform;

  if (use_locate && !starts (name, "mac-"))
    {
      string s= eval_system ("locate", "/" * name);
      //cout << "locate " << name << " -> " << s << "\n";
      int start, i, n= N(s);
      for (start=0, i=0; i<n; i++)
	if (s[i]=='\n') {
	  if (ends (s (start, i), name))
	    return url (s (start, i));
	  start= i+1;
      }
    }

  url indexed= tt_font_index_find (name);
  if (!is_none (indexed)) return indexed;
  if (!tt_font_file_index_warmup_disabled) return url_none ();

  string xtt= get_env ("ATHENA_FONT_PATH");
  string ximp= get_preference ("imported fonts", "");
  url tt_path= search_sub_dirs (tt_private_font_roots (xtt, ximp));
  //cout << "Resolve " << name << " in " << tt_path << "\n";
  return resolve (tt_path * name);
}

url
tt_font_find_sub (string name) {
  //cout << "tt_font_find " << name << "\n";
  url u= tt_unpack (name);
  if (!is_none (u)) return u;
  u= tt_locate (name * ".ttf");
  //if (!is_none (u)) cout << name << " -> " << u << "\n";
  //else cout << name << " -> ???\n";
  if (!is_none (u)) return u;
  u= tt_locate (name * ".ttc");
  if (!is_none (u)) return u;
  u= tt_locate (name * ".otf");
  if (!is_none (u)) return u;
  u= tt_locate (name * ".dfont");
  return u;
}

url
tt_font_find (string name) {
  string s= "ttf:" * name;
  if (is_cached ("font_cache.scm", s)) {
    string r= cache_get ("font_cache.scm", s) -> label;
    if (r == "") return url_none ();
    url u= url_system (r);
    if (exists (u)) return u;
    cache_reset ("font_cache.scm", s);
  }

  url r= tt_platform_font_find (name);
  if (is_none (r)) r= tt_font_find_sub (name);
  if (is_none (r)) cache_set ("font_cache.scm", s, "");
  else cache_set ("font_cache.scm", s, as_string (r));
  cache_save ("font_cache.scm");
  return r;
}

bool
tt_font_exists (string name) {
  //cout << "tt_font_exists? " << name << "\n";
  if (font_state ().tt_fonts->contains (name)) return font_state ().tt_fonts[name] == "yes";
  bool yes= !is_none (tt_font_find (name));
  font_state ().tt_fonts (name)= yes? string ("yes"): string ("no");
  return yes;
}

string
tt_find_name_sub (string name, int size) {
  if (size == 0) {
    if (tt_font_exists (name)) return name;
    else return "";
  }
  if (tt_font_exists (name * as_string (size)))
    return name * as_string (size);
  if (size > 333) size= (size+50)/100;
  if (tt_font_exists (name * as_string (size)))
    return name * as_string (size);

  if ((size >= 15) && tt_font_exists (name * "17")) return name * "17";
  if ((size >  12) && tt_font_exists (name * "12")) return name * "12";
  if ((size <  5 ) && tt_font_exists (name * "5" )) return name * "5" ;
  if ((size <  6 ) && tt_font_exists (name * "6" )) return name * "6" ;
  if ((size <  7 ) && tt_font_exists (name * "7" )) return name * "7" ;
  if ((size <  8 ) && tt_font_exists (name * "8" )) return name * "8" ;
  if ((size <  9 ) && tt_font_exists (name * "9" )) return name * "9" ;
  if ((size <  9 ) && tt_font_exists (name * "7" )) return name * "7" ;
  if (tt_font_exists (name * "10")) return name * "10";
  if ((size <  9 ) && tt_font_exists (name * "700" )) return name * "700" ;
  if ((size >= 15) && tt_font_exists (name * "1700")) return name * "1700";
  if (tt_font_exists (name * "1000")) return name * "1000";
  if (tt_font_exists (name)) return name;
  return "";
}

string
tt_find_name (string name, int size) {
  string s= "tt:" * name * as_string (size);
  if (is_cached ("font_cache.scm", s)) {
    string r= cache_get ("font_cache.scm", s) -> label;
    if (tt_font_exists (r)) return r;
    cache_reset ("font_cache.scm", s);
  }

  bench_start ("tt find name");
  string r= tt_find_name_sub (name, size);
  //cout << name << size << " -> " << r << "\n";
  bench_cumul ("tt find name");

  if (r != "") cache_set ("font_cache.scm", s, r);
  return r;
}
