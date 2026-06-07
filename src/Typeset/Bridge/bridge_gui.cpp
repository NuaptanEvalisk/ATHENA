
/******************************************************************************
* MODULE     : bridge_with.cpp
* DESCRIPTION: Bridge for GUI markup
* COPYRIGHT  : (C) 2007  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "bridge.hpp"
#include "analyze.hpp"
#include "Concat/canvas_properties.hpp"
#include "Line/lazy_paragraph.hpp"

/******************************************************************************
* Abstract bridge for ornaments
******************************************************************************/

class bridge_ornamented_rep: public bridge_rep {
protected:
  bridge body;
  tree   with;
  int    idx;

public:
  bridge_ornamented_rep (typesetter ttt, tree st, path ip, int idx);
  void initialize ();

  void notify_assign (path p, tree u);
  void notify_insert (path p, tree u);
  void notify_remove (path p, int nr);
  bool notify_macro  (int type, string var, int level, path p, tree u);
  void notify_change ();

  void my_exec_until (path p);
  bool my_typeset_will_be_complete ();
  void typeset_ornament_stack (int desired_status,
                               array<page_item>& l2, stack_border& sb2);
  box  typeset_ornament (int desired_status);
  void insert_ornament (box b);
};

bridge_ornamented_rep::bridge_ornamented_rep (
  typesetter ttt, tree st, path ip, int idx2):
  bridge_rep (ttt, st, ip), with (tree (TUPLE)), idx (idx2)
{
  initialize ();
}

void
bridge_ornamented_rep::initialize () {
  if (is_nil(body)) body= make_bridge (ttt, st[idx], descend (ip, idx));
  else replace_bridge (body, st[idx], descend (ip, idx));
}

/******************************************************************************
* Event notification
******************************************************************************/

void
bridge_ornamented_rep::notify_assign (path p, tree u) {
  // cout << "Assign " << p << ", " << u << " in " << st << "\n";
  if (is_nil (p)) {
    st= u;
    initialize ();
  }
  else {
    bool mp_flag= is_multi_paragraph (st);
    if (p->item == idx) {
      if (is_atom (p)) body= make_bridge (ttt, u, descend (ip, idx));
      else body->notify_assign (p->next, u);
      st= substitute (st, p->item, body->st);
    }
    else {
      st= substitute (st, p, u);
      body->notify_change ();
    }
    if (mp_flag != is_multi_paragraph (st)) initialize ();
  }
  status= CORRUPTED;
}

void
bridge_ornamented_rep::notify_insert (path p, tree u) {
  // cout << "Insert " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (is_atom (p) || (p->item != idx)) bridge_rep::notify_insert (p, u);
  else {
    bool mp_flag= is_multi_paragraph (st);
    body->notify_insert (p->next, u);
    st= substitute (st, idx, body->st);
    if (mp_flag != is_multi_paragraph (st)) initialize ();
  }
  status= CORRUPTED;
}

void
bridge_ornamented_rep::notify_remove (path p, int nr) {
  // cout << "Remove " << p << ", " << nr << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (is_atom (p) || (p->item != idx)) bridge_rep::notify_remove (p, nr);
  else {
    bool mp_flag= is_multi_paragraph (st);
    body->notify_remove (p->next, nr);
    st= substitute (st, idx, body->st);
    if (mp_flag != is_multi_paragraph (st)) initialize ();
  }
  status= CORRUPTED;
}

bool
bridge_ornamented_rep::notify_macro (
  int type, string var, int l, path p, tree u)
{
  //cout << "Notify macro " << type << ", " << var << ", " << l
  //     << ", " << p << ", " << u << " in " << st << "\n";
  bool flag= body->notify_macro (type, var, l, p, u);
  flag= flag || env->depends (st, var, l);
  if (flag) status= CORRUPTED;
  return flag;
}

void
bridge_ornamented_rep::notify_change () {
  status= CORRUPTED;
  body->notify_change ();
}

