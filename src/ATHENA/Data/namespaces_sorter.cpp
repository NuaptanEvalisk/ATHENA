/******************************************************************************
* MODULE     : namespaces_sorter.cpp
* DESCRIPTION: Sorter loading and product sorter generation for ATHENA namespaces
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "namespaces_private.hpp"

#include "vault.hpp"

#include <libtcc.h>

#include <cstdio>
#include <ctime>
#include <functional>
#include <thread>

namespace athena_namespaces {

extern "C" int
athena_ns_strcmp (const char* a, const char* b) {
  if (a == nullptr) a= "";
  if (b == nullptr) b= "";
  return std::strcmp (a, b);
}

extern "C" int
athena_ns_strcasecmp (const char* a, const char* b) {
  if (a == nullptr) a= "";
  if (b == nullptr) b= "";
  return strcasecmp (a, b);
}

extern "C" int
athena_ns_cmp_int (long long a, long long b) {
  return (a > b) - (a < b);
}

extern "C" int
athena_ns_roman_value (const char* s) {
  return parse_roman_value (s == nullptr ? std::string_view () : std::string_view (s));
}

extern "C" int
athena_ns_cmp_roman (int a, int b) {
  return (a > b) - (a < b);
}

struct compiled_sorter {
  TCCState* state;
  ns_compare_fn fn;
  std::thread::id owner= std::this_thread::get_id ();
  compiled_sorter (TCCState* s, ns_compare_fn f): state (s), fn (f) {}
  ~compiled_sorter () { tcc_delete (state); }
};

struct sorter_cache_entry {
  sorter_handle compiled;
  std::filesystem::file_time_type mtime {};
  std::string error;
};

static thread_local std::map<std::string, sorter_cache_entry> sorter_cache;

static std::string
read_file_std (const std::string& path) {
  std::ifstream in (path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf ();
  return ss.str ();
}

static std::filesystem::file_time_type
mtime_for_path (const std::string& path) {
  std::error_code error;
  auto stamp= std::filesystem::last_write_time (path, error);
  return error ? std::filesystem::file_time_type::min () : stamp;
}

static std::string
resolve_vault_relative_path (string rel) {
  std::string p= tm_to_std (rel);
  if (p.empty ()) return "";
  std::filesystem::path fp (p);
  if (fp.is_absolute ()) return fp.string ();
  url u= vault_get_root () * url (rel);
  return tm_to_std (concretize (u));
}

static void
tcc_error_cb (void* opaque, const char* msg) {
  std::string* error= static_cast<std::string*> (opaque);
  if (!error->empty ()) *error += "\n";
  *error += msg == nullptr ? "unknown libtcc error" : msg;
}

sorter_handle
load_sorter (string sorter_path, string& error) {
  std::string path= resolve_vault_relative_path (sorter_path);
  if (path.empty ()) return nullptr;
  auto mt= mtime_for_path (path);

  sorter_cache_entry& ent= sorter_cache[path];
  if (ent.compiled && ent.mtime == mt) return ent.compiled;
  if (!ent.error.empty () && ent.mtime == mt) {
    error= std_to_tm (ent.error);
    return nullptr;
  }
  ent.compiled.reset ();
  ent.error.clear ();
  ent.mtime= mt;

  std::ifstream probe (path);
  if (!probe.good ()) {
    ent.error= "Cannot open sorter source: " + path;
    error= std_to_tm (ent.error);
    return nullptr;
  }
  std::string source= read_file_std (path);
  static const char* abi=
    "typedef enum {\n"
    "  ATHENA_NS_STRING,\n"
    "  ATHENA_NS_WORD,\n"
    "  ATHENA_NS_CHAR,\n"
    "  ATHENA_NS_INT,\n"
    "  ATHENA_NS_POS_INT,\n"
    "  ATHENA_NS_ROMAN\n"
    "} AthenaNsFieldType;\n"
    "typedef struct {\n"
    "  const char* text;\n"
    "  int type;\n"
    "  long long integer;\n"
    "  int roman;\n"
    "} AthenaNsField;\n"
    "int athena_ns_strcmp(const char*, const char*);\n"
    "int athena_ns_strcasecmp(const char*, const char*);\n"
    "int athena_ns_cmp_int(long long, long long);\n"
    "int athena_ns_cmp_roman(int, int);\n"
    "int athena_ns_roman_value(const char*);\n";

  std::string errors;
  std::unique_ptr<TCCState, decltype (&tcc_delete)> state (
    tcc_new (), &tcc_delete);
  TCCState* s= state.get ();
  if (s == nullptr) {
    ent.error= "Cannot initialize libtcc.";
    error= std_to_tm (ent.error);
    return nullptr;
  }
  tcc_set_error_func (s, &errors, tcc_error_cb);
  tcc_set_output_type (s, TCC_OUTPUT_MEMORY);
  std::string program= std::string (abi) + "\n" + source;
  if (tcc_compile_string (s, program.c_str ()) < 0) {
    ent.error= errors.empty () ? "Sorter compilation failed." : errors;
    error= std_to_tm (ent.error);
    return nullptr;
  }

  tcc_add_symbol (s, "athena_ns_strcmp", (void*) athena_ns_strcmp);
  tcc_add_symbol (s, "athena_ns_strcasecmp", (void*) athena_ns_strcasecmp);
  tcc_add_symbol (s, "athena_ns_cmp_int", (void*) athena_ns_cmp_int);
  tcc_add_symbol (s, "athena_ns_cmp_roman", (void*) athena_ns_cmp_roman);
  tcc_add_symbol (s, "athena_ns_roman_value", (void*) athena_ns_roman_value);

#ifdef TCC_RELOCATE_AUTO
  if (tcc_relocate (s, TCC_RELOCATE_AUTO) < 0) {
#else
  if (tcc_relocate (s) < 0) {
#endif
    ent.error= errors.empty () ? "Sorter relocation failed." : errors;
    error= std_to_tm (ent.error);
    return nullptr;
  }
  void* sym= tcc_get_symbol (s, "athena_ns_compare");
  if (sym == nullptr) {
    ent.error= "Sorter must define athena_ns_compare.";
    error= std_to_tm (ent.error);
    return nullptr;
  }
  tcc_set_error_func (s, nullptr, nullptr);
  ent.compiled= std::make_shared<const compiled_sorter> (
    s, reinterpret_cast<ns_compare_fn> (sym));
  state.release ();
  ent.error.clear ();
  return ent.compiled;
}

static AthenaNsField
to_c_field (const athena_namespace_match& m, int i) {
  AthenaNsField f;
  f.text= m.captures[i].c_str ();
  const string& type= m.capture_types[i];
  f.type= ATHENA_NS_STRING;
  if (type == "word") f.type= ATHENA_NS_WORD;
  else if (type == "char") f.type= ATHENA_NS_CHAR;
  else if (type == "int") f.type= ATHENA_NS_INT;
  else if (type == "positive-int") f.type= ATHENA_NS_POS_INT;
  else if (type == "roman") f.type= ATHENA_NS_ROMAN;
  f.integer= std::strtoll (f.text, nullptr, 10);
  f.roman= athena_ns_roman_value (f.text);
  return f;
}

void
sort_namespace_members (const sorter_handle& sorter,
                         namespace_records<athena_namespace_match>& members) {
  if (!sorter) return;
  ASSERT (sorter->owner == std::this_thread::get_id (),
          "namespace sorter used outside its owning thread");
  std::vector<std::vector<AthenaNsField>> fields (members.size ());
  std::vector<size_t> order (members.size ());
  std::iota (order.begin (), order.end (), 0);
  for (size_t i=0; i<members.size (); ++i) {
    const auto& member= members[i];
    ASSERT (member.captures.size () == member.capture_types.size (),
            "namespace capture types do not match captures");
    fields[i].reserve (member.captures.size ());
    for (size_t j=0; j<member.captures.size (); ++j)
      fields[i].push_back (to_c_field (member, (int) j));
  }
  // Records stay immutable and stationary while C borrows their string storage.
  std::stable_sort (order.begin (), order.end (), [&] (size_t a, size_t b) {
    int n= (int) std::min (fields[a].size (), fields[b].size ());
    return sorter->fn (n, fields[a].data (), fields[b].data ()) < 0;
  });
  members.reorder (order);
}

static tree
text (const std::string& s) {
  return tree (std_to_tm (s));
}

static tree
line (const std::string& s) {
  return compound ("paragraph*", text (s));
}

static tree
line_tm (string s) {
  return compound ("paragraph*", tree (s));
}

static tree
document_for_body (tree body) {
  tree doc (DOCUMENT);
  doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION);
  doc << compound ("style", tuple ("generic"));
  doc << compound ("body", body);
  return doc;
}

static tree
error_page (const std::string& title, const std::string& message) {
  tree body (DOCUMENT);
  body << compound ("section*", text (title));
  body << line (message);
  return document_for_body (body);
}

static std::string
c_string_escape (const std::string& s) {
  std::string out;
  for (char ch: s) {
    unsigned char c= (unsigned char) ch;
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c < 32 || c >= 127) {
      char buf[8];
      std::snprintf (buf, sizeof (buf), "\\x%02x", c);
      out += buf;
    }
    else out.push_back (ch);
  }
  return out;
}

static std::string
safe_identifier (const std::string& s) {
  std::string out;
  for (char ch: s) {
    if (std::isalnum ((unsigned char) ch)) out.push_back (ch);
    else out.push_back ('_');
  }
  if (out.empty () || std::isdigit ((unsigned char) out[0]))
    out= "_" + out;
  return out;
}

static std::string
safe_file_component (const std::string& s) {
  std::string out;
  for (char ch: s) {
    if (std::isalnum ((unsigned char) ch) || ch == '-' || ch == '_')
      out.push_back ((char) std::tolower ((unsigned char) ch));
    else if (ch == ' ') out.push_back ('-');
  }
  if (out.empty ()) out= "namespace";
  return out;
}

static std::string
replace_identifier (const std::string& source, const std::string& from,
                    const std::string& to) {
  std::string out;
  for (size_t i=0; i<source.size (); ) {
    if (source.compare (i, from.size (), from) == 0) {
      bool left= i == 0 ||
        !(std::isalnum ((unsigned char) source[i - 1]) ||
          source[i - 1] == '_');
      size_t end= i + from.size ();
      bool right= end >= source.size () ||
        !(std::isalnum ((unsigned char) source[end]) ||
          source[end] == '_');
      if (left && right) {
        out += to;
        i= end;
        continue;
      }
    }
    out.push_back (source[i++]);
  }
  return out;
}

static bool
sorter_source_for_function (const athena_namespace_definition& ns,
                            const std::string& function_name,
                            std::string& source, string& error) {
  if (ns.sorter_trivial || ns.sorter_path == "") {
    source=
      std::string ("static int\n") + function_name +
      " (int n, const AthenaNsField* a, const AthenaNsField* b) {\n"
      "  (void) n; (void) a; (void) b;\n"
      "  return 0;\n"
      "}\n";
    return true;
  }

  std::string path= resolve_vault_relative_path (ns.sorter_path);
  if (path.empty () || !std::filesystem::exists (path)) {
    error= "Cannot open sorter source: " * ns.sorter_path;
    return false;
  }
  source= replace_identifier (read_file_std (path), "athena_ns_compare",
                              function_name);
  return true;
}

static std::string
c_field_type_expr (ns_field_type type) {
  switch (type) {
  case ns_string_field: return "ATHENA_NS_STRING";
  case ns_word_field: return "ATHENA_NS_WORD";
  case ns_char_field: return "ATHENA_NS_CHAR";
  case ns_int_field: return "ATHENA_NS_INT";
  case ns_pos_int_field: return "ATHENA_NS_POS_INT";
  case ns_roman_field: return "ATHENA_NS_ROMAN";
  }
  return "ATHENA_NS_STRING";
}

static void
append_field_build_code (std::ostringstream& out, const std::string& prefix,
                         const std::string& src_name,
                         const std::vector<parent_field_expr>& fields) {
  int count= (int) fields.size ();
  out << "  AthenaNsField " << prefix << "[" << (count == 0 ? 1 : count)
      << "];\n";
  for (int i=0; i<count; i++) {
    std::string buf= prefix + "_text_" + std::to_string (i);
    out << "  char " << buf << "[512];\n";
    out << "  " << buf << "[0] = 0;\n";
    for (const field_fragment& part: fields[(size_t) i].parts) {
      if (part.child) {
        out << "  if (" << part.child_index << " < n) "
            << "athena_ns_product_append (" << buf << ", 512, "
            << src_name << "[" << part.child_index << "].text);\n";
      }
      else {
        out << "  athena_ns_product_append (" << buf << ", 512, \""
            << c_string_escape (part.literal) << "\");\n";
      }
    }
    out << "  " << prefix << "[" << i << "].text = " << buf << ";\n";
    out << "  " << prefix << "[" << i << "].type = "
        << c_field_type_expr (fields[(size_t) i].type) << ";\n";
    out << "  " << prefix << "[" << i << "].integer = "
        << "athena_ns_product_parse_int (" << buf << ");\n";
    out << "  " << prefix << "[" << i << "].roman = "
        << "athena_ns_roman_value (" << buf << ");\n";
  }
}

static std::string
product_sorter_source (const athena_namespace_definition& first,
                       const athena_namespace_definition& second,
                       const derivation_result& first_map,
                       const derivation_result& second_map,
                       string product_template, string& error) {
  std::string first_source, second_source;
  if (!sorter_source_for_function (first, "athena_ns_compare_left",
                                   first_source, error))
    return "";
  if (!sorter_source_for_function (second, "athena_ns_compare_right",
                                   second_source, error))
    return "";

  std::ostringstream out;
  out << "/*\n"
      << " * Generated ATHENA namespace product sorter.\n"
      << " * Parent 1: " << c_string_escape (tm_to_std (first.name)) << "\n"
      << " * Parent 2: " << c_string_escape (tm_to_std (second.name)) << "\n"
      << " * Product template: "
      << c_string_escape (tm_to_std (product_template)) << "\n"
      << " */\n\n";
  out << first_source << "\n" << second_source << "\n";
  out << "static void\n"
      << "athena_ns_product_append (char* out, int cap, const char* s) {\n"
      << "  int i = 0;\n"
      << "  if (cap <= 0) return;\n"
      << "  while (i + 1 < cap && out[i] != 0) i++;\n"
      << "  if (s == 0) return;\n"
      << "  while (i + 1 < cap && *s != 0) out[i++] = *s++;\n"
      << "  out[i] = 0;\n"
      << "}\n\n"
      << "static long long\n"
      << "athena_ns_product_parse_int (const char* s) {\n"
      << "  long long sign = 1, value = 0;\n"
      << "  if (s == 0) return 0;\n"
      << "  if (*s == '-') { sign = -1; s++; }\n"
      << "  while (*s >= '0' && *s <= '9') {\n"
      << "    value = value * 10 + (*s - '0');\n"
      << "    s++;\n"
      << "  }\n"
      << "  return sign * value;\n"
      << "}\n\n";
  out << "int\n"
      << "athena_ns_compare (int n, const AthenaNsField* a, "
      << "const AthenaNsField* b) {\n";
  append_field_build_code (out, "left_a", "a", first_map.fields);
  append_field_build_code (out, "left_b", "b", first_map.fields);
  append_field_build_code (out, "right_a", "a", second_map.fields);
  append_field_build_code (out, "right_b", "b", second_map.fields);
  out << "  int c1 = athena_ns_compare_left ("
      << first_map.fields.size () << ", left_a, left_b);\n"
      << "  int c2 = athena_ns_compare_right ("
      << second_map.fields.size () << ", right_a, right_b);\n"
      << "  if (c1 < 0 || c2 < 0) return -1;\n"
      << "  if (c1 > 0 || c2 > 0) return 1;\n"
      << "  return 0;\n"
      << "}\n";
  return out.str ();
}

