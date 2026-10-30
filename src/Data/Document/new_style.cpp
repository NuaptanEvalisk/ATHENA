
/******************************************************************************
* MODULE     : new_style.cpp
* DESCRIPTION: Style and DRD computation and caching
* COPYRIGHT  : (C) 1999-2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "new_style.hpp"
#include "Data/NodeContracts/source_environment.hpp"
#include "file.hpp"
#include "data_cache.hpp"
#include "convert.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "../../Typeset/env.hpp"
#include "Env/local_environment.hpp"
#include <QFileInfo>
#include <QSaveFile>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

/******************************************************************************
* Global data
******************************************************************************/

struct style_data_rep {
  hashmap<string,bool> style_busy;
  hashmap<tree,style_environment_ptr> styles;

  style_data_rep ():
    style_busy (false),
    styles (nullptr) {}
};

static std::atomic<std::uint64_t> style_generation {1};
static thread_local style_data_rep* sd= NULL;
static thread_local std::uint64_t local_style_generation= 0;
thread_local hashmap<string,bool> hidden_packages (false);

static void
init_style_data () {
  std::uint64_t generation= style_generation.load (std::memory_order_acquire);
  if (sd != NULL && local_style_generation != generation) {
    tm_delete<style_data_rep> (sd);
    sd= NULL;
    hidden_packages= hashmap<string,bool> (false);
  }
  if (sd == NULL) sd= tm_new<style_data_rep> ();
  local_style_generation= generation;
}

extern thread_local hashmap<string,tree> style_tree_cache;

/******************************************************************************
* Native style files
******************************************************************************/

namespace {

bool
has_style_extension (string name) {
  return ends (name, ".ats") || ends (name, ".ts");
}

url
resolve_style_candidate (url search_path, string name) {
  url direct= resolve (url_system (name));
  if (!is_none (direct)) return direct;
  return resolve (search_path * name);
}

tree
style_error (string message) {
  return tree (_ERROR, message);
}

} // namespace

url
resolve_style_file (string package, url search_path, bool allow_legacy) {
  if (has_style_extension (package)) {
    if (ends (package, ".ts") && !allow_legacy) return url_none ();
    return resolve_style_candidate (search_path, package);
  }

  url native= resolve (search_path * (package * ".ats"));
  if (is_none (native)) native= resolve (url_system (package * ".ats"));
  if (!is_none (native)) return native;
  if (!allow_legacy) return url_none ();
  url legacy= resolve (search_path * (package * ".ts"));
  if (is_none (legacy)) legacy= resolve (url_system (package * ".ts"));
  return legacy;
}

tree
load_style_document (url name) {
  string source;
  if (load_string (name, source, false))
    return style_error ("Could not read style file " * as_string (name));
  try {
    std::string_view bytes (as_charp (source), (std::size_t) N(source));
    tree doc;
    if (suffix (name) == "ats")
      doc= athena::document::read_xml (
        bytes, athena::document::xml_kind::document);
    else if (suffix (name) == "ts") {
      tree legacy= texmacs_document_to_tree (source);
      if (is_func (legacy, _ERROR)) return legacy;
      athena::document::legacy_import_context context;
      string local= concretize (name);
      if (N(local) != 0)
        context.source_path= std::filesystem::path (as_charp (local));
      context.source_utf8= athena::text::valid_utf8 (bytes);
      doc= athena::document::import_legacy_document (
        legacy, athena::document::standard_legacy_cork_table (), {}, {}, context).document;
    }
    else return style_error ("Unsupported style file extension in " * as_string (name));
    if (!is_func (doc, DOCUMENT))
      return style_error ("Style file is not a document: " * as_string (name));
    return doc;
  }
  catch (const std::exception& e) {
    return style_error ("Style parse error in " * as_string (name) * ": " *
                        string (e.what ()));
  }
}

tree
load_style_body (url name) {
  tree doc= load_style_document (name);
  if (is_func (doc, _ERROR)) return doc;
  return extract (doc, "body");
}

