
/******************************************************************************
* MODULE     : modifier.cpp
* DESCRIPTION: a modifier box modifies the behaviour of another box
*              as to cursor movements etc. , but displays in the same way
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Typesetting/Boxes/modifier.hpp"
#include "Typesetting/Boxes/composite.hpp"
#include "Typesetting/Boxes/construct.hpp"
#include "Typesetting/Boxes/inline_link.hpp"
#include "math_font.hpp"
#include "colors.hpp"

/******************************************************************************
* Modifier boxes
******************************************************************************/

modifier_box_rep::modifier_box_rep (path ip, box b2):
  box_rep (ip), b (b2)
{
  x1= b->x1; y1= b->y1;
  x2= b->x2; y2= b->y2;
  x3= b->x3; y3= b->y3;
  x4= b->x4; y4= b->y4;
}

modifier_box_rep::~modifier_box_rep () {}

int
modifier_box_rep::subnr () {
  return 1;
}

box
modifier_box_rep::subbox (int i) { (void) i;
  return b;
}

void
modifier_box_rep::display (renderer ren) {
  (void) ren;
}

void
modifier_box_rep::broadcast (tree t, rectangles& rs) {
  return b->broadcast (t, rs);
}

tree
modifier_box_rep::message (tree t, SI x, SI y, rectangles& rs) {
  return b->message (t, x, y, rs);
}

void
modifier_box_rep::loci (SI x, SI y, SI delta,
			list<string>& ids, rectangles& rs)
{
  return b->loci (x, y, delta, ids, rs);
}

void
modifier_box_rep::collect_page_numbers (hashmap<string,tree>& h, tree page) {
  b->collect_page_numbers (h, page);
}

path
modifier_box_rep::find_tag (string name) {
  return b->find_tag (name);
}

modifier_box_rep::operator tree () {
  return (tree) b;
}

double modifier_box_rep::left_slope () { return b->left_slope (); }
double modifier_box_rep::right_slope () { return b->right_slope (); }
SI modifier_box_rep::left_correction () { return b->left_correction (); }
SI modifier_box_rep::right_correction () { return b->right_correction (); }
SI modifier_box_rep::lsub_correction () {
  return b->lsub_correction (); }
SI modifier_box_rep::lsup_correction () {
  return b->lsup_correction (); }
SI modifier_box_rep::rsub_correction () {
  return b->rsub_correction (); }
SI modifier_box_rep::rsup_correction () {
  return b->rsup_correction (); }
SI modifier_box_rep::sub_lo_base (int level) {
  return b->sub_lo_base (level); }
SI modifier_box_rep::sub_hi_lim  (int level) {
  return b->sub_hi_lim (level); }
SI modifier_box_rep::sup_lo_lim  (int level) {
  return b->sup_lo_lim (level); }
SI modifier_box_rep::sup_lo_base (int level) {
  return b->sup_lo_base (level); }
SI modifier_box_rep::sup_hi_lim  (int level) {
  return b->sup_hi_lim (level); }
SI modifier_box_rep::wide_correction (int mode) {
  return b->wide_correction (mode); }
std::optional<SI> modifier_box_rep::top_accent_attachment () {
  return b->top_accent_attachment (); }
std::optional<SI> modifier_box_rep::math_script_kern (
  athena::text::math_kern_corner corner, SI height) {
  return b->math_script_kern (corner, height); }
void modifier_box_rep::get_bracket_extents (SI& lo, SI& hi) {
  b->get_bracket_extents (lo, hi); }

/******************************************************************************
* New routines concerning the cursor
******************************************************************************/

path
modifier_box_rep::find_box_path (SI x, SI y, SI delta,
                                 bool force, bool& found) {
  return path (0, b->find_box_path (x, y, delta, force, found));
}

path
modifier_box_rep::find_lip () {
  return b->find_lip ();
}

path
modifier_box_rep::find_rip () {
  return b->find_rip ();
}

path
modifier_box_rep::find_left_box_path () {
  return path (0, b->find_left_box_path ());
}

