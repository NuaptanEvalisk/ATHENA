
/******************************************************************************
* MODULE     : font_translate.cpp
* DESCRIPTION: Compatibility between old and new font schemes
* COPYRIGHT  : (C) 2013  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "font.hpp"
#include "Freetype/tt_tools.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "iterator.hpp"
#include <string>
#include <unordered_map>

bool is_weight (string s);
bool is_category (string s);
bool is_glyphs (string s);
bool is_other (string s);

/******************************************************************************
* Translation into internal naming scheme
******************************************************************************/

string
get_family (array<string> v) {
  if (N(v) == 0) return "roman";
  return v[0];
}

string
get_variant (array<string> v) {
  array<string> r;
  for (int i=1; i<N(v); i++) {
    if (v[i] == "mono" && contains (string ("typewriter"), v));
    else if (v[i] == "mono" || v[i] == "typewriter")
      r << string ("tt");
    else if (v[i] == "sansserif")
      r << string ("ss");
    else if (v[i] == "digital" ||
	     v[i] == "pen" || v[i] == "artpen" ||
	     v[i] == "chalk" || v[i] == "marker")
      r << v[i];
    else if (is_category (v[i]))
      r << v[i];
    else if (is_glyphs (v[i]))
      r << v[i];
    else if (is_other (v[i]))
      r << v[i];
  }
  if (N(r) == 0) return "rm";
  return recompose (r, "-");
}

string
get_series (array<string> v) {
  for (int i=1; i<N(v); i++)
    if (is_weight (v[i]))
      return v[i];
  return "medium";
}

string
get_shape (array<string> v) {
  array<string> r;
  for (int i=1; i<N(v); i++)
    if (ends (v[i], "condensed") ||
        ends (v[i], "unextended") ||
        ends (v[i], "wide") ||
        v[i] == "proportional" ||
        (v[i] == "mono" && contains (string ("typewriter"), v)))
      r << v[i];
  for (int i=1; i<N(v); i++)
    if (v[i] == "upright") r << string ("right");
    else if (v[i] == "italic") r << string ("italic");
    else if (v[i] == "oblique") r << string ("slanted");
    else if (v[i] == "mathitalic") r << string ("mathitalic");
    else if (v[i] == "mathupright") r << string ("mathupright");
    else if (v[i] == "mathshape") r << string ("mathshape");
  for (int i=1; i<N(v); i++)
    if (v[i] == "smallcaps") r << string ("small-caps");
    else if (v[i] == "long") r << string ("long");
    else if (v[i] == "flat") r << string ("flat");
  if (N(r) == 0) return "right";
  return recompose (r, "-");
}

/******************************************************************************
* Upgrade old family names
******************************************************************************/

