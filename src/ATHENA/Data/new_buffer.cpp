
/******************************************************************************
* MODULE     : new_buffer.cpp
* DESCRIPTION: Buffer management
* COPYRIGHT  : (C) 1999-2022  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_data.hpp"
#include "buffer_actor.hpp"
#include "scheme_execution_context.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "web_files.hpp"
#include "tm_link.hpp"
#include "message.hpp"
#include "new_document.hpp"
#include "new_style.hpp"
#include "merge_sort.hpp"

array<tm_buffer> bufs;

bool
exec_buffer (url name, object command) {
  ASSERT (current_scheme_execution_context () == nullptr,
          "buffer command scheduling requires the global context");
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return false;
  tm_view view= concrete_view (get_recent_view (name));
  if (view == nullptr) return false;
  athena_scheme_handle_id handle=
    scheme_command_handle_acquire (object_to_tmscm (command));
  actor_command_ticket ticket= buf->actor->try_submit (
    actor_command_kind::run_scheme_handle, view->runtime_id,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB,
    SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_UI | SCHEME_CAPABILITY_GLOBAL,
    handle);
  if (!ticket) scheme_command_handle_release (handle);
  return static_cast<bool> (ticket);
}

string propose_title (string old_title, url u);

static bool
invoke_buffer_actor (
  buffer_actor* actor, actor_command_kind kind,
  athena_view_id view_id= ATHENA_NO_VIEW,
  athena_blob_id payload0= ATHENA_NO_BLOB,
  athena_blob_id payload1= ATHENA_NO_BLOB,
  actor_command_record* result= nullptr,
  std::uint64_t argument0= 0) {
  return actor != nullptr && actor->invoke (
    kind, view_id, payload0, payload1, result, SCHEME_CAPABILITY_BUFFER,
    argument0);
}

static bool
invoke_buffer_actor (
  tm_buffer buf, actor_command_kind kind,
  athena_view_id view_id= ATHENA_NO_VIEW,
  athena_blob_id payload0= ATHENA_NO_BLOB,
  athena_blob_id payload1= ATHENA_NO_BLOB,
  actor_command_record* result= nullptr,
  std::uint64_t argument0= 0) {
  return !is_nil (buf) && invoke_buffer_actor (
    buf->actor, kind, view_id, payload0, payload1, result, argument0);
}

static buffer_actor*
current_buffer_actor (url name, athena_view_id& view_id) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr || context->actor == nullptr ||
      context->actor->current_buffer_url () != name)
    return nullptr;
  view_id= context->view_id;
  return context->actor;
}

static void
discard_tree_payload (athena_blob_id payload) {
  if (payload != ATHENA_NO_BLOB)
    (void) actor_tree_registry::instance ().discard (payload);
}

static void
discard_text_payload (athena_blob_id payload) {
  if (payload != ATHENA_NO_BLOB)
    (void) actor_text_registry::instance ().discard (payload);
}

static athena_view_id
buffer_command_view (tm_buffer buf, url name) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr)
    return context->actor == buf->actor ? context->view_id : ATHENA_NO_VIEW;
  tm_view view= concrete_view (get_recent_view (name));
  return view == nullptr ? ATHENA_NO_VIEW : view->runtime_id;
}

tm_buffer_rep::tm_buffer_rep (url name):
  buf (name), vws (0), rp (0), actor (tm_new<buffer_actor> (this)) {}

tm_buffer_rep::~tm_buffer_rep () {
  tm_delete (actor);
}

/******************************************************************************
* Check for changes in the buffer
******************************************************************************/

void
tm_buffer_rep::attach_notifier () {
  (void) invoke_buffer_actor (this, actor_command_kind::attach_notifier);
}

bool
tm_buffer_rep::needs_to_be_saved () {
  if (buf->read_only) return false;
  actor_command_record result;
  return invoke_buffer_actor (
           this, actor_command_kind::query_modified, ATHENA_NO_VIEW,
           ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result) &&
         result.argument[0] != 0;
}

