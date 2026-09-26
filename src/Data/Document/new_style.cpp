
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
#include "tm_configure.hpp"
#include "../../Typeset/env.hpp"
#include <QFileInfo>
#include <QSaveFile>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>

/******************************************************************************
* Global data
******************************************************************************/

struct style_data_rep {
  hashmap<tree,hashmap<string,tree> > style_cache;
  hashmap<tree,tree> style_drd;
  hashmap<string,bool> style_busy;
  hashmap<string,tree> style_void;
  drd_info drd_void;
  hashmap<tree,hashmap<string,tree> > style_cached;
  hashmap<tree,drd_info> drd_cached;

  style_data_rep ():
    style_cache (hashmap<string,tree> (UNINIT)),
    style_drd (tree (COLLECTION)),
    style_busy (false),
    style_void (UNINIT),
    drd_void ("void"),
    style_cached (style_void),
    drd_cached (drd_void) {}
};

static std::atomic<std::uint64_t> style_generation {1};
static std::mutex style_disk_cache_mutex;
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
* Caching style files on disk
******************************************************************************/

static string
cache_file_name (tree t) {
  if (is_atomic (t)) {
    string s= t->label;
    if (ends (s, ".ats")) s= s (0, N(s) - 4);
    else if (ends (s, ".ts")) s= s (0, N(s) - 3);
    s= replace (s, "/", "%");
    s= replace (s, "\\", "%");
    s= replace (s, ":", "_");
    return s;
  }
  else {
    string s;
    int i, n= N(t);
    for (i=0; i<n; i++)
      s << "__" << cache_file_name (t[i]);
    return s * "__";
  }
}

static string
style_cache_file_name (tree style) {
  string ns= BUILD_DATE;
  for (int i=0; i<N(ns); i++)
    if (!is_alpha (ns[i]) && !is_digit (ns[i])) ns.set (i, '_');
  return "__athena-style-cache-" * ns * "__" * cache_file_name (style);
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
  std::lock_guard<std::mutex> lock (style_disk_cache_mutex);
  remove ("$ATHENA_HOME_PATH/system/cache" * url_wildcard ("__*"));
}

std::uint64_t
style_cache_generation () {
  return style_generation.load (std::memory_order_acquire);
}

void
style_set_cache (tree style, hashmap<string,tree> H, tree t) {
  init_style_data ();
  // cout << "set cache " << style << LF;
  sd->style_cache (copy (style))= H;
  sd->style_drd   (copy (style))= t;
  url name ("$ATHENA_HOME_PATH/system/cache", style_cache_file_name (style));
  std::lock_guard<std::mutex> guard (style_disk_cache_mutex);
  if (!exists (name)) {
    save_string (name, tree_to_scheme (tuple ((tree) H, t)));
    // cout << "saved " << name << LF;
  }
}

void
style_get_cache (tree style, hashmap<string,tree>& H, tree& t, bool& f) {
  init_style_data ();
  //cout << "get cache " << style << LF;
  if ((style == "") || (style == tree (TUPLE))) { f= false; return; }
  f= sd->style_cache->contains (style);
  if (f) {
    H= sd->style_cache [style];
    t= sd->style_drd   [style];
  }
  else {
    string s;
    url name ("$ATHENA_HOME_PATH/system/cache", style_cache_file_name (style));
    std::lock_guard<std::mutex> guard (style_disk_cache_mutex);
    if (exists (name) && (!load_string (name, s, false))) {
      //cout << "loaded " << name << LF;
      tree p= scheme_to_tree (s);
      H= hashmap<string,tree> (UNINIT, p[0]);
      t= p[1];
      sd->style_cache (copy (style))= H;
      sd->style_drd   (copy (style))= t;
      f= true;
    }
  }
}

/******************************************************************************
* Get environment and drd of style files
******************************************************************************/

bool
compute_env_and_drd (tree style) {
  init_style_data ();
  ASSERT (is_tuple (style), "style tuple expected");
  bool busy= false;
  for (int i=0; i<N(style); i++)
    busy= busy || sd->style_busy->contains (as_string (style[i]));
  hashmap<string,bool> old_busy= copy (sd->style_busy);
  for (int i=0; i<N(style); i++)
    sd->style_busy (as_string (style[i]))= true;

  //cout << "Get environment of " << style << INDENT << LF;
  hashmap<string,tree> H;
  drd_info drd ("none", standard_drd_for_thread ());
  url none= url ("$PWD/none");
  hashmap<string,tree> lref;
  hashmap<string,tree> gref;
  hashmap<string,tree> laux;
  hashmap<string,tree> gaux;
  hashmap<string,tree> latt;
  hashmap<string,tree> gatt;
  edit_env env (drd, none, lref, gref, laux, gaux, latt, gatt);
  if (!busy) {
    tree t;
    bool ok;
    style_get_cache (style, H, t, ok);
    if (ok) {
      env->patch_env (H);
      ok= drd->set_locals (t);
      drd->set_environment (H);
    }
    if (!ok) {
      env->exec (tree (USE_PACKAGE, A (style)));
      env->read_env (H);
      drd->heuristic_init (H);
    }
    sd->style_cached (style)= H;
    sd->drd_cached (style)= drd;
  }
  //cout << UNINDENT << "Got environment of " << style << LF;

  sd->style_busy= old_busy;
  return !busy;
}

