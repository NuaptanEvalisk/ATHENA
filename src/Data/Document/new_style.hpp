
/******************************************************************************			
* MODULE     : new_style.hpp
* DESCRIPTION: Style and DRD computation and caching
* COPYRIGHT  : (C) 1999-2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef NEW_STYLE_H
#define NEW_STYLE_H
#include "Data/NodeContracts/execution_state.hpp"
#include "scheme.hpp"
#include "Data/NodeContracts/evaluation_inputs.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

tree preprocess_style (tree st, url name);

// Native ATHENA styles/packages use .ats and the same UTF-8 XML document
// codec as .ath documents. Legacy .ts remains a read-only compatibility input.
url  resolve_style_file (string package, url search_path,
                         bool allow_legacy= true);
tree load_style_document (url name);
tree load_style_body (url name);
bool convert_legacy_style_file (const std::filesystem::path& source,
                                const std::filesystem::path& target,
                                std::string& error);
bool install_style_file (const std::filesystem::path& source,
                         const std::filesystem::path& target,
                         std::string& error);

void style_invalidate_cache ();
std::uint64_t style_cache_generation ();
class edit_env_rep;
class evaluation_environment_checkpoint;

// Owner-thread package state. Values and evaluated bindings are restored as
// one unit; a package snapshot never owns a live analyzer or editor context.
class style_environment {
  hashmap<string,tree> values;
  athena::contracts::evaluation_input_snapshot inputs;
  athena::contracts::contract_snapshot contracts;
public:
  explicit style_environment (edit_env_rep&);
  void restore (edit_env_rep&) const;
  athena::contracts::evaluation_state context () const;
};
using style_environment_ptr= std::shared_ptr<const style_environment>;
style_environment_ptr get_style_environment (tree style, url source_name,
                                             bool scheme_modules);
tree get_document_preamble (tree t);
athena::contracts::evaluation_state prepare_document_context (tree doc);
// Isolated source-entry role preparation for detached document identity work.
// The document preamble is prepared once; each requested source position is
// evaluated in its own scope and never publishes references or auxiliary data.
using document_source_contract_preparation= std::function<
  std::shared_ptr<const athena::contracts::contract_scope> (const std::vector<int>&)>;
using document_source_environment_preparation= std::function<
  std::shared_ptr<const evaluation_environment_checkpoint> (const std::vector<int>&)>;
struct source_contract_maps {
  hashmap<string,tree> refs, global_refs, aux, global_aux;
  hashmap<string,tree> attachments, global_attachments;
  bool shared_aux= false, shared_attachments= false;
};
// One source-owner operation prepares its baseline only on first custom macro
// and reuses increasing paragraph entries. No cache escapes that operation.
document_source_contract_preparation prepare_source_contract_positions (
  tree body, url source_name,
  std::function<std::shared_ptr<const evaluation_environment_checkpoint> ()> baseline,
  std::function<source_contract_maps ()> inputs= {});
document_source_contract_preparation prepare_document_source_contracts (
  tree doc, url source_name= url ("$PWD/none"));
document_source_contract_preparation prepare_offline_document_source_contracts (
  tree doc, url source_name);
document_source_environment_preparation prepare_document_source_environments (
  tree doc, url source_name= url ("$PWD/none"));

array<string> get_style_names ();
array<string> get_package_names ();
bool   hidden_package (string name);

#endif // NEW_STYLE_H
