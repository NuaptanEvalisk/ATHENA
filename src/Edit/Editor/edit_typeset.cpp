
/******************************************************************************
* MODULE     : typeset.cpp
* DESCRIPTION: typeset the tree being edited
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <climits>
#include "edit_typeset.hpp"
#include "heading_word_count.hpp"
#include "tm_buffer.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "tm_timer.hpp"
#include "Bridge/impl_typesetter.hpp"
#include "new_style.hpp"
#include "iterator.hpp"
#include "merge_sort.hpp"
#include "scheme.hpp"
#ifdef EXPERIMENTAL
#include "../../Style/Environment/std_environment.hpp"
#endif // EXPERIMENTAL

//box empty_box (path ip, int x1=0, int y1=0, int x2=0, int y2=0);
static bool
toc_fold_node (tree t) {
  return is_compound (t, "table-of-contents") ||
         is_compound (t, "table-of-contents*");
}

static bool
toc_fold_contains (tree t) {
  if (toc_fold_node (t)) return true;
  if (is_atomic (t)) return false;
  for (int i=0; i<N(t); i++)
    if (toc_fold_contains (t[i])) return true;
  return false;
}

/******************************************************************************
* Contructors, destructors and notification of modifications
******************************************************************************/

edit_typeset_rep::edit_typeset_rep ():
  editor_rep (), // NOTE: ignored by the compiler, but suppresses warning
  the_style (TUPLE),
  cur (hashmap<string,tree> (UNINIT)),
  stydef (UNINIT), pre (UNINIT), init (UNINIT), fin (UNINIT), grefs (UNINIT),
  folded_headings (), folded_tocs (), unfolded_tocs (),
  fold_view_active (false), fold_view_rebuild (false),
  fold_view_has_toc (false),
  heading_word_count_cache_hash (INT_MIN), heading_word_count_cache (),
  heading_word_count_cache_map (0),
  env (drd, buf->buf->master,
       buf->data->ref, grefs,
       buf->data->aux, buf->data->aux,
       buf->data->att, buf->data->att),
  ttt (new_typesetter (env, subtree (et, rp), reverse (rp))) {
    init_update ();
}

edit_typeset_rep::~edit_typeset_rep () { delete_typesetter (ttt); }

void
edit_typeset_rep::set_data (new_data data) {
  set_style (data->style);
  set_init  (data->init);
  set_fin   (data->fin);
  set_ref   (data->ref);
  set_aux   (data->aux);
  set_att   (data->att);
  notify_page_change ();
  add_init (data->init);
  notify_change (THE_ENVIRONMENT);
  notify_change (THE_DECORATIONS);
  typeset_invalidate_env ();
  iterator<string> it = iterate (data->att);
  while (it->busy()) {
    string key= it->next ();
    (void) call (string ("notify-set-attachment"),
                 buf->buf->name, key, data->att [key]);
  }
  fold_view_has_toc= toc_fold_contains (subtree (et, rp));
  if (fold_view_has_toc) fold_view_rebuild= true;
}

void
edit_typeset_rep::get_data (new_data& data) {
  data->style= get_style ();
  data->init = get_init ();
  data->fin  = get_fin ();
  data->ref  = get_ref ();
  data->aux  = get_aux ();
  data->att  = get_att ();
}

typesetter edit_typeset_rep::get_typesetter () { return ttt; }
tree edit_typeset_rep::get_style () { return the_style; }
void edit_typeset_rep::set_style (tree t) { the_style= copy (t); }
hashmap<string,tree> edit_typeset_rep::get_init () { return init; }
hashmap<string,tree> edit_typeset_rep::get_fin () { return fin; }
hashmap<string,tree> edit_typeset_rep::get_ref () { return buf->data->ref; }
hashmap<string,tree> edit_typeset_rep::get_aux () { return buf->data->aux; }
hashmap<string,tree> edit_typeset_rep::get_att () { return buf->data->att; }
void edit_typeset_rep::set_fin (hashmap<string,tree> H) { fin= H; }
void edit_typeset_rep::set_ref (hashmap<string,tree> H) { buf->data->ref= H; }
void edit_typeset_rep::set_aux (hashmap<string,tree> H) { buf->data->aux= H; }
void edit_typeset_rep::set_att (hashmap<string,tree> H) { buf->data->att= H; }

tree
edit_typeset_rep::get_ref (string key) {
  return buf->data->ref[key];
}

void
edit_typeset_rep::set_ref (string key, tree im) {
  buf->data->ref (key)= im;
}

void
edit_typeset_rep::reset_ref (string key) {
  buf->data->ref->reset (key);
}

static string
concat_as_string (tree t) {
  if (is_atomic (t)) return t->label;
  else if (is_func (t, CONCAT)) {
    string r;
    for (int i=0; i<N(t); i++)
      r << concat_as_string (t[i]);
    return r;
  }
  else return "?";
}

array<string>
edit_typeset_rep::find_refs (string val, bool global) {
  tree a= (tree) buf->data->ref;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    if (N(a[i]) >= 2 && N(a[i][1]) >= 1 &&
        concat_as_string (a[i][1][0]) == val)
      v << a[i][0]->label;
  return v;
}

array<string>
edit_typeset_rep::list_refs (bool global) {
  tree a= (tree) buf->data->ref;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    v << a[i][0]->label;
  merge_sort (v);
  return v;
}

tree
edit_typeset_rep::get_aux (string key) {
  return buf->data->aux[key];
}

void
edit_typeset_rep::set_aux (string key, tree im) {
  buf->data->aux (key)= im;
}

void
edit_typeset_rep::reset_aux (string key) {
  buf->data->aux->reset (key);
}