bool
tm_buffer_rep::needs_to_be_autosaved () {
  if (buf->read_only) return false;
  actor_command_record result;
  return invoke_buffer_actor (
           this, actor_command_kind::query_autosaved, ATHENA_NO_VIEW,
           ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result) &&
         result.argument[0] != 0;
}

/******************************************************************************
* Manipulation of buffer list
******************************************************************************/

void
insert_buffer (url name) {
  if (is_none (name)) return;
  if (!is_nil (concrete_buffer (name))) return;
  tm_buffer buf= tm_new<tm_buffer_rep> (name);
  bufs << buf;
}

void
remove_buffer (tm_buffer buf) {
  int nr, n= N(bufs);
  for (nr=0; nr<n; nr++)
    if (bufs[nr] == buf) {
      while (N(buf->vws) != 0)
        delete_view (abstract_view (buf->vws[0]));
      buf->actor->shutdown ();
      if (n == 1)
        get_server () -> quit ();
      for (int i=nr; i<n-1; i++)
        bufs[i]= bufs[i+1];
      bufs->resize (n-1);
      tm_delete (buf);
      return;
    }
}

void
remove_buffer (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (!is_nil (buf)) remove_buffer (buf);
}

int
number_buffers () {
  return N(bufs);
}

array<url>
get_all_buffers () {
  array<url> r;
  for (int i=N(bufs)-1; i>=0; i--)
    r << bufs[i]->buf->name;
  return r;
}

tm_buffer
concrete_buffer (url name) {
  int i, n= N(bufs);
  for (i=0; i<n; i++)
    if (bufs[i]->buf->name == name)
      return bufs[i];
  return nil_buffer ();
}

tm_buffer
concrete_buffer_insist (url u) {
  tm_buffer buf= concrete_buffer (u);
  if (!is_nil (buf)) return buf;
  buffer_load (u);
  return concrete_buffer (u);
}

/******************************************************************************
* Buffer names
******************************************************************************/

url
get_current_buffer () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr) {
    ASSERT (context->actor_id != ATHENA_NO_ACTOR,
            "no active buffer in Scheme execution context");
    return context->actor->current_buffer_url ();
  }
  tm_view vw= concrete_view (get_current_view ());
  return vw->buf->buf->name;
}

url
get_current_buffer_safe () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr) return context->actor->current_buffer_url ();
  url v= get_current_view_safe ();
  if (is_none (v)) return v;
  return concrete_view (v)->buf->buf->name;
}

url
path_to_buffer (path p) {
  url current= get_current_buffer_safe ();
  tm_buffer buf= concrete_buffer (current);
  if (!is_nil (buf) && buf->rp <= p) return current;
  return url_none ();
}

void
rename_buffer (url name, url new_name) {
  if (new_name == name || is_nil (concrete_buffer (name))) return;
  kill_buffer (new_name);
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  notify_rename_before (name);
  buf->buf->name= new_name;
  buf->buf->master= new_name;
  athena_blob_id renamed= actor_text_from_string (as_string (new_name));
  if (!buf->actor->invoke (
        actor_command_kind::rename_buffer, ATHENA_NO_VIEW, renamed))
    (void) actor_text_registry::instance ().discard (renamed);
  notify_rename_after (new_name);
  string title= propose_title (buf->buf->title, new_name);
  set_title_buffer (new_name, title);
}

url
make_new_buffer () {
  int i=1;
  while (true) {
    url name= url_scratch ("no_name_", ".tm", i);
    if (is_nil (concrete_buffer (name))) {
      set_buffer_tree (name, tree (DOCUMENT));
      return name;
    }
    else i++;
  }
}

bool
buffer_has_name (url name) {
  return !is_scratch (name);
}

/******************************************************************************
* Buffer title
******************************************************************************/