path
modifier_box_rep::find_right_box_path () {
  return path (0, b->find_right_box_path ());
}

path
modifier_box_rep::find_box_path (path p, bool& found) {
  return path (0, b->find_box_path (p, found));
}

path
modifier_box_rep::find_tree_path (path bp) {
  return b->find_tree_path (bp->next);
}

cursor
modifier_box_rep::find_cursor (path bp) {
  return b->find_cursor (bp->next);
}

selection
modifier_box_rep::find_selection (path lbp, path rbp) {
  return b->find_selection (lbp->next, rbp->next);
}

gr_selections
modifier_box_rep::graphical_select (SI x, SI y, SI dist) {
  return b->graphical_select (x- sx(0), y- sy(0), dist);
}

gr_selections
modifier_box_rep::graphical_select (SI x1, SI y1, SI x2, SI y2) {
  return b->graphical_select (x1- sx(0), y1- sy(0), x2- sx(0), y2- sy(0));
}


/******************************************************************************
* Direct link boxes
******************************************************************************/

class direct_link_box_rep: public modifier_box_rep, public inline_link_source {
  string ref;
public:
  direct_link_box_rep (path ip, box b, string ref);
  operator tree () { return tuple ("direct-link", (tree) b); }
  box  adjust_kerning (int mode, double factor);
  box  expand_glyphs (int mode, double factor);
  tree message (tree type, SI x, SI y, rectangles& rs);
  void post_display (renderer& ren);
  bool inline_link (box& body, inline_link_info& link) override {
    body= b;
    link= {list<string> (), ref, "", 0};
    return true;
  }
};

direct_link_box_rep::direct_link_box_rep (path ip, box b, string ref2):
  modifier_box_rep (ip, b), ref (ref2) {}

box
direct_link_box_rep::adjust_kerning (int mode, double factor) {
  return direct_link_box (ip, b->adjust_kerning (mode, factor), ref);
}

box
direct_link_box_rep::expand_glyphs (int mode, double factor) {
  return direct_link_box (ip, b->expand_glyphs (mode, factor), ref);
}

tree
direct_link_box_rep::message (tree type, SI x, SI y, rectangles& rs) {
  if (type == "link-target" && x >= x1 && x < x2 && y >= y1 && y < y2)
    return tree (TUPLE, "link-target", ref);
  if ((type == "select" || type == "double-click") &&
      x >= x1 && x < x2 && y >= y1 && y < y2)
    return tree (TUPLE, "direct-link", ref);
  return modifier_box_rep::message (type, x, y, rs);
}

void
direct_link_box_rep::post_display (renderer& ren) {
  ren->href (ref, x1, y1, x2, y2);
}

/******************************************************************************
* Symbol boxes
******************************************************************************/

class symbol_box_rep: public modifier_box_rep {
  int n;
public:
  symbol_box_rep (path ip, box b, int n);
  operator tree () { return tree (TUPLE, "symbol", subbox(0)); }
  box    adjust_kerning (int mode, double factor);
  box    expand_glyphs (int mode, double factor);
  path   find_box_path (SI x, SI y, SI delta, bool force, bool& found);
};

symbol_box_rep::symbol_box_rep (path ip, box b2, int n2):
  modifier_box_rep (ip, b2), n (n2) {}

box
symbol_box_rep::adjust_kerning (int mode, double factor) {
  return symbol_box (ip, b->adjust_kerning (mode, factor), n);
}

box
symbol_box_rep::expand_glyphs (int mode, double factor) {
  return symbol_box (ip, b->expand_glyphs (mode, factor), n);
}

static path
leaf_prefix (box& b, path& position) {
  path reversed;
  while (!is_nil (position) && !is_atom (position) && b->subnr () > 0) {
    const int child= position->item;
    b= b[child];
    reversed= path (child, reversed);
    position= position->next;
  }
  return reverse (reversed);
}

path
symbol_box_rep::find_box_path (SI x, SI y, SI delta, bool force, bool& found) {
  path p= modifier_box_rep::find_box_path (x, y, delta, force, found);
  box leaf (this);
  path position= p;
  const path prefix= leaf_prefix (leaf, position);
  if (is_nil (position)) return p;
  if (is_accessible (leaf->ip) || force) {
    return prefix * path (position->item <= (n>>1) ? 0 : n, position->next);
  }
  else return p;
}

