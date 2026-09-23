
/******************************************************************************
* MODULE     : tree_spell.cpp
* DESCRIPTION: Spell checking inside trees
* COPYRIGHT  : (C) 2019  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tree_search.hpp"
#include "analyze.hpp"
#include "boot.hpp"
#include "drd_mode.hpp"
#include "drd_std.hpp"
#include "language.hpp"
#include "vars.hpp"
#include "hashset.hpp"
#include "universal.hpp"
#include "tree_spell.hpp"
#include "utf8_edit.hpp"
#include "locale.hpp"
#include <chrono>

thread_local int spell_max_hits= 1000000;
void spell (range_set& sel, tree t, tree what, path p);
thread_local hashset<tree_label> spell_ignore;

/******************************************************************************
* Useful subroutines
******************************************************************************/

static void
spell_initialize () {
  if (N(spell_ignore) > 0) return;
  spell_ignore->insert (make_tree_label ("abbr"));
  spell_ignore->insert (make_tree_label ("name"));
  spell_ignore->insert (make_tree_label ("bib-list"));
  spell_ignore->insert (make_tree_label ("explain-macro"));
}

static void
merge (range_set& sel, range_set ssel) {
  for (int i=0; i+1<N(ssel); i+=2)
    if (N(sel) == 0 || path_less_eq (sel[N(sel)-1], ssel[i]))
      sel << ssel[i] << ssel[i+1];
}

static bool
is_accessible_for_spell (tree t, int i) {
  if (spell_ignore->contains (L(t))) return false;
  if (is_func (t, RAW_DATA) || is_func (t, NAMED_SYMBOL)) return false;
  if (is_accessible_child (t, i)) return true;
  if (is_func (t, HIDDEN)) return true;
  if (get_access_mode () != DRD_ACCESS_SOURCE) return false;
  return i >= 0 && i < N(t);
}

/******************************************************************************
* Spelling
******************************************************************************/

bool
spell_string (tree lan, string s) {
  if (is_atomic (lan)) return check_word (lan->label, s);
  else return true;
}

void
spell_string (tree lan, range_set& sel, string s,
              path p, int pos1, int pos2) {
  if (!is_atomic (lan)) return;
  pos1= utf8_grapheme_snap (s, pos1, true);
  pos2= utf8_grapheme_snap (s, pos2, false);
  if (pos1 >= pos2) return;
  string locale= language_to_locale (lan->label);
  for (const auto& word: athena::text::word_segments (
         {s.data () + pos1, std::size_t (pos2 - pos1)},
         {locale.data (), std::size_t (N(locale))})) {
    int begin= pos1 + word.begin, end= pos1 + word.end;
    if (word.lexical && end - begin <= 512 &&
        !spell_string (lan, s (begin, end)))
      merge (sel, simple_range (p * begin, p * end));
  }
}

void
spell (tree mode, tree lan, range_set& sel, tree t, path p) {
  if (N(sel) > spell_max_hits) return;
  if (is_atomic (t)) {
    if (mode == "text")
      spell_string (lan, sel, t->label, p, 0, N(t->label)); }
  else
    for (int i=0; i<N(t); i++)
      if (is_accessible_for_spell (t, i)) {
        tree smode= the_drd->get_env_child (t, i, MODE, mode);
        tree slan = the_drd->get_env_child (t, i, LANGUAGE, lan);
        spell (smode, slan, sel, t[i], p * i);
      }
}

void
spell (tree mode, tree lan, range_set& sel, tree t, path p, path pos) {
  if (is_atomic (t)) spell (mode, lan, sel, t, p);
  else {
    if (is_nil (pos)) spell (mode, lan, sel, t, p);
    else {
      int hits= 0;
      array<range_set> sub (N(t));
      if (pos->item >= 0 && pos->item < N(t))
        if (is_accessible_for_spell (t, pos->item)) {
          int i= pos->item;
          tree smode= the_drd->get_env_child (t, i, MODE, mode);
          tree slan = the_drd->get_env_child (t, i, LANGUAGE, lan);
          spell (smode, slan, sub[i], t[i], p * i);
          hits += N(sub[i]);
        }
      for (int d=1; d<N(t); d++)
        for (int e=0; e<=1; e++) {
          if (hits > spell_max_hits) break;
          int i= (e==0? pos->item + d: pos->item - d);
          if (i >= 0 && i < N(t))
            if (is_accessible_for_spell (t, i)) {
              tree smode= the_drd->get_env_child (t, i, MODE, mode);
              tree slan = the_drd->get_env_child (t, i, LANGUAGE, lan);
              spell (smode, slan, sub[i], t[i], p * i);
              hits += N(sub[i]);
            }
        }
      for (int i=0; i<N(t); i++) sel << sub[i];
    }
  }
}

