
/******************************************************************************
* MODULE     : tex_files.cpp
* DESCRIPTION: manipulation of TeX font files
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tex_files.hpp"
#include "font_domain.hpp"
#include "boot.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "path.hpp"
#include "hashmap.hpp"
#include "analyze.hpp"
#include "tm_timer.hpp"
#include "data_cache.hpp"

#ifdef USE_KPATHSEA_API
#include <cstdlib>
#include "kpathsea_lookup.hpp"
#endif

namespace {
struct local_font_state {
  url the_tfm_path= url_none ();
  url the_pk_path= url_none ();
  url the_pfb_path= url_none ();
  bool tfm_fallback_loaded= false;
  bool pk_fallback_loaded= false;
  bool pfb_fallback_loaded= false;
};
local_font_state& font_state () {
  return font_domain_local<local_font_state> ();
}
}

static url get_kpsepath (string s);

static url
legacy_tex_roots (string kind) {
#ifdef OS_WIN32
  return search_sub_dirs (url ("$TEX_HOME/fonts") * kind);
#else
  static const char* roots[]= {
    "/opt/local/share/texmf-texlive-dist/fonts",
    "/usr/lib/tetex/fonts",
    "/usr/lib/texmf/fonts",
    "/usr/local/lib/texmf/fonts",
    "/usr/share/texmf/fonts",
    "/usr/TeX/lib/texmf/fonts",
    "/var/texfonts",
    "/var/tmp/texfonts"
  };
  url result= url_none ();
  for (unsigned int i=0; i<sizeof (roots) / sizeof (roots[0]); i++) {
    url root= url (roots[i]) * kind;
    if (exists (root)) result= search_sub_dirs (root) | result;
  }
  return result;
#endif
}

static void
load_tfm_fallback () {
  if (font_state ().tfm_fallback_loaded) return;
  font_state ().tfm_fallback_loaded= true;
  url fallback= get_kpsepath ("tfm");
  if (is_none (fallback)) fallback= legacy_tex_roots ("tfm");
  font_state ().the_tfm_path= expand (factor (font_state ().the_tfm_path | fallback));
}

static void
load_pk_fallback () {
  if (font_state ().pk_fallback_loaded) return;
  font_state ().pk_fallback_loaded= true;
  url fallback= get_kpsepath ("pk");
  if (is_none (fallback)) fallback= legacy_tex_roots ("pk");
  font_state ().the_pk_path= expand (factor (font_state ().the_pk_path | fallback));
}

static void
load_pfb_fallback () {
  if (font_state ().pfb_fallback_loaded) return;
  font_state ().pfb_fallback_loaded= true;
  font_state ().the_pfb_path= expand (factor (font_state ().the_pfb_path |
                                legacy_tex_roots ("type1")));
}

/******************************************************************************
* Finding a TeX font
******************************************************************************/

static string
kpsewhich (string name, int format) {
  bench_start ("kpsewhich");
#ifdef USE_KPATHSEA_API
  c_string cname (name);
  char* found= athena_kpathsea_find (
    cname, (athena_kpathsea_format) format);
  string which= found == NULL? string (""): string (found);
  if (found != NULL) std::free (found);
#else
  (void) format;
  string which= var_eval_system ("kpsewhich " * name);
#endif
  bench_cumul ("kpsewhich");
  return which;
}

static url
resolve_tfm (url name) {
  if (is_none (font_state ().the_tfm_path)) reset_tfm_path (false);
  url r= resolve (font_state ().the_tfm_path * name);
  if (!is_none (r)) return r;
  if (get_setting ("KPSEWHICH") == "true") {
    string which= kpsewhich (as_string (name),
#ifdef USE_KPATHSEA_API
                             ATHENA_KPSE_TFM
#else
                             0
#endif
                            );
    if ((which!="") && exists (url_system (which))) return url_system (which);
    // cout << "Missed " << name << "\n";
  }
  else {
    load_tfm_fallback ();
    r= resolve (font_state ().the_tfm_path * name);
  }
  return r;
}