/******************************************************************************
* Shorter boxes are used for hyphenation
******************************************************************************/

class shorter_box_rep: public modifier_box_rep {
  int pos, len;
public:
  shorter_box_rep (path ip, box b, int len);
  operator tree () { return tuple ("shorter", subbox(0)); }
  box    adjust_kerning (int mode, double factor);
  box    expand_glyphs (int mode, double factor);
  path   find_box_path (SI x, SI y, SI delta, bool force, bool& found);
  path   find_rip ();
  path   find_right_box_path ();
  std::optional<athena::text::caret_affinity> cursor_affinities (path bp) override;
  std::optional<SI> top_accent_attachment () override {
    return len == b->get_leaf_right_pos () - pos ?
      b->top_accent_attachment () : std::nullopt;
  }
  int    get_type ();
  int    get_leaf_left_pos ();
  int    get_leaf_right_pos ();
  string get_leaf_string ();
  font   get_leaf_font ();
  pencil get_leaf_pencil ();
  brush  get_leaf_background ();
  SI     get_leaf_offset (string search);
};

shorter_box_rep::shorter_box_rep (path ip, box b2, int len2):
  modifier_box_rep (ip, b2), pos (b->get_leaf_left_pos ()), len (len2) {}

box
shorter_box_rep::adjust_kerning (int mode, double factor) {
  return shorter_box (ip, b->adjust_kerning (mode, factor), len);
}

box
shorter_box_rep::expand_glyphs (int mode, double factor) {
  return shorter_box (ip, b->expand_glyphs (mode, factor), len);
}

path
shorter_box_rep::find_box_path (SI x, SI y, SI delta, bool force, bool& found) {
  path p= modifier_box_rep::find_box_path (x, y, delta, force, found);
  box leaf (this);
  path position= p;
  const path prefix= leaf_prefix (leaf, position);
  if (is_nil (position)) return p;
  if ((is_accessible (leaf->ip) || force) && (position->item > len))
    return prefix * path (len, position->next);
  else return p;
}

path
shorter_box_rep::find_rip () {
  path p= modifier_box_rep::find_rip ();
  if (is_accessible (ip) && (!is_nil(p)) && (p->item > (pos+ len)))
    return descend (p->next, pos+ len);
  else return p;  
}

path
shorter_box_rep::find_right_box_path () {
  path bp= b->find_right_box_path ();
  box leaf= b;
  const path prefix= leaf_prefix (leaf, bp);
  if (is_nil (bp)) return path (0, prefix);
  return path (0, prefix * path (min (bp->item, len), bp->next));
}

std::optional<athena::text::caret_affinity>
shorter_box_rep::cursor_affinities (path bp) {
  using athena::text::caret_affinity;
  const auto sides= modifier_box_rep::cursor_affinities (bp);
  const path right= find_rip ();
  if (sides && is_accessible (right) && find_tree_path (bp) == reverse (right))
    return len == 0 ? caret_affinity::both : caret_affinity::upstream;
  return sides;
}

int
shorter_box_rep::get_type () {
  return SHORTER_BOX;
}

int
shorter_box_rep::get_leaf_left_pos () {
  return pos;
}

int
shorter_box_rep::get_leaf_right_pos () {
  return pos+ len;
}

string
shorter_box_rep::get_leaf_string () {
  return b->get_leaf_string () (0, len);
}

font
shorter_box_rep::get_leaf_font () {
  return b->get_leaf_font ();
}

pencil
shorter_box_rep::get_leaf_pencil () {
  return b->get_leaf_pencil ();
}

brush
shorter_box_rep::get_leaf_background () {
  return b->get_leaf_background ();
}

SI
shorter_box_rep::get_leaf_offset (string search) {
  return b->get_leaf_offset (search);
}

/******************************************************************************
* Frozen boxes
******************************************************************************/

