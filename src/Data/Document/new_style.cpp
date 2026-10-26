
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
#include "file.hpp"
#include "data_cache.hpp"
#include "convert.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "../../Typeset/env.hpp"
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
  hashmap<tree,style_environment_ptr> offline_documents;

  style_data_rep ():
    style_busy (false),
    styles (nullptr), offline_documents (nullptr) {}
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

    init_std_drd ();
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
  inputs (env.inputs.snapshot ()), contracts (env.drd->contracts.snapshot ()) {
  env.read_env (values);
}

void
style_environment::restore (edit_env_rep& env) const {
  if (env.contract_events)
    env.contract_events->record (athena::contracts::evaluation_barrier {});
  env.env= copy (values);
  env.inputs.reset (inputs);
  env.drd->contracts= athena::contracts::contract_scope (contracts);
  env.drd->set_environment (env.env);
}

drd_info
style_environment::descriptor (string name) const {
  drd_info result (name);
  result->contracts= athena::contracts::contract_scope (contracts);
  result->set_environment (copy (values));
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
* Get environment and drd of style files
******************************************************************************/

style_environment_ptr
get_style_environment (tree style, url source_name, bool scheme_modules) {
  init_style_data ();
  init_std_drd ();
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

  drd_info drd ("package-evaluation", standard_drd_for_thread ());
  hashmap<string,tree> lref;
  hashmap<string,tree> gref;
  hashmap<string,tree> laux;
  hashmap<string,tree> gaux;
  hashmap<string,tree> latt;
  hashmap<string,tree> gatt;
  edit_env env (drd, source_name, lref, gref, laux, gaux, latt, gatt);
  env->set_scheme_modules_enabled (scheme_modules);
  if (!busy) {
    env->exec (tree (USE_PACKAGE, A (style)));
    env->analyze_contracts ();
  }
  auto checkpoint= std::make_shared<const style_environment> (*env.operator-> ());
  if (!busy) sd->styles(key)= checkpoint;
  return checkpoint;
}

drd_info
get_style_drd (tree style) {
  return get_style_environment (style, url ("$PWD/none"), true)->descriptor ("style");
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

drd_info
get_document_drd (tree doc) {
  init_style_data ();
  tree style= extract (doc, "style");
  if (is_snippet (doc)) {
    if (the_drd->get_syntax (make_tree_label ("theorem")) != tree (UNINIT))
      return the_drd;
    style= tree (TUPLE, "generic");
  }
  url none= url ("$PWD/none");
  auto base= get_style_environment (style, none, true);
  drd_info drd= base->descriptor ("document");
  tree p= get_document_preamble (doc);
  if (p != "") {
    hashmap<string,tree> lref;
    hashmap<string,tree> gref;
    hashmap<string,tree> laux;
    hashmap<string,tree> gaux;
    hashmap<string,tree> latt;
    hashmap<string,tree> gatt;
    edit_env env (drd, none, lref, gref, laux, gaux, latt, gatt);
    base->restore (*env.operator-> ());
    env->update ();
    env->exec (p);
    env->analyze_contracts ();
  }
  return drd;
}

drd_info
get_offline_document_drd (tree doc, url source_name) {
  init_style_data ();
  tree style= preprocess_style (extract (doc, "style"), source_name);
  auto base= get_style_environment (style, source_name, false);

  tree preamble= get_document_preamble (doc);
  if (preamble == "") return base->descriptor ("offline-document");

  tree key (TUPLE, copy (style), as_string (head (source_name)), copy (preamble));
  if (sd->offline_documents->contains (key))
    return sd->offline_documents[key]->descriptor ("offline-document");

  // Preamble processing is the document-specific overlay.  Reuse the cached
  // package environment/DRD and execute only the local preamble on top of it.
  drd_info drd= base->descriptor ("offline-document-preamble");
  hashmap<string,tree> lref;
  hashmap<string,tree> gref;
  hashmap<string,tree> laux;
  hashmap<string,tree> gaux;
  hashmap<string,tree> latt;
  hashmap<string,tree> gatt;
  edit_env env (drd, source_name, lref, gref, laux, gaux, latt, gatt);
  env->set_scheme_modules_enabled (false);
  base->restore (*env.operator-> ());
  env->update ();
  env->exec (preamble);
  env->analyze_contracts ();
  sd->offline_documents(copy (key))=
    std::make_shared<const style_environment> (*env.operator-> ());
  return drd;
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
