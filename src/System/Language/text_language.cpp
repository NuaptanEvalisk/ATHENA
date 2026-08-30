
/******************************************************************************
* MODULE     : text_language.cpp
* DESCRIPTION: natural textual languages
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_configure.hpp"
#include "analyze.hpp"
#include "hyphenate.hpp"
#include "impl_language.hpp"
#include "sys_utils.hpp"
#include "unicode_ranges.hpp"

/******************************************************************************
* Shared Unicode CJK classification
******************************************************************************/

static bool
read_unicode_entity (string s, int pos, int& next, int& code) {
  int i= pos + 2;
  if (pos + 3 >= N(s) || s[pos] != '<' || s[pos+1] != '#')
    return false;
  code= 0;
  while (i < N(s) && s[i] != '>') {
    if (!is_hex_digit (s[i])) return false;
    code= (code << 4) + from_hexadecimal (s (i, i+1));
    i++;
  }
  if (i >= N(s) || i == pos + 2) return false;
  next= i + 1;
  return true;
}

static bool
is_cjk_letter_code (int code) {
  return unicode_is_east_asian_letter (code) ||
         (code >= 0xFF00 && code <= 0xFFEF);
}

static bool
is_cjk_punctuation_code (int code) {
  return (code >= 0x3000 && code <= 0x303F) ||
         code == 0xFF01 || code == 0xFF0C || code == 0xFF0E ||
         code == 0xFF1A || code == 0xFF1B || code == 0xFF1F;
}

static bool
is_cjk_forbidden_line_start (string s, int pos) {
  int next, code;
  if (pos >= N(s)) return true;
  if (is_punctuation (s[pos])) return true;
  return read_unicode_entity (s, pos, next, code) &&
         is_cjk_punctuation_code (code);
}

static text_property
advance_cjk_entity (string s, int& pos) {
  int code, next;
  if (!read_unicode_entity (s, pos, next, code)) return NULL;
  if (!is_cjk_letter_code (code) && !is_cjk_punctuation_code (code))
    return NULL;

  bool next_forbidden_start= is_cjk_forbidden_line_start (s, next);
  pos= next;
  if (is_cjk_punctuation_code (code)) {
    if (next_forbidden_start)
      return &tp_cjk_no_break_period_rep;
    return &tp_cjk_period_rep;
  }
  if (next_forbidden_start)
    return &tp_cjk_no_break_rep;
  return &tp_cjk_normal_rep;
}

/******************************************************************************
* Western text languages / 8 bit charset
******************************************************************************/

struct text_language_rep: language_rep {
  hashmap<string,string> patterns;
  hashmap<string,string> hyphenations;
  hashmap<string,array<int> > hyphen_cache;

  text_language_rep (string lan_name, string hyph_name);
  text_property advance (tree t, int& pos);
  array<int> get_hyphens (string s);
  void hyphenate (string s, int after, string& left, string& right);
};

text_language_rep::text_language_rep (string lan_name, string hyph_name):
  language_rep (lan_name), patterns ("?"), hyphenations ("?"),
  hyphen_cache (array<int> ()) {
    load_hyphen_tables (hyph_name, patterns, hyphenations, true); }

text_property
text_language_rep::advance (tree t, int& pos) {
  string s= t->label;
  if (pos >= N(s)) return &tp_normal_rep;

  if (s[pos]==' ') {
    pos++;
    // while ((pos<N(s)) && (s[pos]==' ')) pos++;
    if ((pos == N(s)) || (!is_punctuation (s[pos])))
      return &tp_space_rep;
    return &tp_nb_space_rep;
  }

  text_property cjk= advance_cjk_entity (s, pos);
  if (cjk != NULL) return cjk;
  
  if (is_punctuation (s[pos])) {
    while ((pos<N(s)) && is_punctuation (s[pos])) pos++;
    if ((pos==N(s)) || (s[pos]!=' ')) return &tp_normal_rep;
    switch (s[pos-1]) {
    case ',': case ':': case ';': case '`': case '\'':
      return &tp_space_rep;
    case '.': case '!': case '?':
      return &tp_period_rep;
    }
    return &tp_space_rep;
  }

  if (s[pos]=='-') {
    pos++;
    while ((pos<N(s)) && (s[pos]=='-')) pos++;
    return &tp_hyph_rep;
  }

  if (is_iso_alpha (s[pos])) {
    while ((pos<N(s)) && is_iso_alpha (s[pos])) pos++;
    return &tp_normal_rep;
  }

  if (is_numeric (s[pos])) { // can not be a '.'
    while ((pos<N(s)) && is_numeric (s[pos])) pos++;
    while (s[pos-1]=='.') pos--;
    return &tp_normal_rep;
  }

  if (s[pos]=='<') {
    while ((pos<N(s)) && (s[pos]!='>')) pos++;
    if (pos<N(s)) pos++;
    return &tp_normal_rep;
  }

  pos++;
  return &tp_normal_rep;
}