/******************************************************************************
* Typesetting
******************************************************************************/

void
bridge_ornamented_rep::my_exec_until (path p) {
  for (int i=0; i<N(with)-1; i+=2)
    env->monitored_write_update (with[i]->label, with[i+1]);
  body->exec_until (p->next);
}

bool
bridge_ornamented_rep::my_typeset_will_be_complete () {
  if (status != CORRUPTED) return false;
  return body->my_typeset_will_be_complete ();
}

static box
make_ornament_body (path ip, array<page_item> l) {
  int i, n= N(l);
  array<box> lines_bx (n);
  array<SI>  lines_ht (n);
  for (i=0; i<n; i++) {
    page_item item= copy (l[i]);
    lines_bx[i]= item->b;
    lines_ht[i]= item->spc->def;
  }
  box b= stack_box (ip, lines_bx, lines_ht);
  SI dy= n==0? 0: b[0]->y2;
  return move_box (decorate (ip), stack_box (ip, lines_bx, lines_ht), 0, dy);
}

static bool
contains_highlight_box (box b) {
  tree t= (tree) b;
  if (is_tuple (t, "highlight")) return true;
  for (int i=0; i<b->subnr (); i++)
    if (contains_highlight_box (b->subbox (i))) return true;
  return false;
}

static page_item
stack_page_items (path ip, array<page_item> l, int start, int end) {
  int n= end - start + 1;
  array<box> lines_bx (n);
  array<SI> lines_ht (n);
  for (int i=0; i<n; i++) {
    box b= l[start + i]->b;
    lines_bx[i]= resize_box (b->ip, b, b->x1, min (b->y1, b->y3),
                              b->x2, max (b->y2, b->y4));
    lines_ht[i]= l[start + i]->spc->def;
  }
  box b= stack_box (ip, lines_bx, lines_ht);
  SI dy= n == 0? 0: b[0]->y2;
  b= move_box (decorate (ip), b, 0, dy);
  page_item last= l[end];
  return page_item (PAGE_LINE_ITEM, b, space (0), last->penalty,
                    last->fl, last->nr_cols, last->t);
}

static SI
page_item_group_height (page_item item) {
  return item->b->h () + item->spc->def;
}

static void
group_highlight_runs (path ip, array<page_item>& l, SI max_group_height) {
  array<page_item> r;
  for (int i=0; i<N(l); ) {
    if (l[i]->type != PAGE_LINE_ITEM || !contains_highlight_box (l[i]->b)) {
      r << l[i++];
      continue;
    }
    int start= i;
    while (i+1<N(l) && l[i+1]->type == PAGE_LINE_ITEM &&
           contains_highlight_box (l[i+1]->b))
      i++;
    // Keep page breaks available: stacking a multi-page highlight run
    // produces one unbreakable page item.
    SI run_height= 0;
    for (int j=start; j<=i; j++) run_height += page_item_group_height (l[j]);
    if (max_group_height <= 0 || run_height > max_group_height) {
      for (int j=start; j<=i; j++) r << l[j];
    }
    else if (i == start) r << l[start];
    else r << stack_page_items (ip, l, start, i);
    i++;
  }
  l= r;
}

static bool
is_block_background_ornament (ornament_parameters ps, box xb) {
  return is_nil (xb) &&
         ps->bg->get_type () != brush_none &&
         ps->shape == "rectangular" &&
         ps->lw == 0 && ps->bw == 0 && ps->rw == 0 && ps->tw == 0;
}

static void
mark_block_background (array<page_item>& l, brush bg) {
  for (int i=0; i<N(l); i++)
    if (l[i]->type == PAGE_LINE_ITEM) {
      page_item item= copy (l[i]);
      if (item->block_bg->get_type () == brush_none)
        item->block_bg= bg;
      l[i]= item;
    }
}

box
bridge_ornamented_rep::typeset_ornament (int desired_status) {
  array<page_item> l2;
  stack_border sb2;
  typeset_ornament_stack (desired_status, l2, sb2);
  return make_ornament_body (ip, l2);
}