void
spell (tree mode, tree lan, range_set& sel, tree t,
       path p, path pos1, path pos2) {
  if (is_nil (pos1) || is_nil (pos2));
  else if (is_atomic (t))
    { if (mode == "text") spell_string (lan, sel, t->label,
                  p, max (pos1->item, 0), min (pos2->item, N(t->label))); }
  else if (pos1 == path (0) && pos2 == path (1))
    spell (mode, lan, sel, t, p);
  else if (!is_atom (pos1) && !is_atom (pos2) && pos1->item == pos2->item)
    spell (mode, lan, sel, t[pos1->item],
           p * pos1->item, pos1->next, pos2->next);
  else {
    int i1= 0, i2= N(t);
    if (!is_atom (pos1)) i1= max (pos1->item, i1);
    if (!is_atom (pos2)) i2= min (pos2->item + 1, i2);
    int hits= 0;
    array<range_set> sub (N(t));
    for (int i=i1; i<i2; i++) {
      if (hits > spell_max_hits) break;
      if (is_accessible_for_spell (t, i)) {
        tree smode= the_drd->get_env_child (t, i, MODE, mode);
        tree slan = the_drd->get_env_child (t, i, LANGUAGE, lan);
        if (i == i1 && !is_atom (pos1) && i1 + 1 < i2)
          spell (smode, slan, sub[i], t[i],
                 p * i, pos1->next, path (right_index (t[i])));
        else if (i == i2 - 1 && !is_atom (pos2) && i1 + 1 < i2)
          spell (smode, slan, sub[i], t[i],
                 p * i, path (0), pos2->next);
        else
          spell (smode, slan, sub[i], t[i], p * i);
        hits += N(sub[i]);
      }
    }
    for (int i=i1; i<i2; i++) sel << sub[i];
  }
}

/******************************************************************************
* Front end
******************************************************************************/

incremental_spell::frame::frame (tree t2, tree mode2, tree lan2,
                                path p2, path focus2):
  t (t2), mode (mode2), lan (lan2), p (p2), focus (focus2) {
  if (!is_atomic (t) && !is_nil (focus))
    pivot= max (0, min (N(t)-1, focus->item));
  if (is_atomic (t)) {
    limit= N(t->label);
    if (!is_nil (focus)) {
      offset= max (0, min (limit, focus->item));
      int remaining= 512;
      while (offset > 0 && t->label[offset-1] != ' ' && --remaining > 0)
        --offset;
      if (remaining == 0) offset= 0;
      // The byte-limited viewport search must not stop inside a UTF-8 scalar.
      while (offset > 0 && !athena::text::scalar_boundary (
               {t->label.data (), std::size_t (limit)}, offset)) --offset;
      stop= offset;
    }
  }
}

void
incremental_spell::reset () {
  stack.clear ();
  errors.clear ();
  initialized= false;
}

void
incremental_spell::start (tree t, tree mode, tree lan, path root, path focus) {
  reset ();
  spell_initialize ();
  stack.emplace_back (t, mode, lan, root, focus);
  initialized= true;
}