array<int>
text_language_rep::get_hyphens (string s) {
  if (hyphen_cache->contains (s)) return hyphen_cache[s];
  if (N(hyphen_cache) >= 8192) hyphen_cache->clear ();
  array<int> result= ::get_hyphens (s, patterns, hyphenations);
  hyphen_cache (s)= result;
  return result;
}

void
text_language_rep::hyphenate (
  string s, int after, string& left, string& right)
{
  array<int> penalty= get_hyphens (s);
  std_hyphenate (s, after, left, right, penalty[after]);
}

/******************************************************************************
* French typography
******************************************************************************/

struct french_language_rep: language_rep {
  hashmap<string,string> patterns;
  hashmap<string,string> hyphenations;
  hashmap<string,array<int> > hyphen_cache;

  french_language_rep (string lan_name, string hyph_name);
  text_property advance (tree t, int& pos);
  array<int> get_hyphens (string s);
  void hyphenate (string s, int after, string& left, string& right);
};

french_language_rep::french_language_rep (string lan_name, string hyph_name):
  language_rep (lan_name), patterns ("?"), hyphenations ("?"),
  hyphen_cache (array<int> ()) {
    load_hyphen_tables (hyph_name, patterns, hyphenations, true); }

inline bool
is_french_punctuation (char c) {
  return is_punctuation (c) || (c=='\23') || (c=='\24');
}

text_property
french_language_rep::advance (tree t, int& pos) {
  string s= t->label;
  if (pos >= N(s)) return &tp_normal_rep;

  if (s[pos]==' ') {
    pos++;
    if (pos>1 && s[pos-2] == '\23')
      return &tp_nb_thin_space_rep;
    // while ((pos<N(s)) && (s[pos]==' ')) pos++;
    if ((pos == N(s)) || (!is_french_punctuation (s[pos])))
      return &tp_space_rep;
    if (s[pos] == '\23')
      return &tp_space_rep;
    if (/*s[pos] == ':' ||*/ s[pos] == ';' ||
        s[pos] == '!' || s[pos] == '?' || s[pos] == '\24')
      return &tp_nb_thin_space_rep;
    return &tp_nb_space_rep;
  }

  text_property cjk= advance_cjk_entity (s, pos);
  if (cjk != NULL) return cjk;
  
  if (is_french_punctuation (s[pos])) {
    while ((pos<N(s)) && is_french_punctuation (s[pos])) pos++;
    if ((pos==N(s)) || (s[pos]!=' ')) return &tp_normal_rep;
    switch (s[pos-1]) {
    case '\23':
      return &tp_nb_thin_space_rep;
    case '\24':
    case ',': case ':': case ';': case '`': case '\'':
      return &tp_space_rep;
    case '.': case '!': case '?':
      return &tp_period_rep;
    }
    return &tp_space_rep;
  }

  if (s[pos]=='-') {
    pos++;
    while ((pos<N(s)) && (s[pos]=='-')) pos++;
    return &tp_hyph_rep;
  }

  if (is_iso_alpha (s[pos])) {
    while ((pos<N(s)) && is_iso_alpha (s[pos])) pos++;
    return &tp_normal_rep;
  }

  if (is_numeric (s[pos])) { // can not be a '.'
    while ((pos<N(s)) && is_numeric (s[pos])) pos++;
    while (s[pos-1]=='.') pos--;
    return &tp_normal_rep;
  }

  if (s[pos]=='<') {
    while ((pos<N(s)) && (s[pos]!='>')) pos++;
    if (pos<N(s)) pos++;
    return &tp_normal_rep;
  }

  pos++;
  return &tp_normal_rep;
}