static url
resolve_pk (url name) {
  if (is_none (font_state ().the_pk_path)) reset_pk_path (false);
  url r= resolve (font_state ().the_pk_path * name);
  if (!is_none (r)) return r;
#ifndef OS_WIN32 // The kpsewhich from MikTeX is bugged for pk fonts
  if (get_setting ("KPSEWHICH") == "true") {
    string which= kpsewhich (as_string (name),
#ifdef USE_KPATHSEA_API
                             ATHENA_KPSE_PK
#else
                             0
#endif
                            );
    if ((which!="") && exists (url_system (which))) return url_system (which);
    // cout << "Missed " << name << "\n";
  }
#endif
  bool need_fallback= get_setting ("KPSEWHICH") != "true";
#ifdef OS_WIN32
  need_fallback= true;
#endif
  if (need_fallback) {
    load_pk_fallback ();
    r= resolve (font_state ().the_pk_path * name);
  }
  return r;
}

static url
resolve_pfb (url name) {
  if (is_none (font_state ().the_pfb_path)) reset_pfb_path ();
  url r= resolve (font_state ().the_pfb_path * name);
  if (!is_none (r)) return r;
#ifndef OS_WIN32 // The kpsewhich from MikTeX is bugged for pfb fonts
  if (get_setting ("KPSEWHICH") == "true") {
    string which= kpsewhich (as_string (name),
#ifdef USE_KPATHSEA_API
                             ATHENA_KPSE_TYPE1
#else
                             0
#endif
                            );
    if ((which!="") && exists (url_system (which))) return url_system (which);
    // cout << "Missed " << name << "\n";
  }
#endif
  bool need_fallback= get_setting ("KPSEWHICH") != "true";
#ifdef OS_WIN32
  need_fallback= true;
#endif
  if (need_fallback) {
    load_pfb_fallback ();
    r= resolve (font_state ().the_pfb_path * name);
  }
  return r;
}

url
tfm_font_path () {
  if (is_none (font_state ().the_tfm_path)) reset_tfm_path (false);
  return font_state ().the_tfm_path;
}

/******************************************************************************
* Caching results
******************************************************************************/

url
resolve_tex (url name) {
  string s= as_string (name);
  if (is_cached ("font_cache.scm", s)) {
    string cached= cache_get ("font_cache.scm", s) -> label;
    if (cached == "") return url_none ();
    url u= url_system (cached);
    if (exists (u)) return u;
    cache_reset ("font_cache.scm", s);
  }

  bench_start ("resolve tex");
  url u= url_none ();
  if (ends (s, "mf" )) {
    u= resolve_tfm (name);
#ifdef OS_WIN32
    if (is_none (u))
      u= resolve_tfm (replace (s, ".mf", ".tfm"));
#endif
  }
  if (ends (s, "tfm")) u= resolve_tfm (name);
  if (ends (s, "pk" )) u= resolve_pk  (name);
  if (ends (s, "pfb")) u= resolve_pfb (name);
  bench_cumul ("resolve tex");

  cache_set ("font_cache.scm", s, is_none (u)? string (""): as_string (u));
  //cout << "Resolve " << name << " -> " << u << "\n";
  return u;
}

bool
exists_in_tex (url u) {
  return !is_none (resolve_tex (u));
}

/******************************************************************************
* Automatically generate missing fonts
******************************************************************************/

void
make_tex_tfm (string name) {
  string s;
  int r= 0;
  if (get_setting ("MAKETFM") == "MakeTeXTFM") {
    s= "MakeTeXTFM " * name;
    if (DEBUG_VERBOSE) debug_fonts << "Executing " << s << "\n";
    r= system (s);
  }
  if (get_setting ("MAKETFM") == "mktextfm") {
    url tfm_dir ("$ATHENA_HOME_PATH/fonts/tfm");
    s= "mktextfm " *
      string ("--destdir ") * as_string (tfm_dir) * " " *
      name;
    if (DEBUG_VERBOSE) debug_fonts << "Executing " << s << "\n";
    r= system (s);
    string superfluous= name * ".600pk";
    if (ends (name, ".tfm")) superfluous= name (0, N(name)-4) * ".600pk";
    remove (tfm_dir * superfluous);
  }
  if (get_setting ("MAKETFM") == "maketfm"){
    if (name(N(name) - 4, N(name)) == ".tfm")
      name = name (0, N(name) - 4);
    s = "maketfm --dest-dir \"" * get_env("$ATHENA_HOME_PATH")
      * "\\fonts\\tfm\" " * name;
    if (DEBUG_VERBOSE) debug_fonts << "Executing " << s << "\n";
    r= system (s);
  }
  if (r) cout << "ATHENA] system command failed: " << s << "\n";
}

