
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
#include "tt_tools.hpp"
#include "file.hpp"
#include "boot.hpp"
#include "analyze.hpp"
#include "hashmap.hpp"
#include "Metafont/tex_files.hpp"
#include "tm_timer.hpp"
#include "data_cache.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"

#ifdef USE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

static hashmap<string,string> tt_fonts ("no");

#define TT_FONT_PATH_CACHE "font_path_cache_v2.scm"
#define TT_FONT_FILE_INDEX_CACHE "font_file_index_v2.scm"
#define TT_FONT_CACHE_VERSION "2"

static bool tt_font_file_index_ready= false;
static bool tt_font_file_index_building= false;
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
         ends (l, ".dfont") || ends (l, ".pfb");
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
  tt_fonts= hashmap<string,string> ("no");
  tt_font_file_index_ready= false;
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
    url ("/System/Library/PrivateFrameworks/FontServices.framework/Versions/A/Resources/Fonts/ApplicationSupport") |
    url ("/opt/local/share/texmf-texlive/fonts/opentype") |
    url ("/opt/local/share/texmf-texlive/fonts/truetype") |
    url ("/opt/local/share/texmf-texlive-dist/fonts/opentype") |
    url ("/opt/local/share/texmf-texlive-dist/fonts/truetype");
#else
  roots=
    roots |
    url ("/usr/local/texlive/2020/texmf-dist/opentype") |
    url ("/usr/local/texlive/2020/texmf-dist/truetype") |
    url ("/usr/local/texlive/2021/texmf-dist/opentype") |
    url ("/usr/local/texlive/2021/texmf-dist/truetype") |
    url ("/usr/local/texlive/2022/texmf-dist/opentype") |
    url ("/usr/local/texlive/2022/texmf-dist/truetype");
#endif
  return roots;
}

