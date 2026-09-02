
/******************************************************************************
* MODULE     : new_view.cpp
* DESCRIPTION: View management
* COPYRIGHT  : (C) 1999-2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_data.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "web_files.hpp"
#include "tm_link.hpp"
#include "message.hpp"
#include "new_document.hpp"
#include "drd_std.hpp"
#include "boot.hpp"
#include "scheme_execution_context.hpp"
#include "buffer_actor.hpp"
#include "editor.hpp"

#ifdef QTTEXMACS
#include <QTimer>
#include "Qt/qt_actor_widget.hpp"
#endif

#ifdef OS_MINGW
#define WINPATHS
#endif

/******************************************************************************
* Associating URLs to views
******************************************************************************/

static hashmap<tree,int> view_number_table (0);
static hashmap<tree,pointer> view_table (NULL);
static athena_view_id next_runtime_view_id= 1;

static int
new_view_number (url u) {
  view_number_table (u->t) += 1;
  return view_number_table [u->t];
}

tm_view_rep::tm_view_rep (tm_buffer buf2):
  buf (buf2), canvas (), win (NULL), master_view (NULL),
  nr (new_view_number (buf->buf->name)),
  runtime_id (next_runtime_view_id++) {
  ASSERT (runtime_id != ATHENA_NO_VIEW, "view id space exhausted");
#ifdef QTTEXMACS
  bool embedded= starts (as_string (buf->buf->name), "tmfs://aux/");
  canvas= actor_editor_widget (buf->actor->id (), runtime_id, embedded);
#endif
}

static string
encode_url (url u) {
  if (!is_rooted (u)) return "here/" * as_string (u, URL_UNIX);
  if (get_root (u) == "default") return "default" * as_string (u, URL_UNIX);
  return get_root (u) * "/" * as_string (unroot (u), URL_UNIX);
}

static url
decode_url (string s) {
  int i= search_forwards ("/", 0, s);
  if (i < 0) return url_none ();
#ifdef WINPATHS
  int j= 0;
  if (s (0, i) == "here") j= i+1;
  if (s (0, i) == "default") j= i;
  if (j != 0) {
    if (s[j+1] == ':') return url (s (j, N(s)));
    else return url_root ("default") * url (s (j, N(s)));
  }
#else
  if (s (0, i) == "here") return url (s (i+1, N(s)));
  if (s (0, i) == "default") return url (s (i, N(s)));
#endif
  return url_root (s (0, i)) * url_general (s (i+1, N(s)), URL_CLEAN_UNIX);
}

url
make_abstract_view_url (url buffer_name, int view_number) {
  string name= encode_url (buffer_name);
  //cout << vw->buf->buf->name << " -> " << name << "\n";
  string nr  = as_string (view_number);
  return "tmfs://view/" * nr * "/" * name;
}

url
abstract_view (tm_view vw) {
  if (vw == NULL) return url_none ();
  return make_abstract_view_url (vw->buf->buf->name, vw->nr);
}

tm_view
concrete_view (url u) {
  if (is_none (u)) return NULL;
  string s= as_string (u);
  if (!starts (s, "tmfs://view/")) return NULL;
  s= s (N (string ("tmfs://view/")), N(s));
  int i= search_forwards ("/", 0, s);
  if (i < 0) return NULL;
  int nr= as_int (s (0, i));
  url name= decode_url (s (i+1, N(s)));
  //cout << s (i+1, N(s)) << " -> " << name << "\n";
  tm_buffer buf= concrete_buffer (name);
  if (!is_nil (buf))
    for (i=0; i<N(buf->vws); i++)
      if (buf->vws[i]->nr == nr)
        return buf->vws[i];
  return NULL;
}

tm_view
concrete_runtime_view (athena_view_id view_id) {
  if (view_id == ATHENA_NO_VIEW) return nullptr;
  for (int i= 0; i < N (bufs); ++i)
    for (int j= 0; j < N (bufs[i]->vws); ++j)
      if (bufs[i]->vws[j]->runtime_id == view_id)
        return bufs[i]->vws[j];
  return nullptr;
}

/******************************************************************************
* Views associated to editor, window, or buffer
******************************************************************************/

tm_view the_view= NULL;

bool
has_current_view () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr) return context->view_id != ATHENA_NO_VIEW;
  return the_view != NULL;
}