array<int>
french_language_rep::get_hyphens (string s) {
  if (hyphen_cache->contains (s)) return hyphen_cache[s];
  if (N(hyphen_cache) >= 8192) hyphen_cache->clear ();
  array<int> result= ::get_hyphens (s, patterns, hyphenations);
  hyphen_cache (s)= result;
  return result;
}

void
french_language_rep::hyphenate (
  string s, int after, string& left, string& right)
{
  array<int> penalty= get_hyphens (s);
  std_hyphenate (s, after, left, right, penalty[after]);
}

/******************************************************************************
* Eastern text languages / using UCS entities
******************************************************************************/

struct ucs_text_language_rep: language_rep {
  hashmap<string,string> patterns;
  hashmap<string,string> hyphenations;
  hashmap<string,array<int> > hyphen_cache;

  ucs_text_language_rep (string lan_name, string hyph_name);
  text_property advance (tree t, int& pos);
  array<int> get_hyphens (string s);
  void hyphenate (string s, int after, string& left, string& right);
  bool unicode;
};

ucs_text_language_rep::ucs_text_language_rep (string lan_name, string hyph_name):
  language_rep (lan_name), patterns ("?"), hyphenations ("?"),
  hyphen_cache (array<int> ())
  { load_hyphen_tables (hyph_name, patterns, hyphenations, false); }

text_property
ucs_text_language_rep::advance (tree t, int& pos) {
  //TODO: replace methods is_punctuation (), is_iso_alpha () and is_numeric (),
  //      by equivalents taking into account unicode entities.
  string s= t->label;
  if (pos >= N(s)) return &tp_normal_rep;

  if (s[pos]==' ') {
    pos++;
    // while ((pos<N(s)) && (s[pos]==' ')) pos++;
    if ((pos == N(s)) || (!is_punctuation (s[pos])))
      return &tp_space_rep;
    return &tp_nb_space_rep;
  }

  text_property cjk= advance_cjk_entity (s, pos);
  if (cjk != NULL) return cjk;
  
  if (is_punctuation (s[pos])) {
    while ((pos<N(s)) && is_punctuation (s[pos])) pos++;
    if ((pos==N(s)) || (s[pos]!=' ')) return &tp_normal_rep;
    switch (s[pos-1]) {
    case ',': case ':': case ';': case '`': case '\'':
      return &tp_space_rep;
    case '.': case '!': case '?':
      return &tp_period_rep;
    }
    return &tp_space_rep;
  }

  if (s[pos]=='-') {
    pos++;
    while ((pos<N(s)) && (s[pos]=='-')) pos++;
    return &tp_hyph_rep;
  }

  if (is_iso_alpha (s[pos]) || (s[pos]=='<')) {
    while ((pos<N(s)) && (is_iso_alpha (s[pos]) || (s[pos]=='<'))) {
      if (s[pos]=='<') {
        while ((pos<N(s)) && (s[pos]!='>')) pos++;
        if (pos<N(s)) pos++;
      }
      else
        pos++;
    }
    return &tp_normal_rep;
  }

  if (is_numeric (s[pos])) { // can not be a '.'
    while ((pos<N(s)) && is_numeric (s[pos])) pos++;
    while (s[pos-1]=='.') pos--;
    return &tp_normal_rep;
  }

  pos++;
  return &tp_normal_rep;
}

array<int>
ucs_text_language_rep::get_hyphens (string s) {
  if (hyphen_cache->contains (s)) return hyphen_cache[s];
  if (N(hyphen_cache) >= 8192) hyphen_cache->clear ();
  array<int> result= ::get_hyphens (s, patterns, hyphenations, true);
  hyphen_cache (s)= result;
  return result;
}

