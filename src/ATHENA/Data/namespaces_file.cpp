/******************************************************************************
* MODULE     : namespaces_file.cpp
* DESCRIPTION: File creation helpers for ATHENA namespaces
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "namespaces_private.hpp"

#include "boot.hpp"
#include "file.hpp"
#include "new_style.hpp"
#include "vault.hpp"

using namespace athena_namespaces;

namespace {

const char*
namespace_field_placeholder (ns_field_type type) {
  switch (type) {
  case ns_string_field: return "%s";
  case ns_word_field: return "%w";
  case ns_char_field: return "%c";
  case ns_int_field: return "%d";
  case ns_pos_int_field: return "%N";
  case ns_roman_field: return "%R";
  }
  return "%s";
}

std::filesystem::path
namespace_resolve_path (string p, string base_root) {
  std::string raw= tm_to_std (p);
  if (raw.empty ()) return std::filesystem::path ();
  std::filesystem::path path (raw);
  if (path.is_absolute ()) return path;

  std::string base= tm_to_std (base_root);
  if (!base.empty ()) {
    std::filesystem::path candidate= std::filesystem::path (base) / path;
    if (std::filesystem::exists (candidate)) return candidate;
  }

  if (vault_active ()) {
    std::filesystem::path root (
      tm_to_std (as_unix_string (concretize (vault_get_root ()))));
    std::filesystem::path candidate= root / path;
    if (std::filesystem::exists (candidate)) return candidate;
    return candidate;
  }

  return path;
}

bool
namespace_install_style (string style_path, string base_root,
                         string& style_name, string& error) {
  style_name= "";
  if (style_path == "") return true;

  std::filesystem::path source= namespace_resolve_path (style_path, base_root);
  if (source.empty () || !std::filesystem::exists (source)) {
    error= "Namespace style file does not exist: " * style_path;
    return false;
  }
  if (source.extension () != ".ts") {
    error= "Namespace style file must end in .ts: " * style_path;
    return false;
  }

  string home_s= get_env ("ATHENA_HOME_PATH");
  if (home_s == "") {
    error= "ATHENA_HOME_PATH is not set.";
    return false;
  }

  std::filesystem::path styles_dir= std::filesystem::path (tm_to_std (home_s)) /
                                    "styles";
  std::error_code ec;
  std::filesystem::create_directories (styles_dir, ec);
  if (ec) {
    error= "Could not create custom styles directory: " *
           std_to_tm (styles_dir.string ());
    return false;
  }

  std::filesystem::path target= styles_dir / source.filename ();
  if (!std::filesystem::exists (target) ||
      std::filesystem::absolute (source) != std::filesystem::absolute (target)) {
    std::filesystem::copy_file (
      source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      error= "Could not install namespace style: " *
             std_to_tm (ec.message ());
      return false;
    }
  }

  style_invalidate_cache ();
  style_name= std_to_tm (source.stem ().string ());
  return true;
}

tree
namespace_empty_document () {
  tree doc (DOCUMENT);
  doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION);
  doc << compound ("style", tuple ("generic"));
  doc << compound ("body", tree (DOCUMENT, ""));
  return doc;
}

bool
namespace_load_initial_document (const athena_namespace_definition& ns,
                                 string base_root, tree& doc, string& error) {
  if (ns.initial_content_path == "") {
    doc= namespace_empty_document ();
    return true;
  }

  std::filesystem::path source=
    namespace_resolve_path (ns.initial_content_path, base_root);
  if (source.empty () || !std::filesystem::exists (source)) {
    error= "Namespace initial content file does not exist: " *
           ns.initial_content_path;
    return false;
  }

  string text;
  if (load_string (url_system (std_to_tm (source.string ())), text, false)) {
    error= "Could not read namespace initial content: " *
           ns.initial_content_path;
    return false;
  }
  doc= texmacs_document_to_tree (text);
  return true;
}

} // namespace

bool
athena_namespace_match_stem (const athena_namespace_definition& ns,
                             string stem, athena_namespace_match& match,
                             string& error) {
  return match_stem (ns, tm_to_std (stem), match, error);
}

std::vector<athena_namespace_definition>
athena_namespace_concrete_matches_stem (string stem, string& error) {
  std::vector<athena_namespace_definition> out;
  std::string s= tm_to_std (stem);
  std::vector<athena_namespace_definition> all= athena_namespaces_list ();
  for (const athena_namespace_definition& ns: all) {
    if (canonical_kind (ns.kind) != "concrete") continue;
    athena_namespace_match m;
    string local_error;
    if (match_stem (ns, s, m, local_error))
      out.push_back (ns);
    else if (local_error != "" && error == "")
      error= local_error;
  }
  return out;
}

std::vector<athena_namespace_template_field>
athena_namespace_template_fields (const athena_namespace_definition& ns,
                                  string& error) {
  std::vector<athena_namespace_template_field> out;
  std::vector<template_token> toks;
  if (!parse_template (ns.templ, toks, error)) return out;
  for (const template_token& tok: toks) {
    if (!tok.field) continue;
    athena_namespace_template_field field;
    switch (tok.type) {
    case ns_string_field: field.placeholder= "%s"; break;
    case ns_word_field: field.placeholder= "%w"; break;
    case ns_char_field: field.placeholder= "%c"; break;
    case ns_int_field: field.placeholder= "%d"; break;
    case ns_pos_int_field: field.placeholder= "%N"; break;
    case ns_roman_field: field.placeholder= "%R"; break;
    }
    field.type= std_to_tm (field_type_name (tok.type));
    out.push_back (field);
  }
  return out;
}

bool
athena_namespace_build_stem (const athena_namespace_definition& ns,
                             const array<string>& values, string& stem,
                             string& error) {
  std::vector<template_token> toks;
  if (!parse_template (ns.templ, toks, error)) return false;

  int field_count= 0;
  for (const template_token& tok: toks)
    if (tok.field) field_count++;
  if (field_count != N(values)) {
    error= "Expected " * as_string (field_count) *
           " template field value(s), got " * as_string (N(values)) * ".";
    return false;
  }

  std::string out;
  int value_i= 0;
  for (const template_token& tok: toks) {
    if (!tok.field) out += tok.literal;
    else {
      std::string value= tm_to_std (values[value_i++]);
      if (!field_value_satisfies_type (tok.type, value)) {
        error= std_to_tm (
          "Template field " + std::string (namespace_field_placeholder (
                                tok.type)) +
          " (" + field_type_name (tok.type) + ") does not accept \"" +
          value + "\".");
        return false;
      }
      out += value;
    }
  }
  stem= std_to_tm (out);
  return true;
}

tree
athena_namespace_apply_style_to_tree (tree doc,
                                      const athena_namespace_definition& ns,
                                      string base_root, string& error) {
  if (ns.style_path == "") return doc;
  string style_name;
  if (!namespace_install_style (ns.style_path, base_root, style_name, error))
    return doc;
  if (style_name == "") return doc;
  return change_doc_attr (doc, "style", tuple (style_name));
}

bool
athena_namespace_create_file (const athena_namespace_definition& ns, url target,
                              string base_root, bool use_initial_content,
                              string& error) {
  if (exists (target)) {
    error= "Target file already exists: " * as_string (target);
    return false;
  }

  tree doc;
  if (use_initial_content)
    if (!namespace_load_initial_document (ns, base_root, doc, error))
      return false;
  if (!use_initial_content)
    doc= namespace_empty_document ();

  doc= athena_namespace_apply_style_to_tree (doc, ns, base_root, error);
  if (error != "") return false;

  if (save_string (target, tree_to_texmacs (doc))) {
    error= "Could not write target file: " * as_string (target);
    return false;
  }
  return true;
}

bool
athena_namespace_create_plain_file (url target, string& error) {
  if (exists (target)) {
    error= "Target file already exists: " * as_string (target);
    return false;
  }
  if (save_string (target, tree_to_texmacs (namespace_empty_document ()))) {
    error= "Could not write target file: " * as_string (target);
    return false;
  }
  return true;
}