void
set_current_view (url u) {
  tm_view vw= concrete_view (u);
  //ASSERT (is_none (u) || starts (as_string (tail (u)), "no_name") || vw != NULL, "bad view");
  the_view= vw;
  if (vw != NULL) {
    vw->buf->buf->last_visit= texmacs_time ();
  }
  swap_current_drd (nullptr);
  swap_current_document_tree (nullptr);
}

url
get_current_view () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr) {
    ASSERT (context->view_id != ATHENA_NO_VIEW,
            "no active view in Scheme context");
    return context->actor->current_view_url (context->view_id);
  }
  ASSERT (the_view != NULL, "no active view");
  return abstract_view (the_view);
}

url
get_current_view_safe () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr)
    return context->view_id == ATHENA_NO_VIEW ? url_none () :
      context->actor->current_view_url (context->view_id);
  if (the_view == NULL) return url_none ();
  return abstract_view (the_view);
}

void notify_delete_view (url u);

editor
get_current_editor () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr) {
    ASSERT (context->editor != nullptr,
            "no editor capability in Scheme execution context");
    ASSERT (context->has (SCHEME_CAPABILITY_BUFFER),
            "Scheme execution context cannot access a buffer editor");
    return editor (context->editor);
  }
  FAILED ("editor state is owned by its BufferActor");
  return editor ();
}

array<url>
buffer_to_views (url name) {
  tm_buffer buf= concrete_buffer (name);
  array<url> r;
  if (is_nil (buf)) return r;
  for (int i=0; i<N(buf->vws); i++)
    r << abstract_view (buf->vws[i]);
  return r;
}

url
view_to_buffer (url u) {
  tm_view vw= concrete_view (u);
  if (vw == NULL) return url_none ();
  return vw->buf->buf->name;
}

url
view_to_window (url u) {
  tm_view vw= concrete_view (u);
  if (vw == NULL) return url_none ();
  return abstract_window (vw->win);
}

editor
view_to_editor (url u) {
  tm_view vw= concrete_view (u);
  if (vw == NULL) {
    notify_delete_view (u); // HACK: returns to valid (?) state.
#ifdef ADVANCED_DEVELOPER_MODE
    failed_error << "View is " << u << "\n";
    FAILED ("View admits no editor");
#else
    return editor ();
#endif
  }
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor == vw->buf->actor &&
      context->view_id == vw->runtime_id && context->editor != nullptr)
    return editor (context->editor);
  FAILED ("editor state requested outside its BufferActor");
  return editor ();
}

/******************************************************************************
* Viewing history
******************************************************************************/

array<url> view_history;

void
notify_set_view (url u) {
  int i;
  for (i=0; i<N(view_history); i++)
    if (view_history[i] == u) break;
  if (i >= N(view_history))
    view_history= append (u, view_history);
  else {
    int j;
    for (j=i; j>0; j--)
      view_history[j]= view_history[j-1];
    view_history[j]= u;
  }
}

void
notify_delete_view (url u) {
  for (int i=0; i<N(view_history); i++)
    if (view_history[i] == u) {
      view_history= append (range (view_history, 0, i),
			    range (view_history, i+1, N(view_history)));
      return;
    }
}

url
get_recent_view (url name, bool same, bool other, bool active, bool passive) {
  // Get most recent view with the following filters:
  //   If same, then the name of the buffer much be name
  //   If other, then the name of the buffer much be other than name
  //   If active, then the buffer must be active
  //   If passive, then the buffer must be passive
  int i;
  for (i= 0; i < N(view_history); i++) {
    tm_view vw= concrete_view (view_history[i]);
    if (vw != NULL) {
      if (same && vw->buf->buf->name != name) continue;
      if (other && vw->buf->buf->name == name) continue;
      if (active && vw->win == NULL) continue;
      if (passive && vw->win != NULL) continue;
      return view_history[i];
    }
  }
  return url_none ();
}

array<url>
get_all_views () {
  return view_history;
}

/******************************************************************************
* Creation of views on buffers
******************************************************************************/

url tm_init_buffer_file= url_none ();
url my_init_buffer_file= url_none ();
static bool defer_view_initialization= false;
static url deferred_view= url_none ();

void
defer_next_view_initialization () {
  defer_view_initialization= true;
}