namespace {

bool
write_native_style_file (const tree& doc, const std::filesystem::path& target,
                         std::string& error) {
  namespace fs= std::filesystem;
  try {
    const std::string xml= athena::document::write_xml (
      doc, athena::document::xml_kind::document);
    tree roundtrip= athena::document::read_xml (
      xml, athena::document::xml_kind::document);
    if (roundtrip != doc) {
      error= "native XML round-trip changed the style tree";
      return false;
    }
    std::error_code ec;
    if (!target.parent_path ().empty ())
      fs::create_directories (target.parent_path (), ec);
    if (ec) {
      error= "could not create target directory: " + ec.message ();
      return false;
    }
    QSaveFile output (QString::fromStdString (target.string ()));
    if (!output.open (QIODevice::WriteOnly)) {
      error= "could not open target style";
      return false;
    }
    const QByteArray data (xml.data (), (qsizetype) xml.size ());
    if (output.write (data) != data.size () || !output.commit ()) {
      error= "could not atomically write target style";
      return false;
    }
    return true;
  }
  catch (const std::exception& e) {
    error= e.what ();
    return false;
  }
}

} // namespace

bool
install_style_file (const std::filesystem::path& source,
                    const std::filesystem::path& target,
                    std::string& error) {
  namespace fs= std::filesystem;
  if (source.extension () != ".ats" && source.extension () != ".ts") {
    error= "source style must end in .ats or .ts";
    return false;
  }
  if (target.extension () != ".ats") {
    error= "target style must end in .ats";
    return false;
  }
  std::error_code ec;
  fs::path input= fs::absolute (source, ec);
  if (ec || !fs::exists (input) || !fs::is_regular_file (input)) {
    error= "source style does not exist";
    return false;
  }
  tree doc= load_style_document (url_system (string (input.string ().c_str ())));
  if (is_func (doc, _ERROR)) {
    error= std::string (doc[0]->label.data (), (std::size_t) N(doc[0]->label));
    return false;
  }
  return write_native_style_file (doc, target, error);
}

bool
convert_legacy_style_file (const std::filesystem::path& source,
                           const std::filesystem::path& target,
                           std::string& error) {
  namespace fs= std::filesystem;
  try {
    if (source.extension () != ".ts") {
      error= "source style must end in .ts";
      return false;
    }
    if (target.extension () != ".ats") {
      error= "target style must end in .ats";
      return false;
    }
    if (!fs::exists (source) || !fs::is_regular_file (source)) {
      error= "source style does not exist";
      return false;
    }
    if (fs::exists (target)) {
      error= "target style already exists";
      return false;
    }

    athena::contracts::initialize_builtins ();
    return install_style_file (source, target, error);
  }
  catch (const std::exception& e) {
    error= e.what ();
    return false;
  }
}

/******************************************************************************
* Modify style so as to search in all ancestor directories
******************************************************************************/

tree
preprocess_style (tree st, url name) {
  if (is_rooted_tmfs (name)) return st;
  if (is_atomic (st)) st= tree (TUPLE, st);
  if (!is_tuple (st)) return st;
  //if (is_rooted_web (name)) return st;
  bool remote= is_rooted_web (name);
  tree r (TUPLE, N(st));
  for (int i=0; i<N(st); i++) {
    r[i]= st[i];
    if (!is_atomic (st[i])) continue;
    string pack= st[i]->label;
    url global= resolve_style_file (pack, "$ATHENA_STYLE_PATH", true);
    if (!remote || is_none (global)) {
      url local_path= expand (head (name) * url_ancestor ());
      url stf= resolve_style_file (pack, local_path, true);
      if (!is_none (stf)) r[i]= as_string (stf);
    }
  }
  //if (r != st) cout << "old: " << st << "\nnew: " << r << "\n";
  return r;
}

/******************************************************************************
* Owner-thread evaluated package checkpoints
******************************************************************************/

style_environment::style_environment (edit_env_rep& env):
  inputs (env.inputs.snapshot ()), contracts (env.contract_state.contracts.snapshot ()) {
  env.read_env (values);
}

void
style_environment::restore (edit_env_rep& env) const {
  if (env.contract_events)
    env.contract_events->record (athena::contracts::evaluation_barrier {});
  env.env= copy (values);
  env.inputs.reset (inputs);
  env.contract_state.contracts= athena::contracts::contract_scope (contracts);
  if (env.recorded_contexts) env.recorded_contexts->rebase ();
}

athena::contracts::evaluation_state
style_environment::context () const {
  athena::contracts::evaluation_state result (contracts);
  result.values= copy (values);
  return result;
}

void
style_invalidate_cache () {
  std::uint64_t generation=
    style_generation.fetch_add (1, std::memory_order_acq_rel) + 1;
  style_tree_cache= hashmap<string,tree> ();
  hidden_packages= hashmap<string,bool> (false);
  if (sd != NULL) {
    tm_delete<style_data_rep> (sd);
    sd= NULL;
  }
  init_style_data ();
  local_style_generation= generation;
}

