
/******************************************************************************
* MODULE     : lazy_gui.cpp
* DESCRIPTION: Lazy typesetting of GUI primitives
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Line/lazy_typeset.hpp"
#include "Line/lazy_vstream.hpp"
#include "Format/format.hpp"
#include "Stack/stacker.hpp"
#include "Boxes/construct.hpp"
#include "analyze.hpp"
#include "Concat/canvas_properties.hpp"

box surround (edit_env env, box b, path ip,
              array<line_item> l, array<line_item> r, format fm);

/******************************************************************************
* Canvases
******************************************************************************/

struct lazy_canvas_rep: public lazy_rep {
  canvas_properties props;
  lazy par;

  lazy_canvas_rep (canvas_properties props2, lazy par2, path ip):
    lazy_rep (LAZY_CANVAS, ip), props (props2), par (par2) {}
  inline operator tree () { return "Canvas"; }
  lazy produce (lazy_type request, format fm);
  format query (lazy_type request, format fm);
};

struct lazy_canvas {
EXTEND_NULL(lazy,lazy_canvas);
  inline lazy_canvas (canvas_properties props, lazy par, path ip):
    rep (tm_new<lazy_canvas_rep> (props, par, ip)) {
      rep->ref_count= 1; }
};
EXTEND_NULL_CODE(lazy,lazy_canvas);

format
lazy_canvas_rep::query (lazy_type request, format fm) {
  if ((request == LAZY_BOX) && (fm->type == QUERY_VSTREAM_WIDTH)) {
    format body_fm= par->query (request, fm);
    format_width fmw= (format_width) body_fm;
    SI width= fmw->width;
    edit_env env= props->env;
    tree old1= env->local_begin (PAGE_MEDIUM, "papyrus");
    tree old2= env->local_begin (PAR_LEFT, "0tmpt");
    tree old3= env->local_begin (PAR_RIGHT, "0tmpt");
    tree old4= env->local_begin (PAR_MODE, "justify");
    tree old5= env->local_begin (PAR_NO_FIRST, "true");
    tree old6= env->local_begin (PAR_WIDTH, tree (TMLEN, as_string (width)));
    SI x1, x2, scx;
    get_canvas_horizontal (props, 0, fmw->width, x1, x2, scx);
    env->local_end (PAR_WIDTH, old6);
    env->local_end (PAR_NO_FIRST, old5);
    env->local_end (PAR_MODE, old4);
    env->local_end (PAR_RIGHT, old3);
    env->local_end (PAR_LEFT, old2);
    env->local_end (PAGE_MEDIUM, old1);
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
    return make_format_width (x2 - x1 + delta);
  }
  return lazy_rep::query (request, fm);
}

lazy
lazy_canvas_rep::produce (lazy_type request, format fm) {
  if (request == type) return this;
  if (request == LAZY_VSTREAM || request == LAZY_BOX) {
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
    format bfm= fm;
    if (request == LAZY_VSTREAM) {
      format_vstream fvs= (format_vstream) fm;
      bfm= make_format_width (fvs->width - delta);
    }
    box b= (box) par->produce (LAZY_BOX, bfm);
    format_width fmw= (format_width) bfm;
    SI width= fmw->width + delta;
    edit_env env= props->env;
    tree old1= env->local_begin (PAGE_MEDIUM, "papyrus");
    tree old2= env->local_begin (PAR_LEFT, "0tmpt");
    tree old3= env->local_begin (PAR_RIGHT, "0tmpt");
    tree old4= env->local_begin (PAR_MODE, "justify");
    tree old5= env->local_begin (PAR_NO_FIRST, "true");
    tree old6= env->local_begin (PAR_WIDTH, tree (TMLEN, as_string (width)));
    SI x1, x2, scx;
    get_canvas_horizontal (props, b->x1, b->x2, x1, x2, scx);
    SI y1, y2, scy;
    get_canvas_vertical (props, b->y1, b->y2, y1, y2, scy);
    env->local_end (PAR_WIDTH, old6);
    env->local_end (PAR_NO_FIRST, old5);
    env->local_end (PAR_MODE, old4);
    env->local_end (PAR_RIGHT, old3);
    env->local_end (PAR_LEFT, old2);
    env->local_end (PAGE_MEDIUM, old1);
    path dip= (type == "plain"? ip: decorate (ip));
    box rb= clip_box (dip, b, x1, y1, x2, y2, props->xt, props->yt, scx, scy);
    if (type != "plain") rb= put_scroll_bars (props, rb, ip, b, scx, scy);
    if (request == LAZY_BOX) return make_lazy_box (rb);
    else {
      array<page_item> l;
      l << page_item (rb);
      return lazy_vstream (ip, "", l, stack_border ());
    }
  }
  return lazy_rep::produce (request, fm);
}