void
initialize_current_view_scheme () {
  ASSERT (!is_none (tm_init_buffer_file) && !is_none (my_init_buffer_file),
          "view initialization paths were not prepared by the Server");
  bench_start ("load init buffer");
  if (exists (tm_init_buffer_file)) exec_file (tm_init_buffer_file);
  if (exists (my_init_buffer_file)) exec_file (my_init_buffer_file);
  bench_cumul ("load init buffer");
}

static void
initialize_view (tm_view vw) {
  if (is_none (tm_init_buffer_file))
    tm_init_buffer_file= "$ATHENA_PATH/progs/init-buffer.scm";
  if (is_none (my_init_buffer_file))
    my_init_buffer_file= "$ATHENA_HOME_PATH/progs/my-init-buffer.scm";
  if (!vw->buf->actor->invoke (
        actor_command_kind::initialize_view, vw->runtime_id))
    FAILED ("BufferActor rejected view initialization");
}

void
schedule_deferred_view_initialization () {
#ifdef QTTEXMACS
  url pending= deferred_view;
  deferred_view= url_none ();
  QTimer::singleShot (500, [pending] () {
    tm_view vw= concrete_view (pending);
    if (vw != nullptr && vw->win != nullptr) initialize_view (vw);
  });
#endif
}

url
get_new_view (url name) {
  //cout << "Creating new view " << name << "\n";

  create_buffer (name, tree (DOCUMENT));
  tm_buffer buf= concrete_buffer (name);
  tm_view   vw = tm_new<tm_view_rep> (buf);
  buf->vws << vw;
  bench_start ("construct initial editor");
  if (!buf->actor->invoke (
        actor_command_kind::create_view, vw->runtime_id,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, static_cast<std::uint64_t> (vw->nr)))
    FAILED ("BufferActor rejected view creation");
  bench_cumul ("construct initial editor");

  if (defer_view_initialization) {
    defer_view_initialization= false;
    deferred_view= abstract_view (vw);
  }
  else initialize_view (vw);

  //cout << "View created " << abstract_view (vw) << "\n";
  return abstract_view (vw);
}

url
get_passive_view (url name) {
  // Get a view on a buffer, but not one which is attached to a window
  // Create a new view if no such view exists
  tm_buffer buf= concrete_buffer_insist (name);
  if (is_nil (buf)) return url_none ();
  array<url> vs= buffer_to_views (name);
  for (int i=0; i<N(vs); i++) {
    url win= view_to_window (vs[i]);
    if (is_none (win)) return vs[i];
  }
  return get_new_view (buf->buf->name);
}

url
get_recent_view (url name) {
  // Get (most) recent view on a buffer, with a preference for
  // the current buffer or another view attached to a window
  array<url> vs= buffer_to_views (name);
  if (N(vs) == 0) return get_new_view (name);
  url u= get_current_view ();
  if (view_to_buffer (u) == name) return u;
  url r= get_recent_view (name, true, false, true, false);
  if (!is_none (r)) return r;
  r= get_recent_view (name, true, false, false, false);
  if (!is_none (r)) return r;
  return vs[0];
}

/******************************************************************************
* Destroying a view
******************************************************************************/

void
delete_view (url u) {
  tm_view vw= concrete_view (u);
  if (vw == NULL) return;
  if (the_view == vw) set_current_view (url_none ());
  tm_buffer buf= vw->buf;
  int i, j, n= N(buf->vws);
  for (i=0; i<n; i++)
    if (buf->vws[i] == vw) {
      array<tm_view> a (n-1);
      for (j=0; j<n-1; j++)
        if (j<i) a[j]= buf->vws[j];
        else a[j]= buf->vws[j+1];
      buf->vws= a;
    }
  notify_delete_view (u);
  (void) buf->actor->invoke (
    actor_command_kind::destroy_view, vw->runtime_id);
  tm_delete (vw);
}

void
notify_rename_before (url old_name) {
  array<url> vs= buffer_to_views (old_name);
  for (int i=0; i<N(vs); i++)
    notify_delete_view (vs[i]);
}

void
notify_rename_after (url new_name) {
  array<url> vs= buffer_to_views (new_name);
  for (int i=0; i<N(vs); i++)
    notify_set_view (vs[i]);
}

/******************************************************************************
* Attaching and detaching views
******************************************************************************/