static string
unique_title (string old_title, string name) {
  int i, j;
  for (j=1; true; j++) {
    bool flag= true;
    string ret (name);
    if (j>1) ret= name * " (" * as_string (j) * ")";
    if (ret == old_title) return ret;
    for (i=0; i<N(bufs); i++)
      if (bufs[i]->buf->title == ret) flag= false;
    if (flag) return ret;
  }
}

string
propose_title (string old_title, url u) {
  string name= as_string (tail (u));
  if (starts (name, "no_name_") && ends (name, ".tm")) {
    string no_name= "No name";
    for (int i=0; i<N(no_name); i++)
      if (((unsigned char) (no_name[i])) >= (unsigned char) 128)
        { no_name= "No name"; break; }
    name= no_name * " [" * name (8, N(name) - 3) * "]";
  }
  if ((name == "") || (name == "."))
    name= as_string (tail (u * url_parent ()));
  if ((name == "") || (name == "."))
    name= as_string (u);
  return unique_title (old_title, name);
}

string
get_title_buffer (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return "";
  return buf->buf->title;
}

void
set_title_buffer (url name, string title) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  if (buf->buf->title == title) return;
  buf->buf->title= title;
  athena_blob_id title_payload= actor_text_from_string (copy (title));
  if (!invoke_buffer_actor (
        buf, actor_command_kind::set_buffer_title, ATHENA_NO_VIEW,
        title_payload))
    discard_text_payload (title_payload);
  array<url> vs= buffer_to_views (name);
  for (int i=0; i<N(vs); i++) {
    tm_window win= concrete_window (view_to_window (vs[i]));
    if (win != NULL) {
      win->set_window_name (title);
      win->set_window_url (name);
    }
  }
}

void
set_proposed_title_buffer (url name, string title) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  set_title_buffer (name, unique_title (buf->buf->title, std::move (title)));
}

/******************************************************************************
* Setting and getting the buffer tree contents
******************************************************************************/

void
set_buffer_tree (url name, tree doc) {
  tm_buffer buf= concrete_buffer (name);
  bool inserted= is_nil (buf);
  if (inserted) {
    insert_buffer (name);
    buf= concrete_buffer (name);
  }
  string old_title= buf->buf->title;

  string proposed_title= propose_title (old_title, name);
  athena_blob_id document_payload=
    actor_tree_registry::instance ().store (std::move (doc));
  if (!invoke_buffer_actor (
        buf, actor_command_kind::replace_document, ATHENA_NO_VIEW,
        document_payload)) {
    discard_tree_payload (document_payload);
    return;
  }
  buf->buf->title= std::move (proposed_title);
  if (is_rooted_tmfs (name)) {
    buf->buf->read_only=
      !as_bool (call ("tmfs-permission?", object (name), object ("write")));
    (void) invoke_buffer_actor (
      buf, actor_command_kind::set_buffer_read_only, ATHENA_NO_VIEW,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      buf->buf->read_only ? 1 : 0);
  }
  athena_blob_id title_payload=
    actor_text_from_string (copy (buf->buf->title));
  if (!invoke_buffer_actor (
        buf, actor_command_kind::set_buffer_title, ATHENA_NO_VIEW,
        title_payload))
    discard_text_payload (title_payload);
  pretend_buffer_saved (name);
}

tree
get_buffer_tree (url name) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  buffer_actor* actor= current_buffer_actor (name, view_id);
  if (actor == nullptr) {
    tm_buffer buf= concrete_buffer (name);
    if (is_nil (buf)) return "";
    actor= buf->actor;
  }
  actor_command_record result;
  if (!invoke_buffer_actor (
        actor, actor_command_kind::snapshot_document, view_id,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result))
    return "";
  return actor_tree_registry::instance ().take (result.payload0);
}