string
upgrade_family_name (string f) {
  static const std::unordered_map<std::string, std::string> names= {
    {"luxi", "Luxi"}, {"ms-andalemo", "Andale Mono"},
    {"ms-arial", "Arial"}, {"ms-comic", "Comic Sans MS"},
    {"ms-courier", "Courier New"}, {"ms-georgia", "Georgia"},
    {"ms-impact", "Impact"}, {"ms-lucida", "Lucida Console"},
    {"ms-tahoma", "Tahoma"}, {"ms-times", "Times New Roman"},
    {"ms-trebuchet", "Trebuchet MS"}, {"ms-verdana", "Verdana"},
    {"apple-gothic", "AppleGothic"}, {"apple-lucida", "Lucida Grande"},
    {"apple-mingliu", "MingLiU"}, {"apple-symbols", "Apple Symbols"},
    {"apple-simsun", "SimSun"}, {"batang", "Batang"},
    {"fireflysung", "AR PL New Sung"}, {"gulim", "Baekmuk Gulim"},
    {"ipa", "IPAMincho"}, {"heiti", "STHeiti"},
    {"kaku", "Hiragino Kaku Gothic ProN"}, {"kochi", "Kochi Gothic"},
    {"lihei", "LiHei Pro"}, {"mingliu", "MingLiU"},
    {"ms-gothic", "MS Gothic"}, {"ms-mincho", "MS Mincho"},
    {"sazanami", "Sazanami Mincho"}, {"simfang", "FangSong_GB2312"},
    {"simhei", "SimHei"}, {"simkai", "KaiTi_GB2312"},
    {"simli", "LiSu"}, {"simsun", "SimSun"}, {"simyou", "YouYuan"},
    {"ttf-japanese", "TakaoPMincho"}, {"ukai", "AR PL ZenKai Uni"},
    {"uming", "AR PL UMing CN"}, {"unbatang", "UnBatang"},
    {"wqy-microhei", "WenQuanYi Micro Hei"},
    {"wqy-zenhei", "WenQuanYi Zen Hei"}, {"dejavu", "DejaVu"},
    {"stix", "Stix"}, {"bonum", "TeX Gyre Bonum"},
    {"chancery", "TeX Gyre Chorus"}, {"pagella", "TeX Gyre Pagella"},
    {"schola", "TeX Gyre Schola"}, {"termes", "TeX Gyre Termes"},
    {"adobe", "Stix"}, {"Duerer", "duerer"},
    {"math-asana", "Asana Math"}, {"math-apple", "Apple Symbols"},
    {"math-bonum", "TeX Gyre Bonum"}, {"math-dejavu", "DejaVu"},
    {"math-lucida", "Lucida Grande"},
    {"math-pagella", "TeX Gyre Pagella"},
    {"math-schola", "TeX Gyre Schola"}, {"math-stix", "Stix"},
    {"math-termes", "TeX Gyre Termes"}, {"modern", "roman"},
    {"TeXmacs Computer Modern", "roman"},
    {"TeXmacs Computer Modern Mono", "roman"},
    {"TeXmacs Computer Modern Sans", "roman"},
    {"cyrillic", "roman"}, {"rm-cyrillic", "roman"}
  };
  if (f == "sys-chinese") return default_chinese_font_name ();
  if (f == "sys-japanese") return default_japanese_font_name ();
  if (f == "sys-korean") return default_korean_font_name ();
  if (f == "sys-taiwanese") return default_taiwanese_font_name ();
  std::string key (f.data (), static_cast<std::size_t> (N(f)));
  auto found= names.find (key);
  if (found == names.end ()) return f;
  return string (found->second.data (), static_cast<int> (found->second.size ()));
}

/******************************************************************************
* Translation from internal naming scheme
******************************************************************************/

bool
is_other_internal (string s) {
  return
    is_other (s) &&
    s != "rm" &&
    s != "ss" &&
    s != "tt" &&
    s != "small-caps" &&
    s != "right" &&
    s != "slanted";
}

array<string>
variant_features (string s) {
  array<string> v= tokenize (s, "-");
  array<string> r;
  for (int i=0; i<N(v); i++)
    if (v[i] == "ss") r << string ("sansserif");
    else if (v[i] == "tt") r << string ("typewriter");
    else if (v[i] == "digital" ||
	     v[i] == "pen" || v[i] == "artpen" ||
	     v[i] == "chalk" || v[i] == "marker")
      r << v[i];
    else if (is_category (v[i]))
      r << v[i];
    else if (is_glyphs (v[i]))
      r << v[i];
    else if (is_other_internal (v[i]))
      r << v[i];
  return r;
}

array<string>
series_features (string s) {
  array<string> r;
  r << s;
  return r;
}

array<string>
shape_features (string s) {
  s= replace (s, "small-caps", "smallcaps");
  array<string> v= tokenize (s, "-");
  array<string> r;
  for (int i=0; i<N(v); i++)
    if (ends (v[i], "condensed") ||
        ends (v[i], "unextended") ||
        ends (v[i], "wide") ||
        v[i] == "mono" ||
        v[i] == "proportional" ||
        v[i] == "italic" ||
        v[i] == "mathitalic" ||
        v[i] == "mathupright" ||
        v[i] == "mathshape" ||
        v[i] == "smallcaps" ||
        v[i] == "long" ||
        v[i] == "flat")
      r << v[i];
    else if (v[i] == "right") r << string ("upright");
    else if (v[i] == "slanted") r << string ("oblique");
  return r;
}

array<string>
logical_font (string family, string variant, string series, string shape) {
  array<string> r;
  r << upgrade_family_name (family);
  r << variant_features (variant);
  r << series_features (series);
  r << shape_features (shape);
  array<string> v;
  for (int i=0; i<N(r); i++)
    if (r[i] != "medium" &&
        r[i] != "upright")
      v << r[i];
  //cout << family << ", " << variant << ", "
  //     << series << ", " << shape << " -> " << v << "\n";
  return v;
}

/******************************************************************************
* Find closest existing font
******************************************************************************/

#define CLOSEST_CACHE "$ATHENA_HOME_PATH/fonts/font-closest-cache.scm"