class frozen_box_rep: public modifier_box_rep {
public:
  frozen_box_rep (path ip, box b);
  operator tree () { return tree (TUPLE, "frozen", subbox(0)); }
  box  adjust_kerning (int mode, double factor);
  box  expand_glyphs (int mode, double factor);
  path find_lip ();
  path find_rip ();
};

frozen_box_rep::frozen_box_rep (path ip, box b2):
  modifier_box_rep (ip, b2) {}

box
frozen_box_rep::adjust_kerning (int mode, double factor) {
  return frozen_box (ip, b->adjust_kerning (mode, factor));
}

box
frozen_box_rep::expand_glyphs (int mode, double factor) {
  return frozen_box (ip, b->expand_glyphs (mode, factor));
}

path
frozen_box_rep::find_lip () {
  return box_rep::find_lip ();
}

path
frozen_box_rep::find_rip () {
  return box_rep::find_rip ();
}

/******************************************************************************
* macro expansions
******************************************************************************/

struct macro_box_rep: public composite_box_rep {
  font big_fn; // big character font if non nil
  int  btype;
  macro_box_rep (path ip, box b, font big_fn, int btype);
  operator tree () { return tree (TUPLE, "macro", (tree) bs[0]); }
  box adjust_kerning (int mode, double factor);
  box expand_glyphs (int mode, double factor);

  int       find_child (SI x, SI y, SI delta, bool force);
  path      find_box_path (SI x, SI y, SI delta, bool force, bool& found);
  path      find_lip ();
  path      find_rip ();
  path      find_box_path (path p, bool& found);
  path      find_tree_path (path bp);
  cursor    find_cursor (path bp);
  selection find_selection (path lbp, path rbp);
  int       get_type ();
  string    get_leaf_string ();
  font      get_leaf_font ();
  pencil    get_leaf_pencil ();
  brush     get_leaf_background ();
  SI        get_leaf_offset (string search);

  double left_slope () { return bs[0]->left_slope(); }
  double right_slope () { return bs[0]->right_slope(); }
  SI left_correction () { return bs[0]->left_correction(); }
  SI right_correction () { return bs[0]->right_correction(); }
  SI lsub_correction () { return bs[0]->lsub_correction(); }
  SI lsup_correction () { return bs[0]->lsup_correction(); }
  SI rsub_correction () {
    if (btype == BIG_OP_BOX && !is_nil (big_fn) &&
        athena::text::math_layout_metrics (big_fn))
      return -bs[0]->right_correction ();
    return bs[0]->rsub_correction();
  }
  SI rsup_correction () {
    if (btype == BIG_OP_BOX && !is_nil (big_fn) &&
        athena::text::math_layout_metrics (big_fn))
      return 0;
    return bs[0]->rsup_correction();
  }
  SI sub_lo_base (int l) {
    // second test separates small and large big operators
    return (!is_nil (big_fn)) && ((y2-y1) <= 3*big_fn->yx)?
      y1 - (l>0? 0: big_fn->yshift): box_rep::sub_lo_base (l); }
  SI sub_hi_lim (int l) {
    // second test separates small and large size big operators
    return (!is_nil (big_fn)) && ((y2-y1) <= 3*big_fn->yx)?
      y1 - (l>0? 0: big_fn->yshift) +
        bs[0]->sub_hi_lim (l) - bs[0]->sub_lo_base (l):
      box_rep::sub_hi_lim (l); }
  SI sup_lo_base (int l) {
    if (is_nil (big_fn)) return box_rep::sup_lo_base (l);
    SI syx= big_fn->yx * script (big_fn->size, 1) / big_fn->size;
    if ((y2-y1) <= 3*big_fn->yx) syx -= (l<0? 0: big_fn->yshift);
    return y2- syx; }
  SI wide_correction (int mode) {
    return bs[0]->wide_correction (mode); }
  std::optional<SI> top_accent_attachment () override {
    auto anchor= bs[0]->top_accent_attachment ();
    if (anchor) *anchor += sx (0);
    return anchor;
  }
  std::optional<SI> math_script_kern (
    athena::text::math_kern_corner corner, SI height) override {
    return bs[0]->math_script_kern (corner, height);
  }
  void get_bracket_extents (SI& lo, SI& hi) {
    bs[0]->get_bracket_extents (lo, hi); }
};