void
set_buffer_body (url name, tree body) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) {
    new_data data;
    set_buffer_tree (name, attach_data (body, data));
  }
  else {
    athena_blob_id body_payload=
      actor_tree_registry::instance ().store (std::move (body));
    if (!invoke_buffer_actor (
          buf, actor_command_kind::replace_body, ATHENA_NO_VIEW,
          body_payload)) {
      discard_tree_payload (body_payload);
      return;
    }
    pretend_buffer_saved (name);
  }
}

tree
get_buffer_body (url name) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  buffer_actor* actor= current_buffer_actor (name, view_id);
  if (actor == nullptr) {
    tm_buffer buf= concrete_buffer (name);
    if (is_nil (buf)) return "";
    actor= buf->actor;
  }
  actor_command_record result;
  if (!invoke_buffer_actor (
        actor, actor_command_kind::snapshot_body, view_id,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result))
    return "";
  return actor_tree_registry::instance ().take (result.payload0);
}

/******************************************************************************
* Further information attached to buffers
******************************************************************************/

url
get_master_buffer (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return url_none ();
  return buf->buf->master;
}

void
set_master_buffer (url name, url master) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  if (buf->buf->master == master) return;
  buf->buf->master= master;
  athena_blob_id master_payload= actor_text_from_string (as_string (master));
  if (!invoke_buffer_actor (
        buf, actor_command_kind::set_master_buffer, ATHENA_NO_VIEW,
        master_payload))
    discard_text_payload (master_payload);
}

void
set_last_save_buffer (url name, int t) {
  tm_buffer buf= concrete_buffer (name);
  if (!is_nil (buf)) buf->buf->last_save= t;
  //cout << "Set last save " << name << " -> " << t << "\n";
}

int
get_last_save_buffer (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) {
    //cout << "Get last save " << name << " -> *\n";
    return - (int) (((unsigned int) (-1)) >> 1);
  }
  //cout << "Get last save " << name << " -> " << buf->buf->last_save << "\n";
  return (int) buf->buf->last_save;
}

bool
is_aux_buffer (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return false;
  return buf->buf->master != buf->buf->name;
}

double
last_visited (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return (double) texmacs_time ();
  return (double) buf->buf->last_visit;
}

bool
buffer_modified (url name) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  if (buffer_actor* actor= current_buffer_actor (name, view_id)) {
    actor_command_record result;
    return invoke_buffer_actor (
      actor, actor_command_kind::query_modified, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result) && result.argument[0] != 0;
  }
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return false;
  return buf->needs_to_be_saved ();
}

bool
buffer_modified_since_autosave (url name) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  if (buffer_actor* actor= current_buffer_actor (name, view_id)) {
    actor_command_record result;
    return invoke_buffer_actor (
      actor, actor_command_kind::query_autosaved, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result) && result.argument[0] != 0;
  }
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return false;
  return buf->needs_to_be_autosaved ();
}

void
pretend_buffer_modified (url name) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  if (buffer_actor* actor= current_buffer_actor (name, view_id)) {
    (void) invoke_buffer_actor (actor, actor_command_kind::mark_modified, view_id);
    return;
  }
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  (void) invoke_buffer_actor (buf, actor_command_kind::mark_modified);
}

void
pretend_buffer_saved (url name) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor != nullptr &&
      context->actor->current_buffer_url () == name) {
    (void) context->actor->invoke (
      actor_command_kind::mark_saved, context->view_id);
    if (context->editor != nullptr)
      (void) context->editor->publish_ui (
        actor_command_kind::ui_mark_buffer_saved,
        static_cast<std::uint64_t> (last_modified (name)));
    return;
  }
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  (void) invoke_buffer_actor (buf, actor_command_kind::mark_saved);
  set_last_save_buffer (name, last_modified (name));
}

void
pretend_buffer_autosaved (url name) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  if (buffer_actor* actor= current_buffer_actor (name, view_id)) {
    (void) invoke_buffer_actor (actor, actor_command_kind::mark_autosaved, view_id);
    return;
  }
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  (void) invoke_buffer_actor (buf, actor_command_kind::mark_autosaved);
}