bool
incremental_spell::step (int word_budget, int node_budget, int milliseconds) {
  auto deadline= std::chrono::steady_clock::now () +
                std::chrono::milliseconds (milliseconds);
  int chars= 4096;
  bool changed= false;
  while (!stack.empty () && word_budget > 0 && node_budget > 0 && chars > 0 &&
         std::chrono::steady_clock::now () < deadline) {
    frame& f= stack.back ();
    if (is_atomic (f.t)) {
      if (f.mode != "text" || !is_atomic (f.lan) ||
          (f.word == f.words.size () && f.offset >= f.limit && f.stop == 0)) {
        stack.pop_back ();
        --node_budget;
        continue;
      }
      if (f.word == f.words.size () && f.offset >= f.limit) {
        f.limit= f.stop;
        f.stop= 0;
        f.offset= 0;
        f.skip_first= false;
      }
      string s= f.t->label;
      if (f.word == f.words.size ()) {
        f.words.clear ();
        f.word= 0;
        int begin= f.offset, end= begin + min (4096, f.limit - begin);
        while (end > begin && !athena::text::scalar_boundary (
                 {s.data (), std::size_t (N(s))}, end)) --end;
        string locale= language_to_locale (f.lan->label);
        auto segments= athena::text::word_segments (
          {s.data () + begin, std::size_t (end - begin)},
          {locale.data (), std::size_t (N(locale))});
        const bool skip_first= f.skip_first;
        f.skip_first= false;
        f.offset= end;
        if (end < f.limit && !segments.empty ()) {
          const auto tail= segments.back ();
          // Retain an unfinished word; overlong tokens are skipped across
          // windows rather than rescanned or reported as partial words.
          if (tail.end - tail.begin <= 512 && tail.begin != 0)
            f.offset= begin + tail.begin;
          else f.skip_first= true;
          segments.pop_back ();
        }
        for (std::size_t i= 0; i < segments.size (); ++i) {
          const auto& word= segments[i];
          if ((i == 0 && skip_first) || !word.lexical || word.end - word.begin > 512)
            continue;
          f.words.push_back ({begin + word.begin, begin + word.end, true});
        }
        chars-= end - begin;
      }
      if (f.word == f.words.size ()) continue;
      const auto word= f.words[f.word++];
      if (!spell_string (f.lan, s (word.begin, word.end))) {
        errors[f.p * int (word.begin)]= f.p * int (word.end);
        changed= true;
      }
      --word_budget;
    }
    else {
      // Visit the viewport branch first, then successively farther siblings.
      int n= f.next++;
      int i= n == 0? f.pivot:
             (n & 1)? f.pivot + (n+1)/2: f.pivot - n/2;
      --node_budget;
      if (n >= 2*N(f.t)) { stack.pop_back (); continue; }
      if (i < 0 || i >= N(f.t) || !is_accessible_for_spell (f.t, i)) continue;
      tree mode= the_drd->get_env_child (f.t, i, MODE, f.mode);
      tree lan= the_drd->get_env_child (f.t, i, LANGUAGE, f.lan);
      path focus= !is_nil (f.focus) && i == f.focus->item?
                  f.focus->next: path ();
      frame child (f.t[i], mode, lan, f.p * i, focus);
      stack.push_back (std::move (child));
    }
  }
  return changed;
}

range_set
incremental_spell::selections (path cursor) const {
  range_set result;
  for (const auto& hit: errors)
    if (!(path_less_eq (hit.first, cursor) && path_less (cursor, hit.second)))
      result << hit.first << hit.second;
  return result;
}

range_set
spell (string lan, tree t, path p, int limit) {
  spell_initialize ();
  spell_max_hits= limit;
  range_set sel;
  //cout << "Spell " << what << "\n";
  spell ("text", lan, sel, t, p);
  //cout << "Selected " << sel << "\n";
  spell_max_hits= 1000000;
  return sel;
}

range_set
spell (string lan, tree t, path p, path pos, int limit) {
  spell_initialize ();
  spell_max_hits= limit;
  range_set sel;
  //cout << "Spell " << what << "\n";
  spell ("text", lan, sel, t, p, pos);
  //cout << "Selected " << sel << "\n";
  spell_max_hits= 1000000;
  return sel;
}

range_set
spell (string lan, tree t, path p, path pos1, path pos2, int limit) {
  spell_initialize ();
  spell_max_hits= limit;
  range_set sel;
  //cout << "Spell " << what << "\n";
  spell ("text", lan, sel, t, p, pos1, pos2);
  //cout << "Selected " << sel << "\n";
  spell_max_hits= 1000000;
  return sel;
}