macro_box_rep::macro_box_rep (path ip, box b, font fn, int bt):
  composite_box_rep (ip), big_fn (fn), btype (bt) {
    insert (b, 0, 0); position (); finalize (); }
box macro_box_rep::adjust_kerning (int mode, double factor) {
  return macro_box (ip, bs[0]->adjust_kerning (mode, factor), big_fn); }
box macro_box_rep::expand_glyphs (int mode, double factor) {
  return macro_box (ip, bs[0]->expand_glyphs (mode, factor), big_fn); }
int macro_box_rep::find_child (SI x, SI y, SI delta, bool force) {
  (void) x; (void) y; (void) delta; (void) force; return -1; }
path macro_box_rep::find_box_path (SI x, SI y, SI delta, bool force, bool& f) {
  return box_rep::find_box_path (x, y, delta, force, f); }
path macro_box_rep::find_lip () {
  return box_rep::find_lip (); }
path macro_box_rep::find_rip () {
  return box_rep::find_rip (); }
path macro_box_rep::find_box_path (path p, bool& found) {
  return box_rep::find_box_path (p, found); }
path macro_box_rep::find_tree_path (path bp) {
  return box_rep::find_tree_path (bp); }
cursor macro_box_rep::find_cursor (path bp) {
  return box_rep::find_cursor (bp); }
selection macro_box_rep::find_selection (path lbp, path rbp) {
  return box_rep::find_selection (lbp, rbp); }
int macro_box_rep::get_type () {
  return btype; }
string macro_box_rep::get_leaf_string () {
  return bs[0]->get_leaf_string (); }
font macro_box_rep::get_leaf_font () {
  return bs[0]->get_leaf_font (); }
pencil macro_box_rep::get_leaf_pencil () {
  return bs[0]->get_leaf_pencil (); }
brush macro_box_rep::get_leaf_background () {
  return bs[0]->get_leaf_background (); }
SI macro_box_rep::get_leaf_offset (string search) {
  return bs[0]->get_leaf_offset (search); }

struct macro_delimiter_box_rep: public macro_box_rep {
  SI dy;
  macro_delimiter_box_rep (path ip, box b, font fn, SI dy2):
    macro_box_rep (ip, b, fn, STD_BOX), dy (dy2) {}
  operator tree () {
    return tree (TUPLE, "macro_delimiter", (tree) bs[0]); }
  SI sub_lo_base (int l) {
    SI fb= l<=0? big_fn->ysub_lo_base: big_fn->ysub_lo_base + big_fn->yshift;
    fb += min (dy, 0);
    return min (y1, max (y1 - (l>0? 0: big_fn->yshift), fb)); }
  SI sub_hi_lim (int l) {
    return big_fn->ysub_hi_lim + min (dy, 0); }
  SI sup_lo_base (int l) {
    SI fb= l>=0? big_fn->ysup_lo_base: big_fn->ysup_lo_base - big_fn->yshift;
    fb += max (dy, 0);
    SI ex= big_fn->yx;
    return max (y2 - ex, fb); }
  SI sup_hi_lim (int l) {
    SI fb= big_fn->ysup_hi_lim;
    fb += max (dy, 0);
    SI ex= big_fn->yx;
    if (fb + ex <= y2) return y2;
    if (fb + (ex>>1) <= y2) return (y2+fb)>>1;
    return fb; }
};

/******************************************************************************
* box construction routines
******************************************************************************/

box
symbol_box (path ip, box b, int n) {
  return tm_new<symbol_box_rep> (ip, b, n);
}

box
shorter_box (path ip, box b, int len) {
  return tm_new<shorter_box_rep> (ip, b, len);
}

box
frozen_box (path ip, box b) {
  return tm_new<frozen_box_rep> (ip, b);
}

box
direct_link_box (path ip, box b, string ref) {
  return b->with_direct_link (ip, ref);
}