void
bridge_ornamented_rep::typeset_ornament_stack (
  int desired_status, array<page_item>& l2, stack_border& sb2)
{
  int i;
  tree old (TUPLE, N(with));
  for (i=0; i<N(with)-1; i+=2) {
    old[i+1]= env->read (with[i]->label);
    env->write_update (with[i]->label, with[i+1]);
  }
  array<line_item> a2, b2;
  ttt->local_start (l2, sb2);
  a2= ttt->a; b2= ttt->b;
  ttt->a= array<line_item> (); ttt->b= array<line_item> ();
  body->typeset (desired_status);
  ttt->a= a2; ttt->b= b2;
  ttt->local_end (l2, sb2);
  for (i-=2; i>=0; i-=2)
    env->write_update (with[i]->label, old[i+1]);
}

void
bridge_ornamented_rep::insert_ornament (box b) {
  /*
  ttt->insert_marker (st, ip);
  array<page_item> l2= array<page_item> (1);
  l2[0]= page_item (b);
  ttt->insert_stack (l2, stack_border ());
  */
  lazy_paragraph par (env, ip);
  par->a= copy (ttt->a);
  par->a << line_item (STD_ITEM, env->mode_op, b, HYPH_INVALID);
  par->a << ttt->b;
  par->format_paragraph ();
  ttt->insert_stack (par->sss->l, par->sss->sb);
}

/******************************************************************************
* Canvases
******************************************************************************/

class bridge_canvas_rep: public bridge_ornamented_rep {
public:
  bridge_canvas_rep (typesetter ttt, tree st, path ip):
    bridge_ornamented_rep (ttt, st, ip, N(st) - 1) {}
  void my_typeset (int desired_status);
};

bridge
bridge_canvas (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_canvas_rep> (ttt, st, ip);
}

void
bridge_canvas_rep::my_typeset (int desired_status) {
  canvas_properties props= get_canvas_properties (env, st);

  SI delta= 0;
  string type= props->type;
  if (type != "plain") {
    SI hpad= props->hpadding;
    SI w   = props->bar_width;
    SI pad = props->bar_padding;
    SI bor = props->border;
    if (ends (type, "w") || ends (type, "e"))
      delta= max (w + pad, 0);
    delta += 2 * bor + 2 * hpad;
  }
  SI l= env->get_length (PAR_LEFT);
  SI r= env->get_length (PAR_RIGHT) + delta;
  with= tuple (PAR_LEFT, tree (TMLEN, as_string (0))) *
        tuple (PAR_RIGHT, tree (TMLEN, as_string (l + r)));

  box b = typeset_ornament (desired_status);

  SI x1, y1, x2, y2, scx, scy;
  get_canvas_horizontal (props, b->x1, b->x2, x1, x2, scx);
  get_canvas_vertical (props, b->y1, b->y2, y1, y2, scy);
  path dip= (type == "plain"? ip: decorate (ip));
  box cb= clip_box (dip, b, x1, y1, x2, y2, props->xt, props->yt, scx, scy);
  if (type != "plain") cb= put_scroll_bars (props, cb, ip, b, scx, scy);
  insert_ornament (cb);
  //insert_ornament (remember_box (decorate (ip), cb));
}

/******************************************************************************
* Highlighting
******************************************************************************/

class bridge_ornament_rep: public bridge_ornamented_rep {
public:
  bridge_ornament_rep (typesetter ttt, tree st, path ip):
    bridge_ornamented_rep (ttt, st, ip, 0) {}
  void my_typeset (int desired_status);
};

bridge
bridge_ornament (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_ornament_rep> (ttt, st, ip);
}