lazy
make_lazy_canvas (edit_env env, tree t, path ip) {
  canvas_properties props= get_canvas_properties (env, t);
  lazy par= make_lazy (env, t[6], descend (ip, 6));
  return lazy_canvas (props, par, ip);
}

/******************************************************************************
* Ornaments
******************************************************************************/

struct lazy_ornament_rep: public lazy_rep {
  edit_env env;             // "current" environment
  lazy par;                 // the ornamented body
  box xb;                   // extra box
  ornament_parameters ps;   // parameters for the ornament
  lazy_ornament_rep (edit_env env2, lazy par2, box xb2, path ip,
		     ornament_parameters ps2):
    lazy_rep (LAZY_ORNAMENT, ip), env (env2), par (par2),
    xb (xb2), ps (ps2) {}  
  inline operator tree () { return "Ornament"; }
  lazy produce (lazy_type request, format fm);
  format query (lazy_type request, format fm);
};

struct lazy_ornament {
EXTEND_NULL(lazy,lazy_ornament);
  lazy_ornament (edit_env env, lazy par, box xb, path ip,
		 ornament_parameters ps):
    rep (tm_new<lazy_ornament_rep> (env, par, xb, ip, ps)) {
    rep->ref_count= 1; }
};
EXTEND_NULL_CODE(lazy,lazy_ornament);

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
  brush block_bg= l[start]->block_bg;
  for (int i=0; i<n; i++) {
    box b= l[start + i]->b;
    lines_bx[i]= resize_box (b->ip, b, b->x1, min (b->y1, b->y3),
                              b->x2, max (b->y2, b->y4));
    lines_ht[i]= l[start + i]->spc->def;
    if (l[start + i]->block_bg != block_bg) block_bg= brush (false);
  }
  box b= stack_box (ip, lines_bx, lines_ht);
  SI dy= n == 0? 0: b[0]->y2;
  b= move_box (decorate (ip), b, 0, dy);
  page_item last= l[end];
  page_item item= page_item (PAGE_LINE_ITEM, b, space (0), last->penalty,
                             last->fl, last->nr_cols, last->t);
  item->block_bg= block_bg;
  return item;
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
mark_block_background (array<page_item>& l, brush bg, SI dx) {
  for (int i=0; i<N(l); i++)
    if (l[i]->type == PAGE_LINE_ITEM) {
      page_item item= copy (l[i]);
      if (item->block_bg->get_type () == brush_none)
        item->block_bg= bg;
      if (dx != 0)
        item->b= move_box (decorate (item->b->ip), item->b, dx, 0);
      l[i]= item;
    }
}