static std::string
restricted_sorter_source (const athena_namespace_definition& parent,
                          const derivation_result& parent_map,
                          string product_template, string& error) {
  std::string parent_source;
  if (!sorter_source_for_function (parent, "athena_ns_compare_parent",
                                   parent_source, error))
    return "";

  std::ostringstream out;
  out << "/*\n"
      << " * Generated ATHENA namespace restricted sorter.\n"
      << " * Parent: " << c_string_escape (tm_to_std (parent.name)) << "\n"
      << " * Product template: "
      << c_string_escape (tm_to_std (product_template)) << "\n"
      << " */\n\n";
  out << parent_source << "\n";
  out << "static void\n"
      << "athena_ns_product_append (char* out, int cap, const char* s) {\n"
      << "  int i = 0;\n"
      << "  if (cap <= 0) return;\n"
      << "  while (i + 1 < cap && out[i] != 0) i++;\n"
      << "  if (s == 0) return;\n"
      << "  while (i + 1 < cap && *s != 0) out[i++] = *s++;\n"
      << "  out[i] = 0;\n"
      << "}\n\n"
      << "static long long\n"
      << "athena_ns_product_parse_int (const char* s) {\n"
      << "  long long sign = 1, value = 0;\n"
      << "  if (s == 0) return 0;\n"
      << "  if (*s == '-') { sign = -1; s++; }\n"
      << "  while (*s >= '0' && *s <= '9') {\n"
      << "    value = value * 10 + (*s - '0');\n"
      << "    s++;\n"
      << "  }\n"
      << "  return sign * value;\n"
      << "}\n\n";
  out << "int\n"
      << "athena_ns_compare (int n, const AthenaNsField* a, "
      << "const AthenaNsField* b) {\n";
  append_field_build_code (out, "parent_a", "a", parent_map.fields);
  append_field_build_code (out, "parent_b", "b", parent_map.fields);
  out << "  return athena_ns_compare_parent ("
      << parent_map.fields.size () << ", parent_a, parent_b);\n"
      << "}\n";
  return out.str ();
}



} // namespace athena_namespaces