void
ucs_text_language_rep::hyphenate (
  string s, int after, string& left, string& right)
{
  array<int> penalty= get_hyphens (s);
  std_hyphenate (s, after, left, right, penalty[after], true);
}

/******************************************************************************
* Oriental languages
******************************************************************************/

struct oriental_language_rep: language_rep {
  hashmap<string,bool> punct;
  hashmap<string,bool> wide_punct;
  oriental_language_rep (string lan_name);
  text_property advance (tree t, int& pos);
  array<int> get_hyphens (string s);
  void hyphenate (string s, int after, string& left, string& right);
};

oriental_language_rep::oriental_language_rep (string lan_name):
  language_rep (lan_name), punct (false)
{
  punct (".")= true;
  punct (",")= true;
  punct (":")= true;
  punct (";")= true;
  punct ("!")= true;
  punct ("?")= true;
  punct ("<#3000>")= true;
  punct ("<#3001>")= true;
  punct ("<#3002>")= true;
  punct ("<#3003>")= true;
  punct ("<#3004>")= true;
  punct ("<#3005>")= true;
  punct ("<#3006>")= true;
  punct ("<#3007>")= true;
  punct ("<#3008>")= true;
  punct ("<#3009>")= true;
  punct ("<#300a>")= true;
  punct ("<#300b>")= true;
  punct ("<#300c>")= true;
  punct ("<#300d>")= true;
  punct ("<#300e>")= true;
  punct ("<#300f>")= true;
  punct ("<#300A>")= true;
  punct ("<#300B>")= true;
  punct ("<#300C>")= true;
  punct ("<#300D>")= true;
  punct ("<#300E>")= true;
  punct ("<#300F>")= true;
  punct ("<#ff01>")= true;
  punct ("<#ff0c>")= true;
  punct ("<#ff0e>")= true;
  punct ("<#ff1a>")= true;
  punct ("<#ff1b>")= true;
  punct ("<#ff1f>")= true;
  punct ("<#FF01>")= true;
  punct ("<#FF0C>")= true;
  punct ("<#FF0E>")= true;
  punct ("<#FF1A>")= true;
  punct ("<#FF1B>")= true;
  punct ("<#FF1F>")= true;

  //wide_punct ("<#3001>")= true;
  //wide_punct ("<#ff01>")= true;
  //wide_punct ("<#ff0c>")= true;
  //wide_punct ("<#ff0e>")= true;
  //wide_punct ("<#ff1a>")= true;
  //wide_punct ("<#ff1b>")= true;
  //wide_punct ("<#ff1f>")= true;
  //wide_punct ("<#FF01>")= true;
  //wide_punct ("<#FF0C>")= true;
  //wide_punct ("<#FF0E>")= true;
  //wide_punct ("<#FF1A>")= true;
  //wide_punct ("<#FF1B>")= true;
  //wide_punct ("<#FF1F>")= true;
}

text_property
oriental_language_rep::advance (tree t, int& pos) {
  string s= t->label;
  if (pos >= N(s)) return &tp_normal_rep;

  if (s[pos] == ' ') {
    pos++;
    return &tp_space_rep;
  }

  if (test (s, pos, "<#3000>")) {
    pos += 7;
    return &tp_space_before_rep;
  }

  if (s[pos] == '<' && !test (s, pos, "<#")) {
    while ((pos<N(s)) && (s[pos]!='>')) pos++;
    if (pos<N(s)) pos++;
    return &tp_normal_rep;
  }

  if (pos < N(s) && !test (s, pos, "<#")) {
    while (pos < N(s) && s[pos] != ' ' && s[pos] != '<')
      tm_char_forwards (s, pos);
    return &tp_cjk_no_break_rep;
  }

  int start= pos;
  tm_char_forwards (s, pos);
  string c= s (start, pos);
  int next= pos;
  tm_char_forwards (s, next);
  string x= s (pos, next);

  if (punct->contains (c)) {
    if (punct->contains (x) || pos == N(s))
      return &tp_cjk_no_break_period_rep;
    else if (wide_punct->contains (c))
      return &tp_cjk_wide_period_rep;
    else return &tp_cjk_period_rep;
  }
  else {
    if (punct->contains (x) || pos == N(s))
      return &tp_cjk_no_break_rep;
    else return &tp_cjk_normal_rep;
  }
}