array<string>
edit_typeset_rep::list_auxs (bool global) {
  tree a= (tree) buf->data->aux;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    v << a[i][0]->label;
  merge_sort (v);
  return v;
}

tree
edit_typeset_rep::get_att (string key) {
  return buf->data->att[key];
}

void
edit_typeset_rep::set_att (string key, tree im) {
  buf->data->att (key)= im;
}

void
edit_typeset_rep::reset_att (string key) {
  buf->data->att->reset (key);
}

array<string>
edit_typeset_rep::list_atts (bool global) {
  tree a= (tree) buf->data->att;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    v << a[i][0]->label;
  merge_sort (v);
  return v;
}

void
edit_typeset_rep::set_init (hashmap<string,tree> H) {
  init= hashmap<string,tree> (UNINIT);
  add_init (H);
}

void
edit_typeset_rep::add_init (hashmap<string,tree> H) {
  init->join (H);
  ttt->br->notify_assign (path (), subtree (et, rp));
  notify_change (THE_ENVIRONMENT);
}

void
edit_typeset_rep::clear_local_info () {
  buf->data->ref= hashmap<string,tree> ();
  buf->data->aux= hashmap<string,tree> ();
}

/******************************************************************************
* Miscellaneous routines for lengths arithmetic (should be elsewhere)
******************************************************************************/

SI
edit_typeset_rep::as_length (string l) {
  return env->as_length (l); }

string
edit_typeset_rep::add_lengths (string l1, string l2) {
  return env->add_lengths (l1, l2); }

string
edit_typeset_rep::sub_lengths (string l1, string l2) {
  return env->sub_lengths (l1, l2); }

string
edit_typeset_rep::max_lengths (string l1, string l2) {
  return env->max_lengths (l1, l2); }

string
edit_typeset_rep::min_lengths (string l1, string l2) {
  return env->min_lengths (l1, l2); }

string
edit_typeset_rep::multiply_length (double x, string l) {
  return env->multiply_length (x, l); }

bool
edit_typeset_rep::is_length (string s) {
  return env->is_length (s); }

double
edit_typeset_rep::divide_lengths (string l1, string l2) {
  return env->divide_lengths (l1, l2); }

/******************************************************************************
* Processing preamble
******************************************************************************/

void
use_modules (tree t) {
  if (is_tuple (t))
    for (int i=0; i<N(t); i++) {
      string s= as_string (t[i]);
      if (starts (s, "(")) eval ("(use-modules " * s * ")");
      else if (s != "") eval ("(plugin-initialize '" * s * ")");
    }
}

void
edit_typeset_rep::typeset_style_use_cache (tree style) {
  style= preprocess_style (style, buf->buf->master);
  //cout << "Typesetting style using cache " << style << LF;
  bool ok;
  hashmap<string,tree> H;
  tree t;
  style_get_cache (style, H, t, ok);
  if (ok) {
    env->patch_env (H);
    ok= drd->set_locals (t);
    drd->set_environment (H);
  }
  if (!ok) {
    //cout << "Typeset without cache " << style << LF;
    if (!is_tuple (style)) FAILED ("tuple expected as style");
    H= get_style_env (style);
    drd= get_style_drd (style);
    style_set_cache (style, H, drd->get_locals ());
    env->patch_env (H);
    drd->set_environment (H);
  }
  use_modules (env->read (THE_MODULES));
}

void
edit_typeset_rep::typeset_preamble () {
  env->write_default_env ();
  typeset_style_use_cache (the_style);
  env->update ();
  env->read_env (stydef);
  env->patch_env (init);
  env->update ();
  env->read_env (pre);
  drd->heuristic_init (pre);
}

void
edit_typeset_rep::typeset_prepare () {
  env->base_file_name= buf->buf->master;
  env->read_only= buf->buf->read_only;
  env->write_default_env ();
  env->patch_env (pre);
  env->style_init_env ();
  env->update ();
}

void
edit_typeset_rep::init_update () {
}

void
edit_typeset_rep::drd_update () {
  typeset_exec_until (tp);
  drd->heuristic_init (cur[tp]);
}

#ifdef EXPERIMENTAL
void
edit_typeset_rep::environment_update () {
  hashmap<string,tree> h;
  typeset_prepare ();
  env->assign ("base-file-name", as_string (env->base_file_name));
  env->assign ("cur-file-name", as_string (env->cur_file_name));
  env->assign ("secure", bool_as_tree (env->secure));
  env->read_env (h);
  ::primitive (ste, h);
}
#endif

/******************************************************************************
* Routines for getting information
******************************************************************************/

void
edit_typeset_rep::typeset_invalidate_env () {
  cur= hashmap<path,hashmap<string,tree> > (hashmap<string,tree> (UNINIT));
}

static void
restricted_exec (edit_env env, tree t, int end) {
  if (is_func (t, ASSIGN, 2) && end == 2)
    env->exec (t);
  else if (is_document (t) || is_concat (t))
    for (int i=0; i < min (end, 10); i++)
      restricted_exec (env, t[i], arity (t[i]));
  else if (is_compound (t, "hide-preamble", 1) ||
           is_compound (t, "show-preamble", 1))
    env->exec (t[0]);
  else if (is_compound (t, "script-input", 4) && end == 2) {
    if (env->read (MODE) == "text") {
      env->write (MODE, "prog");
      env->write (PROG_LANGUAGE, t[0]);
    }
  }
}