void
bridge_ornament_rep::my_typeset (int desired_status) {
  ornament_parameters ps= env->get_ornament_parameters ();
  SI   l = env->get_length (PAR_LEFT ) + ps->lpad;
  SI   r = env->get_length (PAR_RIGHT) + ps->rpad;
  with   = tuple (PAR_LEFT , tree (TMLEN, as_string (l))) *
           tuple (PAR_RIGHT, tree (TMLEN, as_string (r)));
  box  xb;
  if (N(st) == 2) xb= typeset_as_concat (env, st[1], descend (ip, 1));

  array<page_item> l2;
  stack_border sb2;
  typeset_ornament_stack (desired_status, l2, sb2);
  if (is_block_background_ornament (ps, xb)) {
    mark_block_background (l2, ps->bg);
    ttt->insert_stack (l2, sb2);
    return;
  }
  SI body_w, d1, d2, d3, d4, d5, d6, d7;
  env->get_page_pars (body_w, d1, d2, d3, d4, d5, d6, d7);
  group_highlight_runs (ip, l2, 3 * env->fn->yx);
  SI body_x1= l;
  SI body_x2= max (body_x1, body_w - r);

  int first= -1, last= -1;
  for (int i=0; i<N(l2); i++)
    if (l2[i]->type == PAGE_LINE_ITEM) {
      if (first < 0) first= i;
      last= i;
    }
  if (first < 0) {
    box b = make_ornament_body (ip, l2);
    box hb= highlight_box (ip, b, xb, ps);
    box mb= move_box (decorate (ip), hb, -l, 0);
    insert_ornament (remember_box (decorate (ip), mb));
    return;
  }
  for (int i=first; i<=last; i++)
    if (l2[i]->type == PAGE_LINE_ITEM) {
      body_x1= min (body_x1, l2[i]->b->x1);
      body_x2= max (body_x2, l2[i]->b->x2);
    }

  for (int i=first; i<=last; i++)
    if (l2[i]->type == PAGE_LINE_ITEM) {
      page_item item= copy (l2[i]);
      ornament_parameters ps_i= copy (ps);
      box xb_i= (i == first? xb: box ());
      if (i != first) {
        ps_i->tw= 0;
        ps_i->text= 0.0;
        ps_i->tpad= 0;
        ps_i->tcor= 0;
      }
      if (i != last) {
        ps_i->bw= 0;
        ps_i->bext= 0.0;
        ps_i->bpad += item->spc->def;
        ps_i->bcor= 0;
        item->spc= space (0);
      }
      SI body_y1= min (item->b->y1, env->fn->y1);
      SI body_y2= max (item->b->y2, env->fn->y2);
      box rb= resize_box (item->b->ip, item->b,
                          body_x1, body_y1, body_x2, body_y2);
      box hb= highlight_box (ip, rb, xb_i, ps_i);
      box mb= move_box (decorate (ip), hb, -l, 0);
      item->b= remember_box (decorate (ip), mb);
      l2[i]= item;
    }
  ttt->insert_stack (l2, sb2);
}

/******************************************************************************
* Artistic boxes
******************************************************************************/

class bridge_art_box_rep: public bridge_ornamented_rep {
public:
  bridge_art_box_rep (typesetter ttt, tree st, path ip):
    bridge_ornamented_rep (ttt, st, ip, 0) {}
  void my_typeset (int desired_status);
};

bridge
bridge_art_box (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_art_box_rep> (ttt, st, ip);
}

void
bridge_art_box_rep::my_typeset (int desired_status) {
  art_box_parameters ps= env->get_art_box_parameters (st);
  SI   l = env->get_length (PAR_LEFT ) + ps->lpad;
  SI   r = env->get_length (PAR_RIGHT) + ps->rpad;
  with   = tuple (PAR_LEFT , tree (TMLEN, as_string (l))) *
           tuple (PAR_RIGHT, tree (TMLEN, as_string (r)));
  box  b = typeset_ornament (desired_status);
  box  ab= art_box (ip, b, ps);
  box  mb= move_box (decorate (ip), ab, 0, b->y1 - ps->bpad);
  insert_ornament (remember_box (decorate (ip), mb));
}
