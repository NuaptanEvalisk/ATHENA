
/******************************************************************************
* MODULE     : font_guess.cpp
* DESCRIPTION: Font distance computation based on guessed features
* COPYRIGHT  : (C) 2013  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "font.hpp"
#include "font_domain.hpp"
#include "Freetype/tt_tools.hpp"
#include "analyze.hpp"

array<string> common (array<string> v1, array<string> v2);
bool is_glyphs (string s);

/******************************************************************************
* Guessing features
******************************************************************************/

static int
abs_int (int i) {
  return max (i, -i);
}

array<string>
guessed_features (string family, string style) {
  array<string> r;
  array<string> a= font_database_characteristics (family, style);
  //cout << "a= " << a << "\n";

  string slant  = find_attribute_value (a, "slant");
  string vcnt   = find_attribute_value (a, "vcnt");
  string fillp  = find_attribute_value (a, "fillp");
  string lasprat= find_attribute_value (a, "lasprat");
  string pasprat= find_attribute_value (a, "pasprat");
  string lvw    = find_attribute_value (a, "lvw");
  string weight = find_attribute_value (a, "weight");
  string width  = find_attribute_value (a, "width");
  
  bool oblique  = (slant != "" && slant != "0");
  bool italic   = oblique && contains (string ("italic=yes"), a);
  array<string> style_f = style_features (style);
  bool smallcaps= contains (string ("case=smallcaps"), a) ||
                   contains (string ("smallcaps"), style_f);
  bool mono     = contains (string ("mono=yes"), a);
  bool sans     = contains (string ("sans=yes"), a);

  if (vcnt != "" && fillp != "") {
    int vf= as_int (vcnt);
    int fp= as_int (fillp);

    // begin adjustments
    int delta= 0;
    if (oblique) delta += min (abs_int (as_int (slant)), 50) / 4;
    int asprat= 110;
    if (lasprat != "") asprat= as_int (lasprat);
    if (pasprat != "" && mono) asprat= as_int (pasprat);
    int ecart= asprat - 110;
    ecart= max (min (ecart, 80), -40);
    if (ecart > 0) delta += ecart / 8;
    else delta += ecart / 4;
    vf += delta;
    fp += delta;
    // cout << family << ", " << style << " -> " << delta << "\n";
    // end adjustments

    if (vf > 60) r << string ("black");
    else if (vf > 45) r << string ("bold");
    else if (vf > 40 && fp > 40) r << string ("bold");
    else if (vf < 10) r << string ("thin");
    else if (vf < 20 && fp < 30) r << string ("light");
  }
  else if (weight != "") {
    int w= as_int (weight);
    if (w >= 210) r << string ("black");
    else if (w >= 180) r << string ("bold");
    else if (w <= 40) r << string ("thin");
    else if (w <= 55) r << string ("light");
  }

  if (lasprat != "" && pasprat != "" && lvw != "") {
    int lrat= as_int (lasprat);
    int prat= as_int (pasprat);
    int rat = (4*lrat + prat + 2) / 5;
    if (mono) rat= (lrat + prat) / 2;

    // begin adjustments
    int w= as_int (lvw);
    w= min (w, 40);
    rat -= w/2;
    // cout << family << ", " << style << " -> " << (w/2) << "\n";
    // end adjustments

    if (rat < 75) r << string ("condensed");
    else if (rat > 120) r << string ("wide");
  }
  else if (width != "") {
    int w= as_int (width);
    if (w <= 87) r << string ("condensed");
    else if (w >= 113) r << string ("wide");
  }

  if (italic) r << string ("italic");
  else if (oblique) r << string ("oblique");
  if (smallcaps) r << string ("smallcaps");
  if (mono) r << string ("mono");
  if (sans) r << string ("sansserif");

  return r;
}

array<string>
guessed_features (string family) {
  array<string> r;
  array<string> styles= font_database_styles (family);
  for (int i=0; i<N(styles); i++) {
    array<string> a= guessed_features (family, styles[i]);
    if (i == 0) r= a;
    else r= common (r, a);
  }
  array<string> v;
  v << upgrade_family_name (family) << r;
  return v;
}

double
guessed_distance (string fam1, string sty1, string fam2, string sty2) {
  struct memo_cache;
  auto& memo= font_domain_local<hashmap<tree,double>, memo_cache> (1000000.0);
  tree key= tuple (fam1, sty1, fam2, sty2);
  if (memo->contains (key)) return memo[key];
  array<string> v1= font_database_characteristics (fam1, sty1);
  array<string> v2= font_database_characteristics (fam2, sty2);
  double d= characteristic_distance (v1, v2);
  memo (key)= d;
  return d;
}

double
guessed_distance_families (string fam1, string fam2) {
  struct memo_cache;
  auto& memo= font_domain_local<hashmap<tree,double>, memo_cache> (1000000.0);
  tree key= tuple (fam1, fam2);
  if (memo->contains (key)) return memo[key];
  array<string> stys1= font_database_styles (fam1);
  array<string> stys2= font_database_styles (fam2);
  if (N(stys1) == 0) stys1= font_database_global_styles (fam1);
  if (N(stys2) == 0) stys2= font_database_global_styles (fam2);
  double d= 1000000.0;
  for (int i1=0; i1<N(stys1); i1++)
    for (int i2=0; i2<N(stys2); i2++)
      d= min (d, guessed_distance (fam1, stys1[i1], fam2, stys2[i2]));
  memo (key)= d;
  return d;
}

double
guessed_distance (string family1, string family2) {
  struct memo_cache;
  auto& memo= font_domain_local<hashmap<tree,double>, memo_cache> (1000000.0);
  if (family1 == family2) return 0.0;
  tree key= tuple (family1, family2);
  if (memo->contains (key)) return memo[key];
  double d= guessed_distance_families (family1, family2);
  memo (key)= d;
  return d;
}