std::uint64_t
style_cache_generation () {
  return style_generation.load (std::memory_order_acquire);
}

/******************************************************************************
* Get environment and contract_state of style files
******************************************************************************/

style_environment_ptr
get_style_environment (tree style, url source_name, bool scheme_modules) {
  init_style_data ();
  athena::contracts::initialize_builtins ();
  ASSERT (is_tuple (style), "style tuple expected");
  tree key (TUPLE, copy (style), as_string (head (source_name)),
            as_string_bool (scheme_modules));
  if (sd->styles->contains (key)) return sd->styles[key];
  bool busy= false;
  for (int i=0; i<N(style); i++)
    busy= busy || sd->style_busy->contains (as_string (style[i]));
  struct busy_scope {
    hashmap<string,bool> previous;
    busy_scope (): previous (copy (sd->style_busy)) {}
    ~busy_scope () { sd->style_busy= previous; }
  } active;
  for (int i=0; i<N(style); i++)
    sd->style_busy (as_string (style[i]))= true;

  athena::contracts::evaluation_state contract_state (athena::contracts::builtin_contracts ().snapshot ());
  hashmap<string,tree> lref;
  hashmap<string,tree> gref;
  hashmap<string,tree> laux;
  hashmap<string,tree> gaux;
  hashmap<string,tree> latt;
  hashmap<string,tree> gatt;
  edit_env env (contract_state, source_name, lref, gref, laux, gaux, latt, gatt);
  env->set_scheme_modules_enabled (scheme_modules);
  if (!busy) {
    env->exec (tree (USE_PACKAGE, A (style)));
    env->analyze_contracts ();
  }
  auto checkpoint= std::make_shared<const style_environment> (*env.operator-> ());
  if (!busy) sd->styles(key)= checkpoint;
  return checkpoint;
}

tree
get_document_preamble (tree t) {
  init_style_data ();
  if (is_atomic (t)) return "";
  else if (is_compound (t, "hide-preamble", 1)) return t[0];
  else if (is_compound (t, "show-preamble", 1)) return t[0];
  else {
    int i, n= N(t);
    for (i=0; i<n; i++) {
      tree p= get_document_preamble (t[i]);
      if (p != "") return p;
    }
    return "";
  }
}

template<typename Result, typename Capture>
static std::function<std::shared_ptr<const Result> (const std::vector<int>&)>
prepare_source_positions (
    tree body, url source_name,
    std::function<std::shared_ptr<const evaluation_environment_checkpoint> ()> prepare_baseline,
    std::function<source_contract_maps ()> prepare_maps, Capture capture) {
  auto baseline= std::make_shared<std::shared_ptr<const evaluation_environment_checkpoint>> ();
  auto original_maps= std::make_shared<source_contract_maps> ();
  struct prefix_state {
    int paragraph= 0;
    std::shared_ptr<const evaluation_environment_checkpoint> entry;
    source_contract_maps maps;
  };
  auto prefix= std::make_shared<prefix_state> ();
  return [body, source_name, baseline, original_maps, prefix,
          prepare_baseline= std::move (prepare_baseline),
          prepare_maps= std::move (prepare_maps), capture] (const std::vector<int>& where)
      -> std::shared_ptr<const Result> {
    if (!is_func (body, DOCUMENT) || (!where.empty () &&
        (where.front () < 0 || where.front () >= N(body)))) return {};
    if (!*baseline) {
      *baseline= prepare_baseline ();
      if (!*baseline) return {};
      if (prepare_maps) *original_maps= prepare_maps ();
    }
    // Native and cold callers have different auxiliary/attachment ownership.
    // Reuse the same aliases they would pass to their isolated edit_env.
    const bool forward= !where.empty () && prefix->entry && where.front () >= prefix->paragraph;
    const auto& start= forward ? prefix->entry : *baseline;
    const source_contract_maps& saved= forward ? prefix->maps : *original_maps;
    hashmap<string,tree> refs= copy (saved.refs);
    hashmap<string,tree> global_refs= copy (saved.global_refs);
    hashmap<string,tree> aux= copy (saved.aux);
    hashmap<string,tree> global_aux= copy (saved.global_aux);
    hashmap<string,tree> attachments= copy (saved.attachments);
    hashmap<string,tree> global_attachments= copy (saved.global_attachments);
    athena::contracts::evaluation_state local ((*baseline)->contract_context ().snapshot ());
    athena::contracts::evaluation_state_scope owner (&local);
    edit_env evaluation (local, source_name, refs, global_refs,
                         aux, saved.shared_aux ? aux : global_aux,
                         attachments,
                         saved.shared_attachments ? attachments : global_attachments);
    // Source planning visits nodes in preorder. Retain just the latest root
    // paragraph entry; replaying each entire prefix would be quadratic on a
    // document with custom macros in many later paragraphs. The entry is
    // captured before descending into a paragraph, so its WITH/macro scopes
    // cannot leak into another source occurrence.
    start->restore (*evaluation.operator-> ());
    evaluation->style_init_env ();
    evaluation->update ();
    if (where.empty ()) return capture (local, *evaluation.operator-> ());
    for (int i= forward ? prefix->paragraph : 0; i<where.front (); ++i)
      (void) evaluation->exec (body[i]);
    if (!prefix->entry || where.front () > prefix->paragraph) {
      prefix->paragraph= where.front ();
      prefix->entry= std::make_shared<const evaluation_environment_checkpoint> (
        *evaluation.operator-> ());
      prefix->maps= {copy (refs), copy (global_refs), copy (aux),
                     copy (global_aux), copy (attachments), copy (global_attachments),
                     saved.shared_aux, saved.shared_attachments};
    }
    path inside;
    for (std::size_t i= 1; i<where.size (); ++i) inside= inside * where[i];
    if (!evaluation->exec_until (body[where.front ()], inside * 0)) return {};
    evaluation->analyze_contracts ();
    return capture (local, *evaluation.operator-> ());
  };
}

