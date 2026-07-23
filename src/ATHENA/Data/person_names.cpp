/******************************************************************************
* MODULE     : person_names.cpp
* DESCRIPTION: semantic person-name recognition for ATHENA documents
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/person_names.hpp"

#include "drd_mode.hpp"
#include "file.hpp"
#include "tree_traverse.hpp"
#include "url.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

namespace {

struct PersonDictionary {
  std::unordered_map<uint64_t, std::vector<string>> buckets;
  bool loaded= false;
};

PersonDictionary dictionary;
std::mutex dictionary_mutex;

bool
person_word_character (unsigned char c) {
  return c >= 0x80 || std::isalnum (c) != 0 || c == '_';
}

string
trim_person_name (string name) {
  int first= 0;
  int last= N(name);
  while (first < last &&
         std::isspace ((unsigned char) name[first]) != 0)
    first++;
  while (last > first &&
         std::isspace ((unsigned char) name[last - 1]) != 0)
    last--;
  return name (first, last);
}

bool
eligible_person_name (string name, bool family_name) {
  name= trim_person_name (name);
  if (N(name) == 0 || N(name) > 240) return false;

  bool has_space= false;
  int word_chars= 0;
  for (int i=0; i<N(name); i++) {
    unsigned char c= (unsigned char) name[i];
    if (std::isspace (c) != 0) has_space= true;
    if (person_word_character (c)) word_chars++;
  }
  if (family_name) {
    unsigned char first= (unsigned char) name[0];
    if (first < 0x80 && std::isupper (first) == 0) return false;
    return word_chars >= 5;
  }
  return word_chars >= (has_space ? 3 : 5);
}

uint64_t
person_prefix_key (const string& text, int start, int length) {
  uint64_t key= (uint64_t) length << 32;
  for (int i=0; i<length; i++)
    key |= (uint64_t) (unsigned char) text[start + i] << (8 * i);
  return key;
}

bool
person_name_matches_at (const string& text, int start, const string& name) {
  if (start + N(name) > N(text)) return false;
  for (int i=0; i<N(name); i++)
    if (text[start + i] != name[i]) return false;
  return true;
}

void
load_person_dictionary_unlocked () {
  if (dictionary.loaded) return;
  dictionary.loaded= true;

  string source;
  url file= url ("$ATHENA_PATH/misc/person-names/"
                 "wikidata-person-names.tsv");
  if (load_string (file, source, false)) return;

  std::vector<string> names;
  int line_begin= 0;
  for (int i=0; i<=N(source); i++) {
    if (i != N(source) && source[i] != '\n') continue;
    string line= source (line_begin, i);
    line_begin= i + 1;
    if (N(line) == 0 || line[0] == '#') continue;
    int first_tab= search_forwards ("\t", 0, line);
    string utf8_name= first_tab < 0 ? line : line (0, first_tab);
    int second_tab= first_tab < 0 ? -1 :
      search_forwards ("\t", first_tab + 1, line);
    string kind= first_tab < 0 ? "label" :
      line (first_tab + 1, second_tab < 0 ? N(line) : second_tab);
    string name= trim_person_name (utf8_to_cork (utf8_name));
    if (eligible_person_name (name, kind == "family"))
      names.push_back (name);
  }

  std::sort (names.begin (), names.end ());
  names.erase (std::unique (names.begin (), names.end ()), names.end ());
  for (string& name: names) {
    int prefix_length= min (4, N(name));
    dictionary.buckets[
      person_prefix_key (name, 0, prefix_length)].push_back (std::move (name));
  }
  for (auto& entry: dictionary.buckets)
    std::sort (entry.second.begin (), entry.second.end (),
      [] (const string& a, const string& b) {
        if (N(a) != N(b)) return N(a) > N(b);
        return a < b;
      });
}

bool
name_boundary_before (const string& text, int position) {
  return position == 0 ||
         !person_word_character ((unsigned char) text[position - 1]);
}

bool
name_boundary_after (const string& text, int position) {
  return position >= N(text) ||
         !person_word_character ((unsigned char) text[position]);
}

int
longest_person_name_at_unlocked (
  const string& text, int start, const std::vector<string>& trusted_names)
{
  if (!name_boundary_before (text, start)) return -1;

  int longest= -1;
  int maximum_prefix= min (4, N(text) - start);
  for (int prefix_length= maximum_prefix; prefix_length>=1; prefix_length--) {
    auto found= dictionary.buckets.find (
      person_prefix_key (text, start, prefix_length));
    if (found == dictionary.buckets.end ()) continue;
    for (const string& name: found->second) {
      int end= start + N(name);
      if (end <= longest) break;
      if (person_name_matches_at (text, start, name) &&
          name_boundary_after (text, end)) {
        longest= end;
        break;
      }
    }
  }
  for (const string& name: trusted_names) {
    int end= start + N(name);
    if (end <= longest || end > N(text)) continue;
    if (text (start, end) == name && name_boundary_after (text, end))
      longest= end;
  }
  return longest;
}

tree
normalize_atomic_unlocked (tree atom, int& wrapped,
                           const std::vector<string>& trusted_names) {
  const string& text= atom->label;
  tree result (CONCAT);
  int plain_begin= 0;
  int cursor= 0;
  int local_wrapped= 0;

  while (cursor < N(text)) {
    int end= longest_person_name_at_unlocked (text, cursor, trusted_names);
    if (end < 0) {
      cursor++;
      continue;
    }

    if (plain_begin < cursor) result << tree (text (plain_begin, cursor));
    result << compound ("person", tree (text (cursor, end)));
    wrapped++;
    local_wrapped++;
    cursor= end;
    plain_begin= end;
  }

  if (local_wrapped == 0) return copy (atom);
  if (plain_begin < N(text)) result << tree (text (plain_begin, N(text)));
  if (N(result) == 1) return result[0];
  return result;
}

bool
skip_person_normalization_subtree (tree t) {
  if (is_atomic (t)) return false;
  string label= as_string (L(t));
  return label == "person" || label == "raw-data" || label == "verbatim" ||
         label == "code" || label == "math" ||
         starts (label, "equation") || starts (label, "eqnarray") ||
         starts (label, "src-");
}

tree
normalize_tree_unlocked (tree t, int& wrapped,
                         const std::vector<string>& trusted_names) {
  if (is_atomic (t))
    return normalize_atomic_unlocked (t, wrapped, trusted_names);
  if (skip_person_normalization_subtree (t)) return copy (t);

  tree result (L(t));
  for (int i=0; i<N(t); i++) {
    if (is_accessible_child (t, i))
      result << normalize_tree_unlocked (t[i], wrapped, trusted_names);
    else result << copy (t[i]);
  }
  return result;
}

void
collect_person_occurrences (tree t, path base,
                            std::vector<athena_person_occurrence>& out) {
  if (is_atomic (t)) return;
  if (is_compound (t, "person") && N(t) >= 1) {
    string name= trim_person_name (tree_as_string (t[0]));
    if (name != "") out.push_back ({name, base});
    return;
  }
  if (is_func (t, RAW_DATA)) return;
  for (int i=0; i<N(t); i++)
    collect_person_occurrences (t[i], base * i, out);
}

bool
atomic_contains_person_text (const string& text, const string& name) {
  if (name == "") return false;
  for (int start=0; start + N(name) <= N(text); start++) {
    if (!name_boundary_before (text, start)) continue;
    if (text (start, start + N(name)) != name) continue;
    if (name_boundary_after (text, start + N(name))) return true;
  }
  return false;
}

bool
tree_contains_person_text (tree t, const string& name) {
  if (is_atomic (t)) return atomic_contains_person_text (t->label, name);
  if (is_func (t, RAW_DATA)) return false;
  for (int i=0; i<N(t); i++)
    if (tree_contains_person_text (t[i], name)) return true;
  return false;
}

} // namespace

tree
athena_normalize_person_names (tree body, int& wrapped) {
  return athena_normalize_person_names (
    body, wrapped, athena_collect_person_names (body));
}

tree
athena_normalize_person_names (
  tree body, int& wrapped, const std::vector<string>& trusted_names)
{
  std::lock_guard<std::mutex> guard (dictionary_mutex);
  load_person_dictionary_unlocked ();
  std::set<string> unique;
  for (const string& raw_name: trusted_names) {
    string name= trim_person_name (raw_name);
    if (N(name) > 0 && N(name) <= 240) unique.insert (name);
  }
  std::vector<string> contextual_names (unique.begin (), unique.end ());
  wrapped= 0;
  return normalize_tree_unlocked (body, wrapped, contextual_names);
}

tree
athena_normalize_person_names_for_scheme (tree body) {
  int wrapped= 0;
  return athena_normalize_person_names (body, wrapped);
}

std::vector<athena_person_occurrence>
athena_collect_person_occurrences (tree body) {
  std::vector<athena_person_occurrence> out;
  collect_person_occurrences (body, path (), out);
  return out;
}

std::vector<string>
athena_collect_person_names (tree body) {
  std::vector<athena_person_occurrence> occurrences=
    athena_collect_person_occurrences (body);
  std::set<string> unique;
  for (const athena_person_occurrence& occurrence: occurrences)
    unique.insert (occurrence.name);
  return std::vector<string> (unique.begin (), unique.end ());
}

bool
athena_tree_contains_person_text (tree body, string name) {
  return tree_contains_person_text (body, trim_person_name (name));
}
