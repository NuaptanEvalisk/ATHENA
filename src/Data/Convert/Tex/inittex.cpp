
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
#include "scheme.hpp"

#include <iostream>
#include <unordered_map>
#include <string>

#include "tm_ostream.hpp"

static string
paper_opts_func (string s) {
  return as_string (call ("latex-paper-opts", s));
}

static string
paper_type_func (string s) {
  return as_string (call ("latex-paper-type", s));
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
  // 1. Fetch all LaTeX command names from the database
  // This query is usually very fast compared to property lookups.
  string get_tags_code = 
    "(begin "
    "  (use-modules (convert latex latex-drd)) "
    "  (map (lambda (row) "
    "         (let ((tag (cdar row))) "
    "           (if (symbol? tag) (symbol->string tag) tag))) "
    "       (query '(latex-tag% 'x))))";
  
  object tags_result = eval(get_tags_code);
  if (!is_list(tags_result)) {
    std::cout << "AOFM] Failed to fetch LaTeX command tags." << std::endl;
    return;
  }
  
  array<object> tags = as_array_object(tags_result);
  int total = N(tags);
  int chunk_size = 50;

  // Ensure modules are loaded at the top level
  eval("(use-modules (convert latex latex-drd))");

  // Resolve the resolver once
  object resolver = eval("(lambda (l) (map (lambda (s) (list s (latex-type s) (latex-arity s))) l))");
  
  for (int i = 0; i < total; i += chunk_size) {
    int end = (i + chunk_size > total) ? total : (i + chunk_size);
    print_latex_cache_progress (end, total);

    // Build the chunk list in C++ to avoid string escaping issues
    object chunk_list = null_object();
    for (int j = end - 1; j >= i; --j) {
      chunk_list = cons(tags[j], chunk_list);
    }

    object batch_result = call(resolver, chunk_list);
    if (is_list(batch_result)) {
      array<object> rows = as_array_object(batch_result);
      for (int k = 0; k < N(rows); ++k) {
        if (is_list(rows[k])) {
          array<object> row = as_array_object(rows[k]);
          if (N(row) >= 3) {
            std::string cmd = as_charp(as_string(row[0]));
            std::string type = as_charp(as_string(row[1]));
            int arity = as_int(row[2]);
            aofm_type_cache[cmd] = type;
            aofm_arity_cache[cmd] = arity;
          }
        }
      }
    }
  }
  std::cout << std::endl;
  std::cout << "AOFM] Cached " << aofm_type_cache.size() << " LaTeX commands." << std::endl;
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

    // Cache miss means the command is definitely not in the Scheme database
    return "undefined";
  }
  return as_string (call ("latex-type", s));
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

    // Cache miss means the command is definitely not in the Scheme database
    return 0;
  }
  return as_int (call ("latex-arity", s));
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