void
attach_buffer_notifier (url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return;
  buf->attach_notifier ();
}

/******************************************************************************
* Loading
******************************************************************************/

tree
attach_subformat (tree t, url u, string fm) {
  if ((fm == "texmacs") || (fm == "tmml") || (fm == "stm")) return t;
  if (!format_exists (fm)) return t;

  string s= suffix (u);
  string inferred_fm= suffix_to_format (s);
  if (!is_empty (inferred_fm) && inferred_fm != "generic") fm= inferred_fm;
  if (fm == "verbatim") return t;
  if (!prog_lang_exists (fm) &&
      fm != "scheme") return t;

  hashmap<string,tree> h (UNINIT, extract (t, "initial"));
  h (MODE)= "prog";
  h (PROG_LANGUAGE)= fm;
  tree t2= change_doc_attr (t, "initial", make_collection (h));
  return change_doc_attr (t2, "style", tree ("code"));
}

tree
import_loaded_tree (string s, url u, string fm) {
  set_file_focus (u);
  if (s == "" && suffix (u) == "ath") {
    tree doc (DOCUMENT);
    doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION)
        << compound ("style", "generic")
        << compound ("body", tree (DOCUMENT, ""));
    return doc;
  }
  if (fm == "generic" && suffix (u) == "txt") fm= "verbatim";
  if (fm == "generic") fm= get_format (s, suffix (u));
  if (fm == "texmacs" && starts (s, "(document (TeXmacs")) fm= "stm";
  if (fm == "verbatim" && starts (s, "(document (TeXmacs")) fm= "stm";
  tree t= fm == "texmacs" ? texmacs_document_to_tree (s)
                           : generic_to_tree (s, fm * "-document");
  tree links= extract (t, "links");
  if (N (links) != 0)
    (void) call ("register-link-locations", object (u), object (links));
  return attach_subformat (t, u, fm);
}

tree
import_tree (url u, string fm) {
  url r= resolve (u, "fr");
  if (is_none (r)) {
    url b= get_current_buffer ();
    r= resolve (b * url_parent () * u);
  }
  string s;
  if (is_none (r) || load_string (r, s, false)) return "error";
  set_file_focus (r);
  return import_loaded_tree (s, r, fm);
}

bool
buffer_import (url name, url src, string fm) {
  tree t= import_tree (src, fm);
  if (t == "error" || is_func (t, _ERROR)) return true;
  set_buffer_tree (name, t);
  return false;
}

bool
buffer_load (url name) {
  string fm= file_format (name);
  return buffer_import (name, name, fm);
}

thread_local hashmap<string,tree> style_tree_cache ("");
static thread_local std::uint64_t style_tree_generation= 0;

tree
load_style_tree (string package) {
  std::uint64_t generation= style_cache_generation ();
  if (style_tree_generation != generation) {
    style_tree_cache= hashmap<string,tree> ("");
    style_tree_generation= generation;
  }
  if (style_tree_cache->contains (package))
    return style_tree_cache [package];
  url name= url_none ();
  url styp= "$ATHENA_STYLE_PATH";
  if (ends (package, ".ts")) name= package;
  else name= styp * (package * ".ts");
  name= resolve (name);
  string doc_s;
  if (!load_string (name, doc_s, false)) {
    tree doc= texmacs_document_to_tree (doc_s);
    if (is_func (doc, _ERROR))
      std_warning << "Style parse error in " << name << ": "
                  << doc[0] << LF;
    else if (is_compound (doc))
      doc= extract (doc, "body");
    style_tree_cache (package)= doc;
    return doc;
  }
  style_tree_cache (package)= "";
  return "";
}