using namespace athena_namespaces;

bool
athena_namespace_sorter_source (const athena_namespace_definition& ns,
                                string& source, string& error) {
  if (ns.sorter_trivial || ns.sorter_path == "") {
    source= "Built-in trivial sorter: every pair of files compares equal.";
    return true;
  }
  std::string path= resolve_vault_relative_path (ns.sorter_path);
  if (path.empty () || !std::filesystem::exists (path)) {
    error= "Cannot open sorter source: " * ns.sorter_path;
    return false;
  }
  source= std_to_tm (read_file_std (path));
  return true;
}

bool
athena_namespace_generate_product_sorter (
  const athena_namespace_definition& first,
  const athena_namespace_definition& second,
  string product_template, string& sorter_path, string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if ((!first.sorter_trivial && first.sorter_path == "") ||
      (!second.sorter_trivial && second.sorter_path == "")) {
    error= "Both product parents need explicit or trivial sorters.";
    return false;
  }

  derivation_result first_map, second_map;
  if (!template_derivation_mapping (product_template, first.templ, false,
                                    first_map, error)) {
    if (error == "") error= "Product template does not derive from " *
                            first.name * ".";
    return false;
  }
  if (!template_derivation_mapping (product_template, second.templ, false,
                                    second_map, error)) {
    if (error == "") error= "Product template does not derive from " *
                            second.name * ".";
    return false;
  }

  std::string source= product_sorter_source (first, second, first_map,
                                             second_map, product_template,
                                             error);
  if (source.empty ()) return false;

  std::filesystem::path root (tm_to_std (concretize (vault_get_root ())));
  std::filesystem::path dir= root / ".athena" / "ns-sorters";
  std::error_code ec;
  std::filesystem::create_directories (dir, ec);
  if (ec) {
    error= "Cannot create namespace sorter directory: " *
           std_to_tm (ec.message ());
    return false;
  }

  std::string stem= "product-" + safe_file_component (tm_to_std (first.name)) +
                    "-" + safe_file_component (tm_to_std (second.name)) +
                    "-" + std::to_string ((long long) std::time (nullptr));
  std::filesystem::path file;
  for (int i=0; i<1000; i++) {
    std::string suffix= i == 0 ? "" : "-" + std::to_string (i);
    file= dir / (stem + suffix + ".c");
    if (!std::filesystem::exists (file)) break;
  }

  std::ofstream out (file, std::ios::binary);
  if (!out.good ()) {
    error= "Cannot write generated product sorter.";
    return false;
  }
  out << source;
  out.close ();

  std::filesystem::path rel= std::filesystem::relative (file, root, ec);
  sorter_path= std_to_tm (ec ? file.string () : rel.generic_string ());

  string compile_error;
  (void) load_sorter (sorter_path, compile_error);
  if (compile_error != "") {
    std::filesystem::remove (file, ec);
    error= "Generated product sorter did not compile: " * compile_error;
    return false;
  }
  return true;
}

