
/******************************************************************************
* MODULE     : xml.cpp
* DESCRIPTION: routines on xml used in scheme
* COPYRIGHT  : (C) 2019  Joris van der Hoeven, Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "convert.hpp"
#include "analyze.hpp"
#include "scheme.hpp"
#include "unicode_text.hpp"

#include <set>
#include <vector>

/******************************************************************************
* Convert between TeXmacs and XML strings
******************************************************************************/

/*
 * According to: https://www.w3.org/TR/2008/REC-xml-20081126/#sec-common-syn
 * NameStartChar ::= ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | [#xD8-#xF6]
 *   | [#xF8-#x2FF] | [#x370-#x37D] | [#x37F-#x1FFF] | [#x200C-#x200D]
 *   | [#x2070-#x218F] | [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF]
 *   | [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
 * NameChar ::= NameStartChar | "-" | "." | [0-9] | #xB7
 *   | [#x0300-#x036F] | [#x203F-#x2040]
 * Name ::= NameStartChar (NameChar)*
 *
 * Currently, we only handle the visible ascii characters
 */
static bool
is_xml_name (char c, int i) {
  if ((c == ':') || (c == '_') || is_alpha (c)) {
    return true;
  } else if (i == 0) {
    return false;
  } else {
    return (c == '-') || (c == '.') || is_digit (c);
  }
}

/*
 * Convert tm_name to xml_name using '_' as escape
 * If x is not the escape and valid xml name, just keep it
 * Else, escape x use "_${as_int(x)}_"
 */
string
tm_to_xml_name (string s) {
  string r;
  int i, n= N(s);
  for (i=0; i<n; i++)
    if (s[i] != '_' && is_xml_name (s[i], i)) r << s[i];
    else r << "_" << as_string ((int) ((unsigned char) s[i])) << "_";
  return r;
}

string
xml_name_to_tm (string s) {
  string r;
  int i, n= N(s);
  for (i=0; i<n; i++)
    if (s[i] != '_') r << s[i];
    else {
      int start= ++i;
      while ((i<n) && (s[i]!='_')) i++;
      r << (char) ((unsigned char) as_int (s (start, i)));
    }
  return r;
}

string
old_tm_to_xml_cdata (string s) {
  string r;
  int i, n= N(s);
  for (i=0; i<n; i++)
    if (s[i] == '&') r << "&amp;";
    else if (s[i] == '>') r << "&gt;";
    else if (s[i] != '<') r << s[i];
    else {
      int start= ++i;
      while ((i<n) && (s[i]!='>')) i++;
      r << "&" << tm_to_xml_name (s (start, i)) << ";";
    }
  return r;
}

object
tm_to_xml_cdata (string s) {
  std::string_view bytes (s.data (), static_cast<std::size_t> (N(s)));
  athena::text::require_utf8 (bytes);
  string r;
  int n= N(s);
  for (int i=0; i<n; ++i)
    if (s[i] == '&') r << "&amp;";
    else if (s[i] == '<') r << "&lt;";
    else if (s[i] == '>') r << "&gt;";
    else r << s[i];
  return object (r);
}

string
old_xml_cdata_to_tm (string s) {
  string r;
  int i, n= N(s);
  for (i=0; i<n; i++)
    if (s[i] == '<') r << "<less>";
    else if (s[i] == '>') r << "<gtr>";
    else if (s[i] != '&') r << s[i];
    else {
      int start= ++i;
      while ((i<n) && (s[i]!=';')) i++;
      string x= "<" * xml_name_to_tm (s (start, i)) * ">";
      if (x == "<amp>") r << "&";
      else r << x;
    }
  return r;
}

string
xml_unspace (string s, bool first, bool last) {
  string r;
  int i= 0, n= N(s);
  if (first) while ((i<n) && is_space (s[i])) i++;
  while (i<n)
    if (!is_space (s[i])) r << s[i++];
    else {
      while ((i<n) && is_space (s[i])) i++;
      if ((i<n) || (!last)) r << ' ';
    }
  return r;
}