static std::shared_ptr<const athena::contracts::contract_scope>
capture_source_contracts (const athena::contracts::evaluation_state& state, edit_env_rep&) {
  return std::make_shared<const athena::contracts::contract_scope> (state.contracts);
}

document_source_contract_preparation
prepare_source_contract_positions (
    tree body, url source_name,
    std::function<std::shared_ptr<const evaluation_environment_checkpoint> ()> baseline,
    std::function<source_contract_maps ()> maps) {
  return prepare_source_positions<athena::contracts::contract_scope> (
    body, source_name, std::move (baseline), std::move (maps), capture_source_contracts);
}

template<typename Result, typename Capture>
static std::function<std::shared_ptr<const Result> (const std::vector<int>&)>
prepare_document_source_contexts (tree doc, url source_name, bool offline, Capture capture) {
  // Most documents have only native or explicitly classified fields. Prepare
  // the style and preamble only if an actual custom field requests a context.
  auto preamble_maps= std::make_shared<source_contract_maps> ();
  return prepare_source_positions<Result> (
    extract (doc, "body"), source_name,
    [doc, source_name, offline, preamble_maps] ()
        -> std::shared_ptr<const evaluation_environment_checkpoint> {
      init_style_data ();
      const tree style= offline ?
        preprocess_style (extract (doc, "style"), source_name) : extract (doc, "style");
      auto prepared_style= get_style_environment (style, source_name, !offline);
      athena::contracts::evaluation_state state= prepared_style->context ();
      athena::contracts::evaluation_state_scope owner (&state);
      hashmap<string,tree> refs, global_refs, aux, global_aux, attachments, global_attachments;
      edit_env initial (state, source_name, refs, global_refs,
                        aux, global_aux, attachments, global_attachments);
      prepared_style->restore (*initial.operator-> ());
      initial->style_init_env ();
      initial->update ();
      initial->patch_env (hashmap<string,tree> (UNINIT, extract (doc, "initial")));
      initial->update ();
      const tree preamble= get_document_preamble (doc);
      if (preamble != "") initial->exec (preamble);
      initial->analyze_contracts ();
      *preamble_maps= {copy (refs), copy (global_refs), copy (aux), copy (global_aux),
                       copy (attachments), copy (global_attachments)};
      return std::make_shared<const evaluation_environment_checkpoint> (
        *initial.operator-> ());
    }, [preamble_maps] { return *preamble_maps; }, capture);
}

document_source_contract_preparation
prepare_document_source_contracts (tree doc, url source_name) {
  return prepare_document_source_contexts<athena::contracts::contract_scope> (
    doc, source_name, false, capture_source_contracts);
}

document_source_contract_preparation
prepare_offline_document_source_contracts (tree doc, url source_name) {
  return prepare_document_source_contexts<athena::contracts::contract_scope> (
    doc, source_name, true, capture_source_contracts);
}

document_source_environment_preparation
prepare_document_source_environments (tree doc, url source_name) {
  return prepare_document_source_contexts<evaluation_environment_checkpoint> (
    doc, source_name, false, [] (const auto&, edit_env_rep& env) {
      return std::make_shared<const evaluation_environment_checkpoint> (env);
    });
}

