
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

static hashmap<string,string> tt_fonts ("no");

#define TT_FONT_PATH_CACHE "font_path_cache.scm"
#define TT_FONT_FILE_INDEX_CACHE "font_file_index.scm"
#define TT_FONT_CACHE_VERSION "1"

static bool tt_font_file_index_ready= false;
static bool tt_font_file_index_building= false;
static bool tt_font_file_index_warmup_disabled= false;

static tree
tt_font_cache_signature (string xtt, string ximp) {
  return tuple (TT_FONT_CACHE_VERSION, xtt, ximp);
}

static tree
tt_font_cache_signature () {
  return tt_font_cache_signature (get_env ("ATHENA_FONT_PATH"),
                                  get_preference ("imported fonts", ""));
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
}

static url
tt_fontconfig_path () {
#if defined OS_MINGW || defined OS_MACOS
  return url_none ();
#else
  static url cached_path= url_none ();
  static bool initialized= false;
  if (initialized) return cached_path;
  initialized= true;

  string out= eval_system ("fc-list --format='%{file}\\n' 2> /dev/null");
  int i= 0;
  string line;
  while (read_line (out, i, line)) {
    line= trim_spaces (line);
    if (line == "") continue;
    url file= url_system (line);
    if (exists (file))
      cached_path= add_to_path (cached_path, head (file));
    if (i >= N(out)) break;
  }
  return cached_path;
#endif
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

  tree key= tuple ("path", tt_font_cache_signature (xtt, ximp));
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
  bench_cumul ("tt font path");
  cache_set (TT_FONT_PATH_CACHE, key, tt_font_path_encode (cached_path));
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

  tree sig= tt_font_cache_signature ();
  tree ready_key= tuple ("ready", sig);
  if (is_cached (TT_FONT_FILE_INDEX_CACHE, ready_key) &&
      cache_get (TT_FONT_FILE_INDEX_CACHE, ready_key)->label == "yes") {
    tt_font_file_index_ready= true;
    tt_font_file_index_building= false;
    return;
  }

  bench_start ("tt font file index");
  url font_path= tt_font_path ();
  array<url> dirs;
  tt_collect_font_dirs (font_path, dirs);
  int indexed= 0;
  for (int i=0; i<N(dirs); i++) {
    bool error= false;
    array<string> files= read_directory (dirs[i], error);
    if (error) continue;
    for (int j=0; j<N(files); j++) {
      if (!tt_font_file_extension (files[j])) continue;
      tree key= tuple ("file", sig, files[j]);
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

  tree key= tuple ("file", tt_font_cache_signature (), name);
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
  else if (use_locate &&
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

  url tt_path= tt_font_path ();
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

  url r= tt_font_find_sub (name);
  if (is_none (r)) cache_set ("font_cache.scm", s, "");
  else cache_set ("font_cache.scm", s, as_string (r));
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