#ifdef USE_FONTCONFIG
static hashmap<string,string> tt_platform_fonts ("");
static url tt_platform_dirs= url_none ();
static tree tt_platform_catalog (TUPLE);
static string tt_platform_request_signature;
static string tt_platform_catalog_digest;
static bool tt_platform_initialized= false;

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
      ends (lower, ".otf") || ends (lower, ".pfb"))
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
  int slant= FC_SLANT_ROMAN;
  if (FcPatternGetInteger (pattern, FC_SLANT, 0, &slant) == FcResultMatch) {
    result << (string ("slant=") * as_string (slant));
    result << (slant == FC_SLANT_ITALIC? "italic=yes": "italic=no");
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
    tree characteristics= tt_platform_characteristics (pattern);
    for (int family_index=0; ; family_index++) {
      FcChar8* family_value= nullptr;
      if (FcPatternGetString (pattern, FC_FAMILY, family_index,
                              &family_value) != FcResultMatch)
        break;
      string family ((const char*) family_value);
      if (family != "")
        tt_platform_catalog <<
          tuple (family, style, base, as_string (face_index), characteristics);
    }
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

static void
tt_platform_font_catalog (bool refresh= false) {
  string signature= tt_font_cache_signature ();
  if (!refresh && tt_platform_initialized &&
      signature == tt_platform_request_signature) return;

  bench_start ("platform font catalog");
  tt_platform_fonts= hashmap<string,string> ("");
  tt_platform_dirs= url_none ();
  tt_platform_catalog= tree (TUPLE);

  // Qt has initialized Fontconfig by the time ATHENA reaches this path.
  // Reuse that catalog instead of parsing every system font a second time.
  FcConfig* config= FcConfigGetCurrent ();
  bool own_config= false;
  if (config == nullptr) {
    config= FcInitLoadConfigAndFonts ();
    own_config= config != nullptr;
  }
  if (config != nullptr) {
    if (refresh) (void) FcConfigBuildFonts (config);
    string xtt= get_env ("ATHENA_FONT_PATH");
    string ximp= get_preference ("imported fonts", "");
    tt_fontconfig_add_dir (config, tt_private_font_roots (xtt, ximp));

    // Prefer bundled and imported fonts over an older system duplicate.
    tt_platform_collect_set (FcConfigGetFonts (config, FcSetApplication));
    tt_platform_collect_set (FcConfigGetFonts (config, FcSetSystem));

    FcStrList* dirs= FcConfigGetFontDirs (config);
    if (dirs != nullptr) {
      const FcChar8* dir;
      while ((dir= FcStrListNext (dirs)) != nullptr)
        tt_platform_dirs= add_to_path (
          tt_platform_dirs, url_system (string ((const char*) dir)));
      FcStrListDone (dirs);
    }
    if (own_config) FcConfigDestroy (config);
  }

  string digest_source;
  for (int i=0; i<N(tt_platform_catalog); i++)
    if (is_func (tt_platform_catalog[i], TUPLE) &&
        N(tt_platform_catalog[i]) >= 4)
      for (int j=0; j<N(tt_platform_catalog[i]); j++)
        if (is_atomic (tt_platform_catalog[i][j]))
          digest_source << tt_platform_catalog[i][j]->label << "\n";
  tt_platform_catalog_digest=
    as_string (N(tt_platform_catalog)) * ":" * as_string (hash (digest_source));
  tt_platform_request_signature= signature;
  tt_platform_initialized= true;
  bench_cumul ("platform font catalog");
}

static url
tt_platform_font_find (string name) {
  tt_platform_font_catalog (false);
  if (!tt_platform_fonts->contains (name)) return url_none ();
  url u= url_system (tt_platform_fonts[name]);
  return exists (u)? u: url_none ();
}

static url
tt_platform_font_path () {
  tt_platform_font_catalog (false);
  return tt_platform_dirs;
}

static tree
tt_platform_font_entries (bool refresh) {
  tt_platform_font_catalog (refresh);
  return copy (tt_platform_catalog);
}

static string
tt_platform_font_signature () {
  tt_platform_font_catalog (false);
  return tt_platform_catalog_digest;
}
#else
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

static string
tt_platform_font_signature () {
  return "directory-catalog";
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

string
tt_font_catalog_signature () {
  return tt_platform_font_signature ();
}

url
tt_font_path () {
  static bool initialized= false;
  static string cached_xtt;
  static string cached_imported;
  static url cached_path= url_none ();
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
    search_sub_dirs ("/System/Library/PrivateFrameworks/FontServices.framework/Versions/A/Resources/Fonts/ApplicationSupport") |
    search_sub_dirs ("/opt/local/share/texmf-texlive/fonts/opentype") |
    search_sub_dirs ("/opt/local/share/texmf-texlive/fonts/truetype") |
    search_sub_dirs ("/opt/local/share/texmf-texlive-dist/fonts/opentype") |
    search_sub_dirs ("/opt/local/share/texmf-texlive-dist/fonts/truetype") |
    search_sub_dirs ("/usr/local/texlive/2020/texmf-dist/fonts/opentype") |
    search_sub_dirs ("/usr/local/texlive/2020/texmf-dist/fonts/truetype") |
    search_sub_dirs ("/usr/local/texlive/2021/texmf-dist/fonts/opentype") |
    search_sub_dirs ("/usr/local/texlive/2021/texmf-dist/fonts/truetype") |
    search_sub_dirs ("/usr/local/texlive/2022/texmf-dist/fonts/opentype") |
    search_sub_dirs ("/usr/local/texlive/2022/texmf-dist/fonts/truetype");
#else
    tt_fontconfig_path () |
    search_sub_dirs ("$HOME/.fonts") |
    search_sub_dirs ("$HOME/.local/share/fonts") |
    search_sub_dirs ("/usr/share/fonts") |
    search_sub_dirs ("/usr/share/fonts/opentype") |
    search_sub_dirs ("/usr/share/fonts/truetype") |
    search_sub_dirs ("/usr/local/share/fonts") |
    search_sub_dirs ("/usr/local/share/fonts/opentype") |
    search_sub_dirs ("/usr/local/share/fonts/truetype") |
    search_sub_dirs ("/usr/local/texlive/2020/texmf-dist/fonts/opentype") |
    search_sub_dirs ("/usr/local/texlive/2020/texmf-dist/fonts/truetype") |
    search_sub_dirs ("/usr/local/texlive/2021/texmf-dist/fonts/opentype") |
    search_sub_dirs ("/usr/local/texlive/2021/texmf-dist/fonts/truetype") |
    search_sub_dirs ("/usr/local/texlive/2022/texmf-dist/fonts/opentype") |
    search_sub_dirs ("/usr/local/texlive/2022/texmf-dist/fonts/truetype");
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
  if (tt_font_file_index_ready || tt_font_file_index_building) return;
  tt_font_file_index_building= true;

  string sig= tt_font_cache_signature ();
  string ready_key= tt_font_cache_key ("ready", sig);
  if (is_cached (TT_FONT_FILE_INDEX_CACHE, ready_key) &&
      cache_get (TT_FONT_FILE_INDEX_CACHE, ready_key)->label == "yes") {
    tt_font_file_index_ready= true;
    tt_font_file_index_building= false;
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

  tt_font_file_index_ready= true;
  tt_font_file_index_building= false;
}

static url
tt_font_index_find (string name) {
  if (!tt_font_file_index_ready && !tt_font_file_index_building)
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
  if (ends (name, ".pfb")) {
    /*
    if (starts (name, "rpag")) name= "uag" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rpbk")) name= "ubk" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rpcr")) name= "ucr" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rphv")) name= "uhv" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rpnc")) name= "unc" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rppl")) name= "upl" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rpsy")) name= "usy" * name (4, N (name));
    if (starts (name, "rptm")) name= "utm" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rpzc")) name= "uzc" * name (4, N (name) - 4) * "8a.pfb";
    if (starts (name, "rpzd")) name= "uzd" * name (4, N (name));
    */
    url u= resolve_tex (name);
    //cout << "tt_locate: " << name << " -> " << u << "\n";
    if (!is_none (u)) return u;
  }
  url platform= tt_platform_font_find (name);
  if (!is_none (platform)) return platform;

  if (use_locate &&
	   // NOTE: avoiding unnecessary locates can greatly improve timings
	   !starts (name, "ec") &&
	   !starts (name, "la") &&
	   !starts (name, "cm") &&
	   !starts (name, "msam") &&
	   !starts (name, "msbm") &&
	   !starts (name, "bbm") &&
	   !starts (name, "stmary") &&
	   !starts (name, "rsfs") &&
	   !starts (name, "grmn") &&
	   !starts (name, "mac-")
	   // FIXME: better caching of missed tt_locates would be better
	   )
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
  u= tt_locate (name * ".pfb");
  //if (!is_none (u)) cout << name << " -> " << u << "\n";
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
  if (tt_fonts->contains (name)) return tt_fonts[name] == "yes";
  bool yes= !is_none (tt_font_find (name));
  tt_fonts (name)= yes? string ("yes"): string ("no");
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