hashmap<string,tree>
get_style_env (tree style) {
  //cout << "get_style_env " << style << "\n";
  init_style_data ();
  if (sd->style_cached->contains (style))
    return sd->style_cached [style];
  else if (compute_env_and_drd (style))
    return sd->style_cached [style];
  else {
    //cout << "Busy style: " << style << "\n";
    return hashmap<string,tree> ();
  }
}

drd_info
get_style_drd (tree style) {
  //cout << "get_style_drd " << style << "\n";
  init_style_data ();
  init_std_drd ();
  if (sd->drd_cached->contains (style))
    return sd->drd_cached [style];
  else if (compute_env_and_drd (style))
    return sd->drd_cached [style];
  else {
    //cout << "Busy drd: " << style << "\n";
    return standard_drd_for_thread ();
  }
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
  //cout << "style= " << style << "\n";
  drd_info drd= get_style_drd (style);
  tree p= get_document_preamble (doc);
  if (p != "") {
    drd= drd_info ("preamble", drd);
    url none= url ("$PWD/none");
    hashmap<string,tree> lref;
    hashmap<string,tree> gref;
    hashmap<string,tree> laux;
    hashmap<string,tree> gaux;
    hashmap<string,tree> latt;
    hashmap<string,tree> gatt;
    edit_env env (drd, none, lref, gref, laux, gaux, latt, gatt);
    hashmap<string,tree> H;
    env->exec (tree (USE_PACKAGE, A (style)));
    env->exec (p);
    env->read_env (H);
    drd->heuristic_init (H);
  }
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

static string
compute_style_menu (url u, int kind) {
  if (is_or (u)) {
    string sep= "\n";
    if (is_atomic (u[1]) &&
	((is_concat (u[2]) && (u[2][1] != "CVS") && (u[2][1] != ".svn")) ||
	 (is_or (u[2]) && is_concat (u[2][1]))))
      sep= "\n---\n";
    return
      compute_style_menu (u[1], kind) * sep *
      compute_style_menu (u[2], kind);
  }
  if (is_concat (u)) {
    string dir= upcase_first (as_string (u[1]));
    string sub= compute_style_menu (u[2], kind);
    if (ignore_dir (dir) || dir == "CVS" || dir == ".svn") return "";
    return "(-> \"" * dir * "\" " * sub * ")";
  }
  if (is_atomic (u)) {
    string l  = as_string (u);
    if (ends (l, ".ats")) l= l (0, N(l)-4);
    else if (ends (l, ".ts")) l= l (0, N(l)-3);
    else if (ends (l, ".hook")) l= l (0, N(l)-5);
    else return "";
    string cmd ("set-main-style");
    if (kind == 1) cmd= "add-style-package";
    if (kind == 2) cmd= "remove-style-package";
    if (kind == 3) cmd= "toggle-style-package";
    return "((verbatim \"" * l * "\") (" * cmd * " \"" * l * "\"))";
  }
  return "";
}

object
get_style_menu () {
  url sty_u= descendance ("$ATHENA_STYLE_ROOT");
  string sty= compute_style_menu (sty_u, 0);
  return eval ("(menu-dynamic " * sty * ")");
}

object
get_add_package_menu () {
  url pck_u= descendance ("$ATHENA_PACKAGE_ROOT");
  string pck= compute_style_menu (pck_u, 1);
  return eval ("(menu-dynamic " * pck * ")");
}

object
get_remove_package_menu () {
  url pck_u= descendance ("$ATHENA_PACKAGE_ROOT");
  string pck= compute_style_menu (pck_u, 2);
  return eval ("(menu-dynamic " * pck * ")");
}

object
get_toggle_package_menu () {
  url pck_u= descendance ("$ATHENA_PACKAGE_ROOT");
  string pck= compute_style_menu (pck_u, 3);
  return eval ("(menu-dynamic " * pck * ")");
}