static tree
filter_format (tree fm, int i, int n) {
  array<tree> r;
  for (int k=0; k<N(fm); k++)
    if (is_func (fm[k], CWITH) && N(fm[k]) >= 4 &&
        is_int (fm[k][0]) && is_int (fm[k][1])) {
      int j1= as_int (fm[k][0]->label);
      int j2= as_int (fm[k][1]->label);
      if (j1 > 0) j1--; else j1 += n;
      if (j2 > 0) j2--; else j2 += n;
      if (i >= j1 && i <= j2) r << fm[k] (2, N(fm[k]));
    }
  return tree (TFORMAT, r);
}

static void
table_descend (tree& t, path& p, tree& fm) {
  while (!is_nil (p)) {
    if (L(t) == TFORMAT && p->item == N(t) - 1) {
      array<tree> r;
      for (int k=0; k<N(t)-1; k++)
        if (is_func (t[k], CWITH, 6) &&
            is_atomic (t[k][4]) &&
            !starts (t[k][4]->label, "cell-"))
          r << t[k];
      fm= tree (TFORMAT, r);
      t= t[N(t)-1];
      p= p->next;
    }
    else if ((L(t) == TABLE || L(t) == ROW) &&
             p->item >= 0 && p->item < N(t)) {
      fm= filter_format (fm, p->item, N(t));
      t= t[p->item];
      p= p->next;
    }
    else break;
  }
}

static void
define_style_macros (edit_env& env, tree t) {
  if (is_document (t) || is_concat (t)) {
    int i, n=N(t);
    for (i=0; i<n; i++)
      define_style_macros (env, t[i]);
  }
  else if (is_func (t, ASSIGN, 2) && is_atomic (t[0]))
    env->write (t[0]->label, t[1]);
}

void
edit_typeset_rep::typeset_exec_until (path p) {
  // FIXME: we should ensure that p is inside the document
  // if (!(rp <= p)) p= correct_cursor (et, rp * 0);

  //time_t t1= texmacs_time ();
  if (has_changed (THE_TREE + THE_ENVIRONMENT))
    if (p != correct_cursor (et, rp * 0)) {
      if (DEBUG_STD) std_warning << "resynchronizing for path " << p << "\n";
      // apply_changes ();
    }
  if (p == tp && inside_graphics (true) && p != closest_inside (et, p)) {
    //cout << "ATHENA] Warning: corrected cursor\n";
    tp= closest_inside (et, tp);
    p = tp;
  }

  //cout << "Exec until " << p << LF;
  if (N(cur[p])!=0) return;
  if (N(cur)>=25) // avoids out of memory in weird cases
    typeset_invalidate_env ();
  typeset_prepare ();
  if (!(rp <= p)) {
    failed_error << "Erroneous path " << p << "\n";
    FAILED ("invalid typesetting path");
  }
  if (rp < p) {
    tree t= subtree (et, rp);
    path q= path_up (p / rp);
    while (!is_nil (q)) {
      int i= q->item;
      restricted_exec (env, t, i);
      if (L(t) == TFORMAT && i == N(t) - 1) {
        tree fm= tree (TFORMAT);
        table_descend (t, q, fm);
        if (!is_nil (q))
          for (int k=0; k<N(fm); k++)
            if (is_func (fm[k], CWITH, 2))
              env->write (fm[k][0]->label, fm[k][1]);
      }
      else {
        tree w= drd->get_env_child (t, i, tree (ATTR));
        if (w == "") break;
        //cout << "t= " << t << "\n";
        //cout << "i= " << i << "\n";
        //cout << "w= " << w << "\n";
        tree ww (w, N(w));
        for (int j=0; j<N(w); j+=2) {
          //cout << w[j] << " := " << env->exec (w[j+1]) << "\n";
          ww[j+1]= env->exec (w[j+1]);
        }
        for (int j=0; j<N(w); j+=2)
          env->write (w[j]->label, ww[j+1]);
        t= t[i];
        q= q->next;
      }
    }
  }
  if (env->read (PREAMBLE) == "true")
    env->write (MODE, "src");
  if (env->read (MODE) == "src" && env->read (PREAMBLE) != "true")
    define_style_macros (env, subtree (et, rp));
  env->read_env (cur (p));
  //time_t t2= texmacs_time ();
  //if (t2 - t1 >= 10) cout << "typeset_exec_until took " << t2-t1 << "ms\n";
}

tree
edit_typeset_rep::get_full_env () {
  typeset_exec_until (tp);
  return (tree) cur[tp];
}

bool
edit_typeset_rep::defined_at_cursor (string var) {
  typeset_exec_until (tp);
  return cur[tp]->contains (var);
}

tree
edit_typeset_rep::get_env_value (string var, path p) {
  typeset_exec_until (p);
  tree t= cur[p][var];
  return is_func (t, BACKUP, 2)? t[0]: t;
}

tree
edit_typeset_rep::get_env_value (string var) {
 /* FIXME: tp is wrong (and consequently, crashes TeXmacs)
  *   when we call this routine from inside the code which
  *   is triggered by a button, for example.
  *
  * Test: fire TeXmacs, then open a new Graphics, then click
  *   on the icon for going in spline mode. Then it crashes,
  *   because we call (get-env-tree) from inside the Scheme.
  *   If we call (get-env-tree-at ... (cDr (cursor-path))),
  *   then it works.
  */
  return get_env_value (var, tp);
}

bool
edit_typeset_rep::defined_at_init (string var) {
  if (init->contains (var)) return true;
  if (N(pre)==0) typeset_preamble ();
  return pre->contains (var);
}

bool
edit_typeset_rep::defined_in_init (string var) {
  return init->contains (var);
}