static void
materialize_block_background_runs (
  path ip, array<page_item>& l, SI body_x1, SI body_x2)
{
  array<page_item> r;
  for (int i=0; i<N(l); ) {
    if (l[i]->type != PAGE_LINE_ITEM ||
        l[i]->block_bg->get_type () == brush_none) {
      r << l[i++];
      continue;
    }

    int start= i;
    while (i+1<N(l) && l[i+1]->type == PAGE_LINE_ITEM &&
           l[i+1]->block_bg == l[start]->block_bg)
      i++;

    int n= i - start + 1;
    array<box> lines_bx (n);
    array<SI> lines_ht (n);
    for (int j=0; j<n; j++) {
      box b= l[start + j]->b;
      lines_bx[j]= resize_box (b->ip, b, b->x1, min (b->y1, b->y3),
                                b->x2, max (b->y2, b->y4));
      lines_ht[j]= j+1<n? l[start + j]->spc->def: 0;
    }
    box b= stack_box (ip, lines_bx, lines_ht);
    array<rectangle> rs;
    array<brush> bg;
    rs << rectangle (body_x1, b->sy1 (n-1), body_x2, b->sy2 (0));
    bg << l[start]->block_bg;
    b= block_background_box (decorate (b->ip), b, rs, bg);
    SI dy= n == 0? 0: b[0]->y2;
    b= move_box (decorate (ip), b, 0, dy);
    page_item last= l[i];
    page_item item= page_item (PAGE_LINE_ITEM, b, last->spc, last->penalty,
                               last->fl, last->nr_cols, last->t);
    item->block_bg= brush (false);
    r << item;
    i++;
  }
  l= r;
}

format
lazy_ornament_rep::query (lazy_type request, format fm) {
  if ((request == LAZY_BOX) && (fm->type == QUERY_VSTREAM_WIDTH)) {
    format body_fm= par->query (request, fm);
    format_width fmw= (format_width) body_fm;
    SI dw= ps->lpad + ps->rpad;
    return make_format_width (fmw->width + dw);
  }
  return lazy_rep::query (request, fm);
}

lazy
lazy_ornament_rep::produce (lazy_type request, format fm) {
  if (request == type) return this;
  if (request == LAZY_VSTREAM && fm->type == FORMAT_VSTREAM) {
    format_vstream fvs= (format_vstream) fm;
    SI dw= ps->lpad + ps->rpad;
    format bfm= make_format_vstream (fvs->width - dw, fvs->before, fvs->after);
    lazy body= par->produce (LAZY_VSTREAM, bfm);
    lazy_vstream body_vs= (lazy_vstream) body;
    array<page_item> l= body_vs->l;
    if (is_block_background_ornament (ps, xb)) {
      mark_block_background (l, ps->bg, ps->lpad);
      return lazy_vstream (ip, "", l, body_vs->sb);
    }
    group_highlight_runs (ip, l, 3 * env->fn->yx);
    SI body_x1= 0;
    SI body_x2= max (body_x1, fvs->width - dw);

    int first= -1, last= -1;
    for (int i=0; i<N(l); i++)
      if (l[i]->type == PAGE_LINE_ITEM) {
        if (first < 0) first= i;
        last= i;
      }
    if (first < 0) {
      box b = (box) par->produce (LAZY_BOX, make_format_width (fvs->width - dw));
      box hb= highlight_box (ip, b, xb, ps);
      hb= surround (env, hb, ip, fvs->before, fvs->after, bfm);
      array<page_item> empty_l;
      empty_l << page_item (hb);
      return lazy_vstream (ip, "", empty_l, stack_border ());
    }
    for (int i=first; i<=last; i++)
      if (l[i]->type == PAGE_LINE_ITEM) {
        body_x1= min (body_x1, l[i]->b->x1);
        body_x2= max (body_x2, l[i]->b->x2);
      }

    materialize_block_background_runs (ip, l, body_x1, body_x2);
    first= last= -1;
    for (int i=0; i<N(l); i++)
      if (l[i]->type == PAGE_LINE_ITEM) {
        if (first < 0) first= i;
        last= i;
      }

    for (int i=first; i<=last; i++)
      if (l[i]->type == PAGE_LINE_ITEM) {
        page_item item= copy (l[i]);
        SI item_spc= item->spc->def;
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
          ps_i->bpad += item_spc;
          ps_i->bcor= 0;
          item->spc= space (0);
        }
        SI body_y1= min (item->b->y1, env->fn->y1);
        SI body_y2= max (item->b->y2, env->fn->y2);
        box rb= resize_box (item->b->ip, item->b,
                            body_x1, body_y1, body_x2, body_y2);
        item->b= highlight_box (ip, rb, xb_i, ps_i);
        l[i]= item;
      }
    return lazy_vstream (ip, "", l, body_vs->sb);
  }
  if (request == LAZY_VSTREAM) {
    box b = (box) par->produce (LAZY_BOX, fm);
    box hb= highlight_box (ip, b, xb, ps);
    array<page_item> l;
    l << page_item (hb);
    return lazy_vstream (ip, "", l, stack_border ());
  }
  if (request == LAZY_BOX) {
    format bfm= fm;
    box b = (box) par->produce (LAZY_BOX, bfm);
    box hb= highlight_box (ip, b, xb, ps);
    // FIXME: this dirty hack ensures that shoving is correct
    hb= move_box (decorate (ip), hb, 1, 0);
    hb= move_box (decorate (ip), hb, -1, 0);
    // End dirty hack
    return make_lazy_box (hb);
  }
  return lazy_rep::produce (request, fm);
}