void
make_tex_pk (string name, int dpi, int design_dpi) {
  string s;
  int r= 0;
  if (get_setting ("MAKEPK") == "MakeTeXPK") {
    s="MakeTeXPK " * name * " " *
      as_string (dpi) * " " * as_string (design_dpi) * " " *
      as_string (dpi) * "/" * as_string (design_dpi) * " localfont";
    if (DEBUG_VERBOSE) debug_fonts << "Executing " << s << "\n";
    r= system (s);
  }
  if (get_setting ("MAKEPK") == "mktexpk") {
    url pk_dir ("$ATHENA_HOME_PATH/fonts/pk");
    s="mktexpk " *
      string ("--dpi ") * as_string (dpi) * " " *
      string ("--bdpi ") * as_string (design_dpi) * " " *
      string ("--mag ") * as_string (dpi) *"/"* as_string (design_dpi) * " " *
      string ("--destdir ") * as_string (pk_dir) * " " *
      name;
    if (DEBUG_VERBOSE) debug_fonts << "Executing " << s << "\n";
    r= system (s);
  }
  if (get_setting ("MAKEPK") == "makepk") {
#ifdef OS_WIN32
    s = "makepk --dest-dir \""
      * get_env("$ATHENA_HOME_PATH") * "\\fonts\\pk\" "
      * name * " " * as_string(dpi) * " " * as_string(design_dpi)
      * " " * as_string(dpi) * "%//" * as_string(design_dpi);
#else
    s = "makepk --dest-dir \""
      * get_env("$ATHENA_HOME_PATH") * "\\fonts\\pk\" "
      * name * " " * as_string(dpi) * " " * as_string(design_dpi)
      * " " * as_string(dpi) * "/" * as_string(design_dpi);
#endif
    if (DEBUG_VERBOSE) debug_fonts << "Executing " << s << "\n";
    r= system (s);
  }
  if (r) cout << "ATHENA] system command failed: " << s << "\n";
}

/******************************************************************************
* Automatic determination of paths where TeX fonts might have been generated
******************************************************************************/

static url
get_kpsepath (string s) {
  // FIXME: adapt to WIN32
  if (get_setting ("KPSEPATH") != "true") return url_none ();
  string r= var_eval_system ("kpsepath " * s);
  if (N(r)==0) return url_none ();

  int i, start, end;
  url p= url_none ();
  for (i=0; i<N(r); i++) {
    while ((i<N(r)) && (r[i]=='!')) i++;
    start=i;
    while ((i<N(r)) && (r[i]!=':')) i++;
    end=i;
    while ((end>start) && (r[end-1]=='/')) end--;
    string dir= r (start, end);
    if (dir == ".") continue;
    p= search_sub_dirs (dir) | p;
  }
  return p;
}

void
reset_tfm_path (bool rehash) { (void) rehash;
  font_state ().tfm_fallback_loaded= false;
  font_state ().the_tfm_path=
    url_here () |
    search_sub_dirs ("$ATHENA_HOME_PATH/fonts/tfm") |
    search_sub_dirs ("$ATHENA_PATH/fonts/tfm") |
    "$TEX_TFM_PATH";
  font_state ().the_tfm_path= expand (factor (font_state ().the_tfm_path));
}

void
reset_pk_path (bool rehash) { (void) rehash;
  font_state ().pk_fallback_loaded= false;
  font_state ().the_pk_path=
    url_here () |
    search_sub_dirs ("$ATHENA_HOME_PATH/fonts/pk") |
    search_sub_dirs ("$ATHENA_PATH/fonts/pk") |
    "$TEX_PK_PATH";
  font_state ().the_pk_path= expand (factor (font_state ().the_pk_path));
}

void
reset_pfb_path () {
  font_state ().pfb_fallback_loaded= false;
  font_state ().the_pfb_path=
    url_here () |
    search_sub_dirs ("$ATHENA_HOME_PATH/fonts/type1") |
    search_sub_dirs ("$ATHENA_PATH/fonts/type1") |
    "$TEX_PFB_PATH";
  font_state ().the_pfb_path= expand (factor (font_state ().the_pfb_path));
}