tree
edit_typeset_rep::get_init_value (string var) {
  if (init->contains (var)) {
    tree t= init [var];
    if (var == BG_COLOR && is_func (t, _PATTERN)) t= env->exec (t);
    return is_func (t, BACKUP, 2)? t[0]: t;
  }
  if (N(pre)==0) typeset_preamble ();
  tree t= pre [var];
  if (var == BG_COLOR && is_func (t, _PATTERN)) t= env->exec (t);
  return is_func (t, BACKUP, 2)? t[0]: t;
}

string
edit_typeset_rep::get_env_string (string var) {
  return as_string (get_env_value (var));
}

string
edit_typeset_rep::get_init_string (string var) {
  return as_string (get_init_value (var));
}

int
edit_typeset_rep::get_env_int (string var) {
  return as_int (get_env_value (var));
}

int
edit_typeset_rep::get_init_int (string var) {
  return as_int (get_init_value (var));
}

double
edit_typeset_rep::get_env_double (string var) {
  return as_double (get_env_value (var));
}

double
edit_typeset_rep::get_init_double (string var) {
  return as_double (get_init_value (var));
}

color
edit_typeset_rep::get_env_color (string var) {
  return named_color (as_string (get_env_value (var)));
}

color
edit_typeset_rep::get_init_color (string var) {
  return named_color (as_string (get_init_value (var)));
}

language
edit_typeset_rep::get_env_language () {
  string mode= get_env_string (MODE);
  if (mode == "text")
    return text_language (get_env_string (LANGUAGE));
  else if (mode == "math")
    return math_language (get_env_string (MATH_LANGUAGE));
  else return prog_language (get_env_string (PROG_LANGUAGE));
}

int
edit_typeset_rep::get_page_count () {
  return N (eb[0]);
}