tree
with_package_definitions (string package, tree body) {
  // FIXME: it would be more robust to execute the package
  // yet an alternative idea is to fetch the complete environment of
  // a style using get_style_env.
  tree w (WITH);
  tree doc= load_style_tree (package);
  if (!is_func (doc, DOCUMENT)) return body;
  for (int i=0; i<N(doc); i++)
    if (is_func (doc[i], ASSIGN, 2))
      w << doc[i][0] << doc[i][1];
  w << body;
  return w;
}

/******************************************************************************
* Saving
******************************************************************************/

bool
export_tree (tree doc, url u, string fm) {
  if (fm == "generic") fm= "verbatim";
  string s= tree_to_generic (doc, fm * "-document");
  if (s == "* error: unknown format *") return true;
  return save_string (u, s);
}

bool
buffer_export (url name, url dest, string fm) {
  athena_view_id view_id= ATHENA_NO_VIEW;
  buffer_actor* actor= current_buffer_actor (name, view_id);
  if (actor == nullptr) {
    tm_buffer buf= concrete_buffer (name);
    if (is_nil (buf)) return true;
    actor= buf->actor;
    view_id= buffer_command_view (buf, name);
  }
  athena_blob_id destination= actor_text_from_string (as_string (dest));
  athena_blob_id format= actor_text_from_string (copy (fm));
  actor_command_record result;
  bool completed= actor->invoke (
    actor_command_kind::export_buffer, view_id,
    destination, format, &result);
  if (!completed) {
    discard_text_payload (destination);
    discard_text_payload (format);
    return true;
  }
  return result.argument[0] != 0;
}

tree
latex_expand (tree doc, url name) {
  tm_buffer buf= concrete_buffer (name);
  if (is_nil (buf)) return doc;
  athena_blob_id payload=
    actor_tree_registry::instance ().store (std::move (doc));
  actor_command_record result;
  if (!buf->actor->invoke (
        actor_command_kind::latex_expand_buffer,
        buffer_command_view (buf, name), payload, ATHENA_NO_BLOB, &result)) {
    discard_tree_payload (payload);
    return tree ();
  }
  return actor_tree_registry::instance ().take (result.payload0);
}

tree
latex_expand (tree doc) {
  url view_url (as_string (extract (doc, "view")));
  tm_view view= concrete_view (view_url);
  if (view == nullptr) return remove_doc_attr (doc, "view");
  athena_blob_id payload=
    actor_tree_registry::instance ().store (std::move (doc));
  actor_command_record result;
  if (!view->buf->actor->invoke (
        actor_command_kind::latex_expand_buffer, view->runtime_id,
        payload, ATHENA_NO_BLOB, &result)) {
    discard_tree_payload (payload);
    return tree ();
  }
  tree expanded= actor_tree_registry::instance ().take (result.payload0);
  return remove_doc_attr (expanded, "view");
}

bool
buffer_save (url name) {
  string fm= file_format (name);
  if (fm == "generic") fm= "verbatim";
  bool r= buffer_export (name, name, fm);
  if (!r) {
    pretend_buffer_saved (name);
    athena_view_id view_id= ATHENA_NO_VIEW;
    if (current_buffer_actor (name, view_id) == nullptr) {
      array<url> ws = buffer_to_windows (name);
      for (int i=0; i<N(ws); i++)
        concrete_window (ws[i])->set_modified (false);
    }
  }
  return r;
}

/******************************************************************************
* Loading inclusions
******************************************************************************/

static hashmap<string,tree> document_inclusions ("");

void
reset_inclusions () {
  document_inclusions = hashmap<string,tree> ("");
}

void
reset_inclusion (url name) {
  string name_s= as_string (name);
  document_inclusions -> reset (name_s);
}

tree
load_inclusion (url name) {
  // url name= relative (base_file_name, file_name);
  string name_s= as_string (name);
  if (document_inclusions->contains (name_s))
    return document_inclusions [name_s];
  tree doc= extract_document (import_tree (name, "generic"));
  if (!is_func (doc, _ERROR)) document_inclusions (name_s)= doc;
  return doc;
}