// An asynchronously resolved radioactive link retains the exact old box and
// its metrics. The owning actor publishes source-relative ranges; drawing and
// hit testing project them onto the old box without redoing paragraph layout.
class live_radioactive_box_rep final:
    public modifier_box_rep, public inline_link_source {
  std::shared_ptr<live_radioactive_decorations> live;
  int child;
  string tint;

  template<class F> void for_regions (F&& visit) {
    const auto snapshot= live->read ();
    if (!snapshot) return;
    for (const auto& match: snapshot->spans) {
      if (match.child != child || match.begin >= match.end) continue;
      if (b->get_type () != TEXT_BOX) {
        // A native math expression is a structured box, not a textual leaf.
        // The matching projection addresses it as one complete source node.
        if (match.begin <= 0 && match.end >= 1)
          visit (rectangle (x1, y1, x2, y2), match.destination);
        continue;
      }
      const int first= b->get_leaf_left_pos ();
      const int last= b->get_leaf_right_pos ();
      const int begin= std::max (match.begin, first);
      const int end= std::min (match.end, last);
      if (begin >= end) continue;
      selection selected= b->find_selection (path (begin-first), path (end-first));
      if (selected->valid)
        for (auto r= selected->rs; !is_nil (r); r= r->next)
          visit (r->item, match.destination);
    }
  }

public:
  live_radioactive_box_rep (path ip, box body,
    std::shared_ptr<live_radioactive_decorations> state, int part, string color):
    modifier_box_rep (ip, body), live (std::move (state)),
    child (part), tint (color) {}

  operator tree () override { return (tree) b; }
  box adjust_kerning (int mode, double factor) override {
    return live_radioactive_box (ip, b->adjust_kerning (mode, factor),
                                live, child, tint);
  }
  box expand_glyphs (int mode, double factor) override {
    return live_radioactive_box (ip, b->expand_glyphs (mode, factor),
                                live, child, tint);
  }
  bool inline_link (box& body, inline_link_info& link) override {
    body= b;
    link= {list<string> (), "", "", 0, live, child, tint};
    return true;
  }
  tree message (tree type, SI x, SI y, rectangles& rs) override {
    if (type == "link-target" || type == "select" || type == "double-click") {
      tree answer;
      for_regions ([&] (rectangle r, string target) {
        if (x >= r->x1 && x < r->x2 && y >= r->y1 && y < r->y2)
          answer= type == "link-target" ?
            tree (TUPLE, "link-target", target) : tree (TUPLE, "direct-link", target);
      });
      if (!is_nil (answer)) return answer;
    }
    return modifier_box_rep::message (type, x, y, rs);
  }
  void post_display (renderer& ren) override {
    const bool decorate= tint != "" && tint != "preserve";
    const pencil ink= decorate ?
      pencil (named_color (tint), ren->pixel) : pencil (false);
    for_regions ([&] (rectangle r, string target) {
      ren->href (target, r->x1, r->y1, r->x2, r->y2);
      if (decorate)
        b->draw_link_ink_clipped (ren,
          rectangle (r->x1, min (b->y3, r->y1), r->x2,
                     max (b->y4, r->y2)), ink);
    });
  }
};

box live_radioactive_box (
    path ip, box b, std::shared_ptr<live_radioactive_decorations> links,
    int source_child, string color) {
  return tm_new<live_radioactive_box_rep> (
    ip, b, std::move (links), source_child, color);
}

box
box_rep::with_direct_link (path ip, string ref) {
  box linked= tm_new<direct_link_box_rep> (ip, this, ref);
  // Reparent an already positioned box without retaining an inner offset.
  linked->x0= x0; linked->y0= y0;
  x0= y0= 0;
  return linked;
}

box
macro_box (path ip, box b, font big_fn, int btype) {
  return tm_new<macro_box_rep> (ip, b, big_fn, btype);
}

box
macro_delimiter_box (path ip, box b, font fn, SI dy) {
  return tm_new<macro_delimiter_box_rep> (ip, b, fn, dy);
}