SI
edit_typeset_rep::get_page_width (bool deco) {
  (void) get_env_string (PAGE_WIDTH);
  return (env->get_page_width (deco) + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_pages_width (bool deco) {
  (void) get_env_string (PAGE_WIDTH);
  return (env->get_pages_width (deco) + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_page_height (bool deco) {
  (void) get_env_string (PAGE_HEIGHT);
  return (env->get_page_height (deco) + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_total_width (bool deco) {
  SI w= eb->w();
  if (!deco) {
    SI w1= env->get_pages_width (false);
    SI w2= env->get_pages_width (true);
    w -= (w2 - w1);
  }
  return (w + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_total_height (bool deco) {
  SI h= eb->h();
  if (!deco) {
    SI h1= env->get_page_height (false);
    SI h2= env->get_page_height (true);
    int nr= get_page_count ();
    int nx= env->page_packet;
    int ny= ((nr + env->page_offset) + nx - 1) / nx;
    h -= ny * (h2 - h1);
  }
  return (h + std_shrinkf - 1) / std_shrinkf;
}

/******************************************************************************
* Execution without typesetting
******************************************************************************/

static tree
simplify_execed (tree t) {
  if (is_atomic (t)) return t;
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++)
    r[i]= simplify_execed (t[i]);
  if (is_func (r, QUOTE, 1) && is_atomic (r[0]))
    return r[0];
  else return r;
}

static tree
expand_references (tree t, hashmap<string,tree> h) {
  if (is_atomic (t)) return t;
  if (is_func (t, REFERENCE, 1) || is_func (t, PAGEREF)) {
    string ref= as_string (simplify_execed (t[0]));
    if (h->contains (ref)) {
      int which= is_func (t, REFERENCE, 1)? 0: 1;
      return tree (HLINK, copy (h[ref][which]), "#" * ref);
    }
    return tree (HLINK, "?", "#" * ref);
  }
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++)
    r[i]= expand_references (t[i], h);
  return r;  
}

static void
prefix_specific (hashmap<string,tree>& H, string prefix) {
  hashmap<string,tree> R;
  iterator<string> it = iterate (H);
  while (it->busy()) {
    string key= it->next ();
    string var= prefix * key;
    if (H->contains (var)) R(key)= H[var];
    else R(key)= H[key];
  }
  H= R;
}

tree
edit_typeset_rep::exec (tree t, hashmap<string,tree> H, bool expand_refs) {
  hashmap<string,tree> H2;
  env->read_env (H2);
  env->write_env (H);
  t= env->exec (t);
  if (expand_refs)
    t= expand_references (t, buf->data->ref);
  t= simplify_execed (t);
  t= simplify_correct (t);
  env->write_env (H2);
  return t;
}

tree
edit_typeset_rep::exec_texmacs (tree t, path p) {
  typeset_exec_until (p);
  return exec (t, cur[p]);
}

tree
edit_typeset_rep::exec_texmacs (tree t) {
  return exec_texmacs (t, rp * 0);
}

tree
edit_typeset_rep::exec_verbatim (tree t, path p) {
  t= convert_OTS1_symbols_to_universal_encoding (t);
  typeset_exec_until (p);
  hashmap<string,tree> H= copy (cur[p]);
  H ("TeXmacs")= tree (MACRO, "TeXmacs");
  H ("LaTeX")= tree (MACRO, "LaTeX");
  H ("TeX")= tree (MACRO, "TeX");
  return exec (t, H);
}

tree
edit_typeset_rep::exec_verbatim (tree t) {
  return exec_verbatim (t, rp * 0);
}

static tree
search_doc_title (tree t) {
  if (is_atomic (t)) return "";
  else if (is_compound (t, "doc-title", 1)) return t[0];
  else {
    for (int i=0; i<N(t); i++) {
      tree r= search_doc_title (t[i]);
      if (r != "") return r;
    }
    return "";
  }
}

tree
edit_typeset_rep::exec_html (tree t, path p) {
  t= convert_OTS1_symbols_to_universal_encoding (t);
  if (p == (rp * 0)) typeset_preamble ();
  typeset_exec_until (p);
  hashmap<string,tree> H= copy (cur[p]);
  tree patch= as_tree (eval ("(stree->tree (tmhtml-env-patch))"));
  hashmap<string,tree> P (UNINIT, patch);
  H->join (P);
  prefix_specific (H, "tmhtml-");
  tree w (WITH);
  tree doc_title= search_doc_title (t);
  if (doc_title != "")
    w << string ("html-doc-title") << doc_title;
  if (H->contains ("html-title"))
    w << string ("html-title") << H["html-title"];
  if (H->contains ("html-css"))
    w << string ("html-css") << H["html-css"];
  if (H->contains ("html-head-javascript"))
    w << string ("html-head-javascript") << H["html-head-javascript"];
  if (H->contains ("html-head-javascript-src"))
    w << string ("html-head-javascript-src") << H["html-head-javascript-src"];
  if (H->contains ("html-head-favicon"))
    w << string ("html-head-favicon") << H["html-head-favicon"];
  if (H->contains ("html-extra-css"))
    w << string ("html-extra-css") << H["html-extra-css"];
  if (H->contains ("html-extra-javascript-src"))
    w << string ("html-extra-javascript-src")
      << H["html-extra-javascript-src"];
  if (H->contains ("html-extra-javascript"))
    w << string ("html-extra-javascript") << H["html-extra-javascript"];
  if (H->contains ("html-site-version"))
    w << string ("html-site-version") << H["html-site-version"];
  if (N(w) == 0) return exec (t, H);
  else {
    w << t;
    return exec (w, H);
  }
  //tree r= exec (t, H);
  //cout << "In: " << t << "\n";
  //cout << "Out: " << r << "\n";
  //return r;
}

tree
edit_typeset_rep::exec_html (tree t) {
  return exec_html (t, rp * 0);
}

static tree
value_to_compound (tree t, hashmap<string,tree> h) {
  if (is_atomic (t)) return t;
  else if (is_func (t, VALUE, 1) &&
	   is_atomic (t[0]) &&
	   h->contains (t[0]->label))
    return compound (t[0]->label);
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= value_to_compound (t[i], h);
    return r;
  }
}

tree
edit_typeset_rep::exec_latex (tree t, path p) {
  t= convert_OTS1_symbols_to_universal_encoding (t);
  bool expand_unknown_macros= "on" == as_string (
      call ("get-preference", "texmacs->latex:expand-macros"));
  bool expand_user_macro= "on" == as_string (
      call ("get-preference", "texmacs->latex:expand-user-macros"));
  if (!expand_unknown_macros && !expand_user_macro)
    return t;
  if (p == (rp * 0)) typeset_preamble ();
  typeset_exec_until (p);
  hashmap<string,tree> H= copy (cur[p]);
  object l= null_object ();
  iterator<string> it= iterate (H);
  while (it->busy ()) l= cons (object (it->next ()), l);
  tree patch= as_tree (call ("stree->tree", call ("tmtex-env-patch", t, l)));
  hashmap<string,tree> P (UNINIT, patch);
  H->join (P);
  prefix_specific (H, "tmlatex-");

  if (!expand_user_macro &&
      is_document (t) && is_compound (t[0], "hide-preamble")) {
    tree r= copy (t);
    r[0]= "";
    r= exec (value_to_compound (r, P), H, false);
    r[0]= exec (t[0], H, false);
    return r;
  }
  else {
    tree r= exec (value_to_compound (t, P), H, false);
    return r;
  }
}

tree
edit_typeset_rep::exec_latex (tree t) {
  return exec_latex (t, rp * 0);
}

tree
edit_typeset_rep::texmacs_exec (tree t) {
  return ::texmacs_exec (env, t);
}

tree
edit_typeset_rep::var_texmacs_exec (tree t) {
  typeset_exec_until (tp);
  env->write_env (cur[tp]);
  env->update_frame ();
  return texmacs_exec (t);
}

/******************************************************************************
* Wrappers for editing animations
******************************************************************************/

tree
edit_typeset_rep::checkout_animation (tree t) {
  path p= search_upwards (ANIM_STATIC);
  if (is_nil (p)) p= search_upwards (ANIM_DYNAMIC);
  if (is_nil (p)) p= search_upwards ("anim-edit");
  if (!is_nil (p)) {
    typeset_exec_until (p);
    env->write_env (cur[p]);
  }
  return env->checkout_animation (t);
}

tree
edit_typeset_rep::commit_animation (tree t) {
  path p= search_upwards ("anim-edit");
  if (is_nil (p)) p= search_upwards (ANIM_STATIC);
  if (is_nil (p)) p= search_upwards (ANIM_DYNAMIC);
  if (!is_nil (p)) {
    typeset_exec_until (p);
    env->write_env (cur[p]);
  }
  return env->commit_animation (t);
}

/******************************************************************************
* Initialization
******************************************************************************/

void
edit_typeset_rep::change_style (tree t) {
  bool changed= (the_style != t);
  the_style= copy (t);
  if (changed) {
    require_save ();
    notify_change (THE_ENVIRONMENT);
  }
}

void
edit_typeset_rep::init_style () {
  notify_change (THE_ENVIRONMENT);
}

void
edit_typeset_rep::init_style (string name) {
  if ((name == "none") || (name == "") || (name == "style")) the_style= TUPLE;
  else if (arity (the_style) == 0) the_style= tree (TUPLE, name);
  else the_style= tree (TUPLE, name) * the_style (1, N(the_style));
  require_save ();
  notify_change (THE_ENVIRONMENT);
}

tree
edit_typeset_rep::get_init_all () {
  return (tree) init;
}

void
edit_typeset_rep::init_env (string var, tree by) {
  if (init (var) == by) return;
  init (var)= by;
  if (var == "full-screen-mode") return;
  if (var != PAGE_SCREEN_WIDTH &&
      var != PAGE_SCREEN_HEIGHT &&
      var != ZOOM_FACTOR)
    require_save ();
  notify_change (THE_ENVIRONMENT);
}

void
edit_typeset_rep::init_default (string var) {
  if (!init->contains (var)) return;
  init->reset (var);
  if (stydef->contains (var)) pre(var)= stydef[var];
  else pre->reset (var);
  notify_change (THE_ENVIRONMENT);
}

/******************************************************************************
* Actual typesetting
******************************************************************************/

static int
heading_level (tree t) {
  return athena_heading_level (t);
}

static bool
heading_fold_container (tree t) {
  return is_func (t, DOCUMENT) ||
         is_func (t, CONCAT) ||
         is_compound (t, "ignore") ||
         is_compound (t, "show-part") ||
         is_compound (t, "hide-part");
}

static void
heading_collect_paths (tree t, path p, array<path>& paths) {
  if (is_atomic (t)) return;
  if (heading_level (t) != 0) {
    paths << p;
    return;
  }
  for (int i=0; i<N(t); i++)
    heading_collect_paths (t[i], p * i, paths);
}

static bool
heading_path_starts_with (path p, path prefix) {
  if (is_nil (prefix)) return true;
  if (is_nil (p)) return false;
  return p->item == prefix->item &&
         heading_path_starts_with (p->next, prefix->next);
}

static bool
heading_path_before_or_contains (path heading, path cursor) {
  if (is_nil (heading)) return true;
  if (is_nil (cursor)) return false;
  if (heading->item < cursor->item) return true;
  if (heading->item > cursor->item) return false;
  return heading_path_before_or_contains (heading->next, cursor->next);
}

static bool
heading_find_for_cursor (tree doc, path cursor, path& heading) {
  for (path p= cursor; !is_nil (p); p= path_up (p))
    if (has_subtree (doc, p) && heading_level (subtree (doc, p)) != 0) {
      heading= p;
      return true;
    }

  array<path> headings;
  heading_collect_paths (doc, path (), headings);

  bool found= false;
  for (int i=0; i<N(headings); i++)
    if (heading_path_before_or_contains (headings[i], cursor)) {
      heading= headings[i];
      found= true;
    }
    else break;

  return found && has_subtree (doc, heading) &&
         heading_level (subtree (doc, heading)) != 0;
}

static tree
heading_fold_hidden () {
  return compound ("folded-hidden");
}

static tree
heading_fold_screen_tree (tree t, path p, hashset<string> folded,
                          hashset<string> folded_tocs,
                          hashset<string> unfolded_tocs,
                          bool fold_tocs_by_default) {
  if (is_atomic (t)) return t;
  if (toc_fold_node (t)) {
    string key= as_string (p);
    tree_label label= L(t);
    if (folded_tocs->contains (key) ||
        (fold_tocs_by_default && !unfolded_tocs->contains (key)))
      label= make_tree_label (is_compound (t, "table-of-contents*")
                               ? "screen-folded-table-of-contents*"
                               : "screen-folded-table-of-contents");
    else
      label= make_tree_label (is_compound (t, "table-of-contents*")
                               ? "screen-unfolded-table-of-contents*"
                               : "screen-unfolded-table-of-contents");
    if (label != L(t)) {
      tree r (label, N(t));
      for (int i=0; i<N(t); i++) r[i]= t[i];
      return r;
    }
    return t;
  }
  if (!heading_fold_container (t)) return t;

  tree r (t, N(t));
  int folded_level= 0;
  for (int i=0; i<N(t); i++) {
    path cp= p * i;
    int level= heading_level (t[i]);
    bool folded_here= level != 0 && folded->contains (as_string (cp));

    if (folded_level != 0) {
      if (level != 0 && level <= folded_level) folded_level= 0;
      else {
        r[i]= heading_fold_hidden ();
        continue;
      }
    }

    r[i]= heading_fold_screen_tree (t[i], cp, folded,
                                    folded_tocs, unfolded_tocs,
                                    fold_tocs_by_default);
    if (folded_here)
      folded_level= level;
  }
  return r;
}

tree
edit_typeset_rep::folded_screen_tree () {
  tree doc= subtree (et, rp);
  bool fold_tocs_by_default=
    env->get_string (PAGE_MEDIUM) == "automatic" &&
    get_preference ("fold table of contents in reflow", "on") == "on";
  return heading_fold_screen_tree (doc, path (), folded_headings,
                                   folded_tocs, unfolded_tocs,
                                   fold_tocs_by_default);
}

bool
edit_typeset_rep::heading_fold_toggle () {
  return heading_fold_set_current (false, true);
}

bool
edit_typeset_rep::heading_fold_current () {
  return heading_fold_set_current (true, false);
}

bool
edit_typeset_rep::heading_unfold_current () {
  return heading_fold_set_current (false, false);
}

bool
edit_typeset_rep::heading_fold_toggle_at (string p) {
  path hp= as_path (p);
  tree doc= subtree (et, rp);
  if (!has_subtree (doc, hp) || heading_level (subtree (doc, hp)) == 0) {
    set_message ("No heading at path", p);
    return false;
  }

  string key= as_string (hp);
  if (folded_headings->contains (key)) {
    folded_headings->remove (key);
    set_message ("Heading unfolded", "");
  }
  else {
    folded_headings->insert (key);
    set_message ("Heading folded", "");
  }
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
  return true;
}

void
edit_typeset_rep::heading_unfold_all () {
  folded_headings= hashset<string> ();
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
}

array<heading_cell_range>
edit_typeset_rep::heading_cell_ranges () {
  array<heading_cell_range> ranges;
  tree doc= subtree (et, rp);
  array<path> headings;
  heading_collect_paths (doc, path (), headings);

  int hidden_level= 0;
  for (int i=0; i<N(headings); i++) {
    path hp= headings[i];
    int level= heading_level (subtree (doc, hp));
    if (hidden_level != 0) {
      if (level <= hidden_level) hidden_level= 0;
      else continue;
    }

    bool folded_here= folded_headings->contains (as_string (hp));
    int next= i + 1;
    while (next < N(headings) &&
           heading_level (subtree (doc, headings[next])) > level)
      next++;

    path absolute= rp * hp;
    path logical_end= next < N(headings)
      ? start (et, rp * headings[next])
      : end (et, rp);

    heading_cell_range range;
    range.level          = level;
    range.heading_path   = hp;
    range.selection_start= start (et, absolute);
    range.selection_end  = logical_end;
    range.heading_end    = end (et, absolute);
    range.visual_start   = range.selection_start;
    range.visual_end     = folded_here ? range.heading_end : logical_end;
    range.folded         = folded_here;
    ranges << range;

    if (folded_here) hidden_level= level;
  }
  return ranges;
}

bool
edit_typeset_rep::heading_fold_set_current (bool folded, bool toggle) {
  tree doc= subtree (et, rp);
  if (!heading_path_starts_with (tp, rp)) {
    set_message ("No heading at cursor", "");
    return false;
  }

  path hp;
  if (!heading_find_for_cursor (doc, tp / rp, hp)) {
    set_message ("No heading at cursor", "");
    return false;
  }

  string key= as_string (hp);
  bool was_folded= folded_headings->contains (key);
  bool now_folded= toggle? !was_folded: folded;
  if (now_folded) folded_headings->insert (key);
  else folded_headings->remove (key);
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
  set_message (now_folded? "Heading folded": "Heading unfolded", "");
  return true;
}

static bool
toc_fold_find_for_path (tree doc, path p, path& toc) {
  for (path q= p; !is_nil (q); q= path_up (q))
    if (has_subtree (doc, q) && toc_fold_node (subtree (doc, q))) {
      toc= q;
      return true;
    }
  if (!toc_fold_node (doc)) return false;
  toc= path ();
  return true;
}

bool
edit_typeset_rep::toc_fold_set_at (path p, bool folded) {
  tree doc= subtree (et, rp);
  if (!heading_path_starts_with (p, rp)) return false;
  path toc;
  if (!toc_fold_find_for_path (doc, p / rp, toc)) return false;
  string key= as_string (toc);
  if (folded) {
    folded_tocs->insert (key);
    unfolded_tocs->remove (key);
  }
  else {
    unfolded_tocs->insert (key);
    folded_tocs->remove (key);
  }
  notify_change (THE_FREEZE);
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
  set_message (folded? "Table of contents folded":
                        "Table of contents unfolded", "");
  return true;
}

int
edit_typeset_rep::heading_word_count_at (path p) {
  if (!has_subtree (et, rp)) return 0;
  tree doc= subtree (et, rp);

  if (!heading_path_starts_with (p, rp)) return 0;
  path hp;
  if (!heading_find_for_cursor (doc, p / rp, hp)) return 0;
  hp= rp * hp;

  int h= hash (doc);
  if (h != heading_word_count_cache_hash) {
    heading_word_count_cache= athena_heading_word_count_entries (doc, rp);
    heading_word_count_cache_map= hashmap<path,int> (0);
    for (int i=0; i<N(heading_word_count_cache); i++)
      heading_word_count_cache_map (
        heading_word_count_cache[i].tree_path)=
          heading_word_count_cache[i].words;
    heading_word_count_cache_hash= h;
  }

  return heading_word_count_cache_map[hp];
}

void
edit_typeset_rep::typeset_sub (SI& x1, SI& y1, SI& x2, SI& y2) {
  //time_t t1= texmacs_time ();
  typeset_prepare ();
  eb= empty_box (reverse (rp));
  // saves memory, also necessary for change_log update
  bench_start ("typeset");
#ifdef USE_EXCEPTIONS
  try {
#endif
    bool printed= env->get_string (PAGE_PRINTED) == "true";
    ttt->progressive= !printed &&
      env->get_string (PAGE_MEDIUM) != "paper";
    path relative_cursor= rp <= tp? tp / rp: path ();
    ttt->progressive_required=
      is_nil (relative_cursor)? 0: max (0, relative_cursor->item);
    bool progressive_was_pending= progressive_typeset_pending;
    ttt->progressive_advance=
      !progressive_was_pending || progressive_typeset_continue;
    ttt->progressive_budget_ms= progressive_was_pending? 12: 0;
    if (progressive_typeset_continue) {
      ttt->br->status= CORRUPTED;
      progressive_typeset_continue= false;
    }
    bool folded_screen= !printed &&
      (fold_view_has_toc || N(folded_headings) != 0 ||
       N(folded_tocs) != 0 || N(unfolded_tocs) != 0);
    bool full_repaint= fold_view_rebuild ||
                       folded_screen != fold_view_active;
    if (full_repaint) {
      tree doc= subtree (et, rp);
      if (folded_screen)
        doc= heading_fold_screen_tree (doc, path (), folded_headings,
                                      folded_tocs, unfolded_tocs,
                                      env->get_string (PAGE_MEDIUM) == "automatic" &&
                                      get_preference (
                                        "fold table of contents in reflow",
                                        "on") == "on");
      ttt->screen_tree= folded_screen;
      ::notify_assign (ttt, path (), doc);
      fold_view_rebuild= false;
      fold_view_active= folded_screen;
    }
    eb= ::typeset (ttt, x1, y1, x2, y2);
    progressive_typeset_pending=
      ttt->progressive_root_active && ttt->progressive_pending;
    if (full_repaint) {
      SI big= (SI) (1 << 30);
      x1= -big; y1= -big; x2= big; y2= big;
    }
#ifdef USE_EXCEPTIONS
  }
  catch (string msg) {
    the_exception= msg;
    std_error << "Typesetting failure, resetting to empty document\n";
    assign (rp, tree (DOCUMENT, ""));
    ttt->screen_tree= false;
    ::notify_assign (ttt, path(), subtree (et, rp));
    eb= ::typeset (ttt, x1, y1, x2, y2);    
  }
  handle_exceptions ();
#endif
  bench_end ("typeset");
  //time_t t2= texmacs_time ();
  //if (t2 - t1 >= 10) cout << "typeset took " << t2-t1 << "ms\n";
  picture_cache_clean ();
}

static void
report_missing (hashmap<string,tree> missing) {
  array<string> a;
  for (iterator<string> it= iterate (missing); it->busy(); a << it->next ()) {}
  merge_sort (a);
  for (int i=0; i<N(a); i++)
    if (!starts (a[i], "bib-"))
      typeset_warning << "Undefined reference " << a[i] << LF;
}

static void
report_redefined (array<tree> redefined) {
  for (int i=0; i<N(redefined); i++) {
    tree t= redefined[i];
    if (t[1]->label == "")
      typeset_warning << "Redefined " << t[0]->label << LF;
    else
      typeset_warning << "Redefined " << t[0]->label
                      << " as " << t[1]->label << LF;
  }
}

static void
clean_unused (hashmap<string,tree>& refs, hashmap<string,bool> used) {
  array<string> a;
  for (iterator<string> it= iterate (refs); it->busy(); ) {
    string key= it->next ();
    if (!used->contains (key)) a << key;
  }
  for (int i=0; i<N(a); i++)
    refs->reset (a[i]);
}

void
edit_typeset_rep::typeset (SI& x1, SI& y1, SI& x2, SI& y2) {
  int missing_nr= INT_MAX;
  int redefined_nr= INT_MAX;
  int page_ref_updates= 0;
  x1= MAX_SI; y1= MAX_SI; x2= MIN_SI; y2= MIN_SI;
  while (true) {
    SI sx1, sy1, sx2, sy2;
    typeset_sub (sx1, sy1, sx2, sy2);
    bool page_refs_changed= env->page_refs_changed;
    x1= min (x1, sx1); y1= min (y1, sy1);
    x2= max (x2, sx2); y2= max (y2, sy2);
    if (!env->complete) break;
    env->complete= false;
    clean_unused (env->local_ref, env->touched);
    if (N(env->missing) == 0 && N(env->redefined) == 0 &&
        !page_refs_changed)
      break;
    if (page_refs_changed) {
      page_ref_updates++;
      if (page_ref_updates > 5) {
        typeset_warning << "Page references did not stabilize" << LF;
        break;
      }
    }
    if (!page_refs_changed &&
        ((N(env->missing) == missing_nr && N(env->redefined) == redefined_nr) ||
         (N(env->missing) > missing_nr || N(env->redefined) > redefined_nr))) {
      report_missing (env->missing);
      report_redefined (env->redefined);
      break;
    }
    missing_nr= N(env->missing);
    redefined_nr= N(env->redefined);
    ttt->br->notify_assign (path (), ttt->br->st);
  }
}

void
edit_typeset_rep::typeset_forced () {
  //cout << "Typeset forced\n";
  SI x1, y1, x2, y2;
  typeset (x1, y1, x2, y2);
}

void
edit_typeset_rep::typeset_invalidate (path p) {
  if (rp <= p) {
    //cout << "Invalidate " << p << "\n";
    if (!fold_view_has_toc && toc_fold_contains (subtree (et, p)))
      fold_view_has_toc= true;
    if (fold_view_active || fold_view_has_toc) fold_view_rebuild= true;
    notify_change (THE_TREE);
    ::notify_assign (ttt, p / rp, subtree (et, p));
  }
}

void
edit_typeset_rep::typeset_invalidate_all () {
  //cout << "Invalidate all\n";
  heading_word_count_cache_hash= INT_MIN;
  heading_word_count_cache_map= hashmap<path,int> (0);
  if (!fold_view_has_toc)
    fold_view_has_toc= toc_fold_contains (subtree (et, rp));
  if (fold_view_active || fold_view_has_toc || N(folded_headings) != 0 ||
      N(folded_tocs) != 0 || N(unfolded_tocs) != 0)
    fold_view_rebuild= true;
  notify_change (THE_ENVIRONMENT);
  typeset_preamble ();
  ttt->br->notify_assign (path (), subtree (et, rp));
}

void
edit_typeset_rep::typeset_invalidate_players (path p, bool reattach) {
  if (rp <= p) {
    tree t= subtree (et, p);
    blackbox bb;
    bool ok= t->obs->get_contents (ADDENDUM_PLAYER, bb);
    if (ok) {
      if (reattach) tree_addendum_delete (t, ADDENDUM_PLAYER);
      typeset_invalidate (p);
    }
    if (is_compound (t)) {
      int i, n= N(t);
      for (i=0; i<n; i++)
        typeset_invalidate_players (p * i, reattach);
    }
  }
}