void
attach_view (url win_u, url u) {
  tm_window win= concrete_window (win_u);
  tm_view   vw = concrete_view (u);
  if (win == NULL || vw == NULL) return;
  // cout << "Attach view " << vw->buf->buf->name << "\n";
  vw->win= win;
  widget wid= win->wid;
  bench_start ("attach editor canvas");
  set_scrollable (wid, vw->canvas);
  bench_cumul ("attach editor canvas");
  ASSERT (is_attached (wid), "widget should be attached");
  bench_start ("resume initial editor");
  (void) vw->buf->actor->submit (
    actor_command_kind::resume_view, vw->runtime_id);
  bench_cumul ("resume initial editor");
  win->set_window_name (vw->buf->buf->title);
  win->set_window_url (vw->buf->buf->name);
  notify_set_view (u);
  // cout << "View attached\n";
}

void
detach_view (url u) {
  tm_view vw = concrete_view (u);
  if (vw == NULL) return;
  tm_window win= vw->win;
  if (win == NULL) return;
  // cout << "Detach view " << vw->buf->buf->name << "\n";
  vw->win= NULL;
  widget wid= win->wid;
  ASSERT (is_attached (wid), "widget should be attached");
  (void) vw->buf->actor->submit (
    actor_command_kind::suspend_view, vw->runtime_id);
  set_scrollable (wid, glue_widget ());
  win->set_window_name ("TeXmacs");
  win->set_window_url (url_none ());
  // cout << "View detached\n";
}

/******************************************************************************
* Switching views
******************************************************************************/

void
window_set_view (url win_u, url new_u, bool focus) {
  //cout << "set view " << win_u << ", " << new_u << ", " << focus << "\n";
  tm_window win= concrete_window (win_u);
  if (win == NULL) return;
  //cout << "Found window\n";
  tm_view new_vw= concrete_view (new_u);
  if (new_vw == NULL || new_vw->win == win) return;
  //cout << "Found view\n";
  ASSERT (new_vw->win == NULL, "view attached to other window");
  url old_u= window_to_view (win_u);
  if (!is_none (old_u)) detach_view (old_u);
  attach_view (win_u, new_u);
  if (focus || get_current_view () == old_u)
    set_current_view (new_u);
}

void
switch_to_buffer (url name) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->editor != nullptr) {
    (void) context->editor->publish_ui_text (
      actor_command_kind::ui_switch_to_buffer, encode_url (name));
    return;
  }
  //cout << "Switching to buffer " << name << "\n";
  url u= get_passive_view (name);
  tm_view vw= concrete_view (u);
  if (vw == NULL) return;
  window_set_view (get_current_window (), u, true);
  tm_window nwin= vw->win;
  if (nwin != NULL)
    nwin->set_window_zoom_factor (nwin->get_window_zoom_factor ());
  //cout << "Switched to buffer " << vw->buf->buf->name << "\n";
}

void
switch_to_buffer_from_actor (string encoded_name) {
  switch_to_buffer (decode_url (std::move (encoded_name)));
}

void
set_current_drd (url name) {
  (void) name;
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  swap_current_drd (context == nullptr ? nullptr : context->drd);
}

bool
focus_on_buffer (url name) {
  // Focus on the most recent view on a buffer, preferably active in a window
  // Return false if no view exists for the buffer
  if (the_view != NULL && the_view->buf->buf->name == name) return true;
  url r= get_recent_view (name, true, false, true, false);
  if (is_none (r)) r= get_recent_view (name, true, false, false, false);
  if (is_none (r)) {
    array<url> vws= buffer_to_views (name);
    if (N(vws) > 0) r= vws[0];
  }
  if (is_none (r)) return false;
  set_current_view (r);
  return true;
}

bool
var_focus_on_buffer (url name) {
  if (the_view != NULL && the_view->buf->buf->name == name) return true;
  url r= get_recent_view (name, true, false, true, false);
  if (is_none (r)) r= get_recent_view (name, true, false, false, false);
  if (is_none (r)) {
    array<url> vws= buffer_to_views (name);
    if (N(vws) > 0) r= vws[0];
  }
  if (is_none (r)) return false;
  if (the_view != nullptr) {
    (void) the_view->buf->actor->submit (
      actor_command_kind::suspend_view, the_view->runtime_id);
  }
  set_current_view (r);
  tm_view new_vw = concrete_view (r);
  (void) new_vw->buf->actor->submit (
    actor_command_kind::resume_view, new_vw->runtime_id);
  send_keyboard_focus (new_vw->canvas);
  return true;
}