static hashmap<tree,tree> closest_cache (UNINIT);
static bool closest_cache_loaded= false;

static void
closest_cache_load () {
  if (closest_cache_loaded) return;
  closest_cache_loaded= true;
  if (!exists (CLOSEST_CACHE)) return;
  string s;
  if (load_string (CLOSEST_CACHE, s, false)) return;
  tree t= block_to_scheme_tree (s);
  if (!is_tuple (t) || N(t) == 0 ||
      !is_func (t[0], TUPLE, 2) ||
      t[0][0] != "athena-font-closest-cache" ||
      t[0][1] != font_database_cache_signature ()) return;
  for (int i=1; i<N(t); i++)
    if (is_func (t[i], TUPLE, 2) &&
        is_func (t[i][0], TUPLE, 5) &&
        is_func (t[i][1], TUPLE, 4))
      closest_cache (t[i][0])= t[i][1];
}

static void
closest_cache_save () {
  array<scheme_tree> entries;
  entries << tuple ("athena-font-closest-cache",
                    font_database_cache_signature ());
  iterator<tree> it= iterate (closest_cache);
  while (it->busy ()) {
    tree key= it->next ();
    entries << tuple (key, closest_cache[key]);
  }
  save_string (CLOSEST_CACHE,
               scheme_tree_to_block (tree (TUPLE, entries)));
}

void
font_closest_cache_invalidate () {
  closest_cache= hashmap<tree,tree> (UNINIT);
  closest_cache_loaded= true;
  remove (CLOSEST_CACHE);
}

bool
find_closest (string& family, string& variant, string& series, string& shape,
	      int attempt) {
  font_database_load ();
  closest_cache_load ();
  tree val= tuple (copy (family), variant, series, shape);
  tree key= tuple (copy (family), variant, series, shape, as_string (attempt));
  if (closest_cache->contains (key)) {
    tree t = closest_cache[key];
    family = t[0]->label;
    variant= t[1]->label;
    series = t[2]->label;
    shape  = t[3]->label;
    return t != val;
  }
  /*
  else if (attempt == FONT_ATTEMPTS && family != "modern") {
    family= "modern";
    find_closest (family, variant, series, shape, 0);
    tree t= tuple (family, variant, series, shape);
    closest_cache (key)= t;
    return t != val;
  }
  */
  else {
    //cout << "< " << family << ", " << variant
    //     << ", " << series << ", " << shape << "\n";
    array<string> lfn= logical_font (family, variant, series, shape);
    lfn= apply_substitutions (lfn);
    array<string> pfn= search_font (lfn, attempt);
    array<string> nfn= logical_font (pfn[0], pfn[1]);
    array<string> gfn= guessed_features (pfn[0], pfn[1]);
    //cout << lfn << " -> " << pfn << ", " << nfn << ", " << gfn << "\n";
    gfn << nfn;
    family= get_family (nfn);
    variant= get_variant (nfn);
    series= get_series (nfn);
    shape= get_shape (nfn);
    if ( contains (string ("outline"), lfn) &&
	!contains (string ("outline"), gfn))
      variant= variant * "-poorbbb";
    if ( contains (string ("bold"), lfn) &&
	!contains (string ("bold"), gfn))
      series= series * "-poorbf";
    if ( contains (string ("smallcaps"), lfn) &&
	!contains (string ("smallcaps"), gfn))
      shape= shape * "-poorsc";
    if ((contains (string ("italic"), lfn) ||
         contains (string ("oblique"), lfn)) &&
	!contains (string ("italic"), gfn) &&
        !contains (string ("oblique"), gfn))
      shape= shape * "-poorit";
    //cout << "> " << family << ", " << variant
    //     << ", " << series << ", " << shape << "\n";
    tree t= tuple (family, variant, series, shape);
    closest_cache (key)= t;
    closest_cache_save ();
    return t != val;
  }
}

font
closest_font (string family, string variant, string series, string shape,
	      int sz, int dpi, int attempt) {
  string s=
    family * "-" * variant * "-" *
    series * "-" * shape * "-" *
    as_string (sz) * "-" * as_string (dpi) * "-" * as_string (attempt);
  if (font::instances->contains (s)) return font (s);
  find_closest (family, variant, series, shape, attempt);
  font fn= find_font (family, variant, series, shape, sz, dpi);
  //cout << "Found " << fn->res_name << "\n";
  font::instances (s)= (pointer) fn.rep;
  return fn;
}