athena::contracts::source_contract_preparation
athena::contracts::source_contracts (source_environment_preparation prepare) {
  return [prepare= std::move (prepare)] (path p) -> std::shared_ptr<const contract_scope> {
    const auto entry= prepare (p);
    return entry ? std::shared_ptr<const contract_scope> (entry, &entry->contract_context ()) : nullptr;
  };
}

/******************************************************************************
* The style and package menus
******************************************************************************/

static bool
ignore_dir (string dir) {
  return
    (dir == "Beamer") ||
    (dir == "Compute") ||
    (dir == "Customize") ||
    (dir == "Environment") ||
    (dir == "Gui") ||
    (dir == "Header") ||
    (dir == "Latex") ||
    (dir == "Miscellaneous") ||
    (dir == "Obsolete") ||
    (dir == "Poster") ||
    (dir == "Section") ||
    (dir == "Session") ||
    (dir == "Standard") ||
    (dir == "Test") ||
    (dir == "Themes");
}

static bool
hidden_package (url u, string name, bool hidden) {
  if (is_or (u))
    return hidden_package (u[1], name, hidden) ||
           hidden_package (u[2], name, hidden);
  if (is_concat (u)) {
    string dir= upcase_first (as_string (u[1]));
    if (dir == "CVS" || dir == ".svn") return false;
    return hidden_package (u[2], name, hidden || ignore_dir (dir));
  }
  if (hidden && is_atomic (u)) {
    string l= as_string (u);
    if (ends (l, ".ats")) l= l (0, N(l)-4);
    else if (ends (l, ".ts")) l= l (0, N(l)-3);
    else if (ends (l, ".hook")) l= l (0, N(l)-5);
    else return false;
    return name == l;
  }
  return false;
}

bool
hidden_package (string name) {
  if (name == "std-latex") return false;
  if (!hidden_packages->contains (name)) {
    url pck_u= descendance ("$ATHENA_PACKAGE_ROOT");
    hidden_packages (name)= hidden_package (pck_u, name, false);
  }
  return hidden_packages [name];
}

static void
collect_style_names (url u, array<string>& out) {
  if (is_or (u)) {
    collect_style_names (u[1], out);
    collect_style_names (u[2], out);
    return;
  }
  if (is_concat (u)) {
    string dir= upcase_first (as_string (u[1]));
    if (ignore_dir (dir) || dir == "CVS" || dir == ".svn") return;
    collect_style_names (u[2], out);
    return;
  }
  if (!is_atomic (u)) return;
  string name= as_string (u);
  if (ends (name, ".ats")) name= name (0, N(name)-4);
  else if (ends (name, ".ts")) name= name (0, N(name)-3);
  else if (ends (name, ".hook")) name= name (0, N(name)-5);
  else return;
  for (int i=0; i<N(out); ++i)
    if (out[i] == name) return;
  out << name;
}

namespace {
struct style_name_catalog {
  std::mutex mutex;
  std::uint64_t generation= std::numeric_limits<std::uint64_t>::max ();
  std::vector<std::string> names;
};

struct local_style_names {
  std::uint64_t generation= std::numeric_limits<std::uint64_t>::max ();
  array<string> names;
};

array<string>
cached_style_names (const char* root, style_name_catalog& catalog,
                    local_style_names& local) {
  const std::uint64_t current= style_cache_generation ();
  if (local.generation != current) {
    std::lock_guard<std::mutex> lock (catalog.mutex);
    if (catalog.generation != current) {
      array<string> collected;
      collect_style_names (descendance (root), collected);
      std::vector<std::string> names;
      names.reserve (N (collected));
      for (int i= 0; i < N (collected); ++i)
        names.emplace_back (collected[i].data (), N (collected[i]));
      catalog.names= std::move (names);
      catalog.generation= current;
    }
    // Native containers have non-atomic reference counts. Keep the returned
    // array and all its strings local to the consuming actor thread.
    local.names= array<string> ();
    for (const auto& name: catalog.names)
      local.names << string (name.data (), int (name.size ()));
    local.generation= current;
  }
  return local.names;
}
}

array<string>
get_style_names () {
  static style_name_catalog catalog;
  static thread_local local_style_names local;
  return cached_style_names ("$ATHENA_STYLE_ROOT", catalog, local);
}

array<string>
get_package_names () {
  static style_name_catalog catalog;
  static thread_local local_style_names local;
  return cached_style_names ("$ATHENA_PACKAGE_ROOT", catalog, local);
}
