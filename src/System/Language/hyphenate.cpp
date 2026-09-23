
/******************************************************************************
* MODULE     : hyphenate.cpp
* DESCRIPTION: hyphenation by Liang's algorithm
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "file.hpp"
#include "hyphenate.hpp"
#include "analyze.hpp"
#include "universal.hpp"
#include "unicode_text.hpp"
#include "sys_utils.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEARCH 10
#define MAX_BUFFER_SIZE 256

/*
static bool
my_strncmp (char* s1, char* s2, int len) {
  int i;
  for (i=0; i<len; i++) if (s1[i]!=s2[i]) return false;
  return true;
}
*/

void
goto_next_char (string s, int& i) {
  if (i >= N(s)) return;
  i= static_cast<int> (athena::text::next_scalar (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))),
    static_cast<std::size_t> (i)));
}

static string
unpattern (string s) {
  int i, j, n= N(s);
  string r;
  for (i=0; i<n; ) {
    while (i<n && is_digit (s[i])) goto_next_char (s, i);
    j = i;
    goto_next_char (s, j);
    if (i<n) r << s(i,j);
    i = j;
  }
  return r;
}

static string
hyphen_normalize (string s) {
  int i;
  string r (0);
  for (i=0; i<N(s); i++)
    if ((i+3<N(s)) && (s[i]=='^') && (s[i+1]=='^')) {
      r << from_hexadecimal (s (i+2, i+4));
      i+=3;
    }
    else r << s[i];
  return r;
}

void
load_hyphen_tables (string file_name,
                     hashmap<string,string>& patterns,
                     hashmap<string,string>& hyphenations) {
  string s;
  file_name= string ("hyphen.") * file_name;
  load_string (url ("$ATHENA_PATH/langs/natural/hyphen", file_name), s, true);
  if (DEBUG_VERBOSE)
    debug_automatic << "ATHENA] Loading " << file_name << "\n";

  athena::text::require_utf8 (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))));

  hashmap<string,string> H ("?");
  bool pattern_flag=false;
  bool hyphenation_flag=false;
  int i=0, n= N(s);
  while (i<n) {
    string buffer;
    while ((i<n) && (s[i]!=' ') &&
           (s[i]!='\t') && (s[i]!='\n') && (s[i]!='\r')) {
      if (s[i] != '%') buffer << s[i++];
      else while ((i<n) && (s[i]!='\n')) i++;
    }
    if (i<n) i++;
    if (buffer == "}") {
      pattern_flag=false;
      hyphenation_flag=false;
    }
    if (pattern_flag && i != 0 && N(buffer) != 0) {
      string norm= hyphen_normalize (buffer);
      patterns (unpattern (norm))= norm;
      //cout << buffer << " ==> " << unpattern (norm, !toCork) << " ==> " << norm << "\n";
    }
    if (hyphenation_flag && i != 0 && N(buffer) != 0) {
      string word= replace (buffer, "-", "");
      hyphenations (word)= buffer;
      //cout << word << " --> " << buffer << "\n";
      // bug: shows the hyphenation "something} --> some-thing}" for english
    }
    if (buffer == "\\patterns{") pattern_flag=true;
    if (buffer == "\\hyphenation{") hyphenation_flag=true;
  }
}

string
sub_str (string s, int i, int len) {
  // i: start (index is encoding-dependent, i.e. it is not a number of characters)
  // len: length in characters (encoding-independent)
  int j=i, k=0;
  for (k = 0; k < len; k++)
    goto_next_char (s, j);
  return s (i, j);
}

int
str_ind (string s, int ind) {
  int i=0, k;
  for (k=0; k<ind; k++)
    goto_next_char (s, i);
  return i;
}

int
str_length (string s) {
  return (int) athena::text::byte_to_codepoint (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))),
    static_cast<std::size_t> (N(s)));
}

static array<int>
get_hyphens_scalar (string s,
                    hashmap<string,string> patterns,
                    hashmap<string,string> hyphenations) {
  ASSERT (N(s) != 0, "hyphenation of empty string");
  s= uni_locase_all (s);
  athena::text::require_utf8 (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))));

  if (hyphenations->contains (s)) {
    string h= hyphenations [s];
    array<int> penalty (str_length (s)-1);
    int i=0, j=0;
    while (h[j] == '-') j++;
    i++; goto_next_char (h, j);
    while (i < N(penalty)+1) {
      penalty[i-1]= HYPH_INVALID;
      while (j < N(h) && h[j] == '-') {
        penalty[i-1]= HYPH_STD;
        j++;
      }
      i++;
      goto_next_char (h, j);
    }
    //cout << s << " --> " << penalty << "\n";
    return penalty;
  }
  else {
    s= "." * s * ".";
    int i, j, k, l, m, len, slen= str_length (s);
    array<int> T (slen+1);
    for (i=0; i<N(T); i++) T[i]=0;
    for (len=1; len < MAX_SEARCH; len++)
      for (i=0, l=0;
          i<str_ind (s, slen-len+1);
          goto_next_char (s, i), l++) {
        string r= patterns [sub_str (s, i, len)];
        if (!(r == "?")) {
          // cout << "  " << sub_str (s, i, len, utf8) << " => " << r << "\n";
          for (j=0, k=0; j<=len; j++, goto_next_char (r, k)) {
            if (k<N(r) && is_digit (r[k])) {
              m= ((int) r[k])-((int) '0');
              goto_next_char (r, k);
            }
            else m=0;
            if (m>T[l+j]) T[l+j]=m;
          }
        }
      }

    array<int> penalty (N(T)-4);
    for (i=2; i < N(T)-4; i++)
      penalty [i-2]= (((T[i]&1)==1)? HYPH_STD: HYPH_INVALID);
    if (N(penalty)>0) penalty[0] = penalty[N(penalty)-1] = HYPH_INVALID;
    if (N(penalty)>1) penalty[1] = penalty[N(penalty)-2] = HYPH_INVALID;
    if (N(penalty)>2) penalty[N(penalty)-3] = HYPH_INVALID;
    // cout << s << " --> " << penalty << "\n";
    return penalty;
  }
}

array<int>
get_hyphens (string s,
             hashmap<string,string> patterns,
             hashmap<string,string> hyphenations) {
  athena::text::require_utf8 (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))));
  array<int> scalar= get_hyphens_scalar (s, patterns, hyphenations);
  array<int> bytes (max (0, N(s)-1));
  for (int i=0; i<N(bytes); ++i) bytes[i]= HYPH_INVALID;
  const std::string_view text (s.data (), static_cast<std::size_t> (N(s)));
  std::size_t boundary= 0;
  for (int i=0; i<N(scalar); ++i) {
    boundary= athena::text::next_scalar (text, boundary);
    if (boundary > 0 && boundary-1 < static_cast<std::size_t> (N(bytes)))
      bytes[(int) boundary-1]= scalar[i];
  }
  return bytes;
}

void
std_hyphenate (string s, int after, string& left, string& right, int penalty) {
  ASSERT (after >= 0 && after < N(s), "hyphenation position out of range");
  left = s (0, after+1);
  right= s (after+1, N(s));
  if (penalty >= HYPH_INVALID) left << string ("\\");
  else left << string ("-");
  //cout << "Yields " << left << ", " << right << "\n";
}