bool
athena_namespace_generate_restricted_sorter (
  const athena_namespace_definition& parent,
  string product_template, string& sorter_path, string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (!parent.sorter_trivial && parent.sorter_path == "") {
    error= "The semi-concrete parent needs an explicit or trivial sorter.";
    return false;
  }

  derivation_result parent_map;
  if (!template_derivation_mapping (product_template, parent.templ, false,
                                    parent_map, error)) {
    if (error == "") error= "Product template does not derive from " *
                            parent.name * ".";
    return false;
  }

  std::string source= restricted_sorter_source (parent, parent_map,
                                                product_template, error);
  if (source.empty ()) return false;

  std::filesystem::path root (tm_to_std (concretize (vault_get_root ())));
  std::filesystem::path dir= root / ".athena" / "ns-sorters";
  std::error_code ec;
  std::filesystem::create_directories (dir, ec);
  if (ec) {
    error= "Cannot create namespace sorter directory: " *
           std_to_tm (ec.message ());
    return false;
  }

  std::string stem= "restricted-" + safe_file_component (tm_to_std (parent.name)) +
                    "-" + std::to_string ((long long) std::time (nullptr));
  std::filesystem::path file;
  for (int i=0; i<1000; i++) {
    std::string suffix= i == 0 ? "" : "-" + std::to_string (i);
    file= dir / (stem + suffix + ".c");
    if (!std::filesystem::exists (file)) break;
  }

  std::ofstream out (file, std::ios::binary);
  if (!out.good ()) {
    error= "Cannot write generated restricted sorter.";
    return false;
  }
  out << source;
  out.close ();

  std::filesystem::path rel= std::filesystem::relative (file, root, ec);
  sorter_path= std_to_tm (ec ? file.string () : rel.generic_string ());

  string compile_error;
  (void) load_sorter (sorter_path, compile_error);
  if (compile_error != "") {
    std::filesystem::remove (file, ec);
    error= "Generated restricted sorter did not compile: " * compile_error;
    return false;
  }
  return true;
}