/******************************************************************************
* Serializing SXML as XML
*******************************************************************************/

static string
xml_escape (string s, bool attribute) {
  string r;
  for (int i=0; i<N(s);) {
    if (s[i] == '&') {
      int j= i + 1;
      if (j < N(s) && s[j] == '#') {
        j++;
        if (j < N(s) && (s[j] == 'x' || s[j] == 'X')) {
          j++;
          while (j < N(s) && is_hex_digit (s[j])) j++;
        }
        else while (j < N(s) && is_digit (s[j])) j++;
      }
      else while (j < N(s) &&
                  (is_alpha (s[j]) || is_digit (s[j]) || s[j] == '_' ||
                   s[j] == ':' || s[j] == '.' || s[j] == '-')) j++;

      if (j > i + 1 && j < N(s) && s[j] == ';') {
        r << s (i, j + 1);
        i= j + 1;
      }
      else {
        r << "&amp;";
        i++;
      }
    }
    else if (s[i] == '<') { r << "&lt;"; i++; }
    else if (s[i] == '>') { r << "&gt;"; i++; }
    else if (attribute && s[i] == '"') { r << "&quot;"; i++; }
    else r << s[i++];
  }
  return r;
}

static string
xml_escape_text (string s) {
  return xml_escape (s, false);
}

static string
xml_escape_attribute (string s) {
  return xml_escape (s, true);
}

static string
sxml_atom_text (scheme_tree t) {
  if (!is_atomic (t)) return "";
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

static bool
sxml_head (scheme_tree t, string head) {
  return is_tuple (t) && N(t) > 0 && is_atomic (t[0]) && t[0]->label == head;
}

static void serialize_xml_node (scheme_tree t, string& out);

static void
serialize_xml_attributes (scheme_tree attrs, string& out) {
  if (!sxml_head (attrs, "@")) return;

  std::set<std::string> seen;
  std::vector<int> keep;
  for (int i=N(attrs)-1; i>=1; --i) {
    scheme_tree a= attrs[i];
    if (!is_tuple (a) || N(a) < 1 || !is_atomic (a[0])) continue;
    string name= a[0]->label;
    std::string key (as_charp (name), N(name));
    if (seen.insert (key).second) keep.push_back (i);
  }
  for (auto it= keep.rbegin (); it != keep.rend (); ++it) {
    scheme_tree a= attrs[*it];
    string name= a[0]->label;
    out << " " << name << "=\"";
    if (N(a) >= 2) out << xml_escape_attribute (sxml_atom_text (a[1]));
    out << "\"";
  }
}

static void
serialize_xml_node (scheme_tree t, string& out) {
  if (is_atomic (t)) {
    out << xml_escape_text (sxml_atom_text (t));
    return;
  }
  if (!is_tuple (t) || N(t) == 0 || !is_atomic (t[0])) return;

  string head= t[0]->label;
  if (head == "*TOP*") {
    for (int i=1; i<N(t); i++) {
      if (i > 1) out << "\n";
      serialize_xml_node (t[i], out);
    }
    return;
  }
  if (head == "*PI*") {
    if (N(t) >= 2) {
      out << "<?" << sxml_atom_text (t[1]);
      if (N(t) >= 3 && sxml_atom_text (t[2]) != "")
        out << " " << sxml_atom_text (t[2]);
      out << "?>";
    }
    return;
  }
  if (head == "*DOCTYPE*") {
    if (N(t) >= 2) out << "<!DOCTYPE " << sxml_atom_text (t[1]) << ">";
    return;
  }
  if (head == "@") return;

  int content= 1;
  out << "<" << head;
  if (N(t) > 1 && sxml_head (t[1], "@")) {
    serialize_xml_attributes (t[1], out);
    content= 2;
  }
  if (content >= N(t)) {
    out << "/>";
    return;
  }
  out << ">";
  for (int i=content; i<N(t); i++) serialize_xml_node (t[i], out);
  out << "</" << head << ">";
}

string
serialize_xml (scheme_tree t) {
  string out;
  serialize_xml_node (t, out);
  return out;
}