array<int>
oriental_language_rep::get_hyphens (string s) {
  int i;
  array<int> penalty (N(s)+1);
  for (i=0; i<N(penalty); i++) penalty[i]= HYPH_INVALID;
  return penalty;
}

void
oriental_language_rep::hyphenate (
  string s, int after, string& left, string& right)
{
  left = s (0, after+1);
  right= s (after+1, N(s));
}

/******************************************************************************
* Main interface
******************************************************************************/

array<string>
get_supported_languages () {
  array<string> r;
  r << string ("american")
    << string ("british")
    << string ("bulgarian")
    << string ("chinese")
    << string ("croatian")
    << string ("czech")
    << string ("danish")
    << string ("dutch")
    << string ("english")
    << string ("esperanto")
    << string ("finnish")
    << string ("french")
    << string ("german")
    << string ("greek")
    << string ("hungarian")
    << string ("italian")
    << string ("japanese")
    << string ("korean")
    << string ("polish")
    << string ("portuguese")
    << string ("romanian")
    << string ("russian")
    << string ("slovak")
    << string ("slovene")
    << string ("spanish")
    << string ("swedish")
    << string ("taiwanese")
    << string ("ukrainian");
  return r;
}

typedef const char* const_char_ptr;

static language
make_ucs_text_language (string s, string h) {
  return tm_new<ucs_text_language_rep> (s, h);
}

static language
make_text_language (string s, string h) {
  return tm_new<text_language_rep> (s, h);
}

static language
make_french_language (string s, string h) {
  return tm_new<french_language_rep> (s, h);
}

static language
make_oriental_language (string s) {
  return tm_new<oriental_language_rep> (s);
}

language
text_language (string s) {
  if (language::instances -> contains (s)) return language (s);
  if (s == "american")   return make_text_language (s, "us");
  if (s == "british")    return make_text_language (s, "ukenglish");
  if (s == "bulgarian")  return make_ucs_text_language (s, "bulgarian");
  if (s == "chinese")    return make_oriental_language (s);
  if (s == "croatian")   return make_text_language (s, "croatian");
  if (s == "czech")      return make_text_language (s, "czech");
  if (s == "danish")     return make_text_language (s, "danish");
  if (s == "dutch")      return make_text_language (s, "dutch");
  if (s == "english")    return make_text_language (s, "us");
  if (s == "esperanto")    return make_text_language (s, "esperanto");
  if (s == "finnish")    return make_text_language (s, "finnish");
  if (s == "french")     return make_french_language (s, "french");
  if (s == "german")     return make_text_language (s, "german");
  if (s == "greek")      return make_text_language (s, "greek");
  if (s == "hungarian")  return make_text_language (s, "hungarian");
  if (s == "italian")    return make_text_language (s, "italian");
  if (s == "japanese")   return make_oriental_language (s);
  if (s == "korean")     return make_oriental_language (s);
  if (s == "polish")     return make_text_language (s, "polish");
  if (s == "portuguese") return make_text_language (s, "portuguese");
  if (s == "romanian")   return make_text_language (s, "romanian");
  if (s == "russian")    return make_ucs_text_language (s, "russian");
  if (s == "slovak")     return make_text_language (s, "slovak");
  if (s == "slovene")    return make_text_language (s, "slovene");
  if (s == "spanish")    return make_text_language (s, "spanish");
  if (s == "swedish")    return make_text_language (s, "swedish");
  if (s == "taiwanese")  return make_oriental_language (s);
  if (s == "ukrainian")  return make_ucs_text_language (s, "ukrainian");
  if (s == "verbatim")   return tm_new<verb_language_rep> ("verbatim");
  if (s != "") std_warning << "Unkown language: " << s << "\n";
  return text_language ("verbatim");
}