lazy
make_lazy_ornament (edit_env env, tree t, path ip) {
  ornament_parameters ps= env->get_ornament_parameters ();
  lazy par= make_lazy (env, t[0], descend (ip, 0));
  box  xb;
  if (N(t) == 2) xb= typeset_as_concat (env, t[1], descend (ip, 1));
  return lazy_ornament (env, par, xb, ip, ps);
}

/******************************************************************************
* Art boxes
******************************************************************************/

struct lazy_art_box_rep: public lazy_rep {
  edit_env env;             // "current" environment
  lazy par;                 // the ornamented body
  art_box_parameters ps;    // parameters for the art_box
  lazy_art_box_rep (edit_env env2, lazy par2, path ip, art_box_parameters ps2):
    lazy_rep (LAZY_ART_BOX, ip), env (env2), par (par2), ps (ps2) {}  
  inline operator tree () { return "Art_Box"; }
  lazy produce (lazy_type request, format fm);
  format query (lazy_type request, format fm);
};

struct lazy_art_box {
EXTEND_NULL(lazy,lazy_art_box);
  lazy_art_box (edit_env env, lazy par, path ip, art_box_parameters ps):
    rep (tm_new<lazy_art_box_rep> (env, par, ip, ps)) {
    rep->ref_count= 1; }
};
EXTEND_NULL_CODE(lazy,lazy_art_box);

format
lazy_art_box_rep::query (lazy_type request, format fm) {
  if ((request == LAZY_BOX) && (fm->type == QUERY_VSTREAM_WIDTH)) {
    format body_fm= par->query (request, fm);
    format_width fmw= (format_width) body_fm;
    SI dw= ps->lpad + ps->rpad;
    return make_format_width (fmw->width + dw);
  }
  return lazy_rep::query (request, fm);
}

lazy
lazy_art_box_rep::produce (lazy_type request, format fm) {
  if (request == type) return this;
  if (request == LAZY_VSTREAM || request == LAZY_BOX) {
    format bfm= fm;
    if (request == LAZY_VSTREAM) {
      format_vstream fvs= (format_vstream) fm;
      SI dw= ps->lpad + ps->rpad;
      bfm= make_format_width (fvs->width - dw);
    }
    box b = (box) par->produce (LAZY_BOX, bfm);
    box hb= art_box (ip, b, ps);
    hb= move_box (decorate (ip), hb, 0, b->y1 - ps->bpad);    
    // FIXME: this dirty hack ensures that shoving is correct
    hb= move_box (decorate (ip), hb, 1, 0);
    hb= move_box (decorate (ip), hb, -1, 0);
    // End dirty hack
    if (fm->type == FORMAT_VSTREAM) {
      format_vstream fs= (format_vstream) fm;
      hb= surround (env, hb, ip, fs->before, fs->after, bfm);
    }
    if (request == LAZY_BOX) return make_lazy_box (hb);
    else {
      array<page_item> l;
      l << page_item (hb);
      return lazy_vstream (ip, "", l, stack_border ());
    }
  }
  return lazy_rep::produce (request, fm);
}

lazy
make_lazy_art_box (edit_env env, tree t, path ip) {
  art_box_parameters ps= env->get_art_box_parameters (t);
  lazy par= make_lazy (env, t[0], descend (ip, 0));
  return lazy_art_box (env, par, ip, ps);
}
