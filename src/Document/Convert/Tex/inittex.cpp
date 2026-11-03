
/******************************************************************************
* MODULE     : inittex.cpp
* DESCRIPTION: initialize conversion from and to TeX
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Tex/convert_tex.hpp"
#include "rel_hashmap.hpp"

#include <iostream>
#include <unordered_map>
#include <string>

#include "tm_ostream.hpp"

static string
paper_opts_func (string s) {
  return latex_native_paper_opts (s);
}

static string
paper_type_func (string s) {
  return latex_native_paper_type (s);
}

extern bool aofm_converter_mode;

static std::unordered_map<std::string, std::string> aofm_type_cache;
static std::unordered_map<std::string, int> aofm_arity_cache;

static void
print_latex_cache_progress (int current, int total) {
  int bar_width= 30;
  float progress= (total > 0)? ((float) current / (float) total): 1.0f;
  int pos= (int) (bar_width * progress);

  std::cout << "\r[";
  for (int i= 0; i < bar_width; ++i) {
    if (i < pos) std::cout << "=";
    else if (i == pos && current < total) std::cout << ">";
    else std::cout << " ";
  }

  std::cout << "] " << (int) (progress * 100.0f) << "% "
            << "[" << current << "/" << total << "] "
            << "Caching LaTeX command dictionary" << std::flush;
}

void aofm_cache_latex_commands() {
  array<string> tags= latex_native_tags ();
  int total = N(tags);
  int chunk_size = 50;
  aofm_type_cache.clear ();
  aofm_arity_cache.clear ();
  for (int i = 0; i < total; i += chunk_size) {
    int end = (i + chunk_size > total) ? total : (i + chunk_size);
    print_latex_cache_progress (end, total);
    for (int j=i; j<end; ++j) {
      std::string cmd= as_charp (tags[j]);
      aofm_type_cache[cmd]= as_charp (latex_native_type (tags[j]));
      aofm_arity_cache[cmd]= latex_native_arity (tags[j]);
    }
  }
  std::cout << std::endl;
  cout << "AOFM] Cached " << (int) aofm_type_cache.size()
       << " LaTeX commands." << LF;
}

static string
latex_type_func (string s) {
  if (aofm_converter_mode) {
    std::string norm_s = as_charp(s);
    
    // Replicate Scheme `latex-resolve` normalization
    if (!norm_s.empty() && norm_s[0] == '\\') {
      norm_s = norm_s.substr(1);
    }
    if (norm_s.compare(0, 4, "end-") == 0) {
      norm_s = "begin-" + norm_s.substr(4);
    }

    auto it = aofm_type_cache.find(norm_s);
    if (it != aofm_type_cache.end()) {
      return string(it->second.c_str());
    }

    // Cache miss means the command is not in the immutable native database.
    return "undefined";
  }
  return latex_native_type (s);
}

static int
latex_arity_func (string s) {
  if (aofm_converter_mode) {
    std::string norm_s = as_charp(s);
    
    // Replicate Scheme `latex-resolve` normalization
    if (!norm_s.empty() && norm_s[0] == '\\') {
      norm_s = norm_s.substr(1);
    }
    
    bool was_end = false;
    if (norm_s.compare(0, 4, "end-") == 0) {
      norm_s = "begin-" + norm_s.substr(4);
      was_end = true;
    }

    if (was_end) {
      return 0; // `end-` tags always have 0 arity according to latex-resolve
    }

    auto it = aofm_arity_cache.find(norm_s);
    if (it != aofm_arity_cache.end()) {
      return it->second;
    }

    // Cache miss means the command is not in the immutable native database.
    return 0;
  }
  return latex_native_arity (s);
}

hashfunc<string,string>    paper_std_opts (paper_opts_func, "undefined");
hashfunc<string,string>    paper_std_type (paper_type_func, "undefined");
hashfunc<string,string>    latex_std_type (latex_type_func, "undefined");
hashfunc<string,int>       latex_std_arity (latex_arity_func, 0);

static array<string> empty_array_string;

rel_hashmap<string,string> command_type ("undefined");
rel_hashmap<string,int>    command_arity (0);
rel_hashmap<string,array<string> > command_def (empty_array_string);

string
paper_opts (string s) {
  return paper_std_opts [s];
}

string
paper_type (string s) {
  return paper_std_type [s];
}

string
latex_type (string s) {
  if (command_type->contains (s)) return command_type[s];
  else return latex_std_type [s];
}

int
latex_arity (string s) {
  if (command_arity->contains (s)) return command_arity[s];
  else return latex_std_arity [s];
}
