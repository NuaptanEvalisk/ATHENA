
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
#include "language.hpp"
#include "vars.hpp"
#include "hashset.hpp"
#include "universal.hpp"
#include "tree_spell.hpp"
#include "utf8_edit.hpp"
#include "locale.hpp"
#include <chrono>

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
spell_ignored (tree t) {
  return spell_ignore->contains (L(t)) ||
    is_func (t, RAW_DATA) || is_func (t, NAMED_SYMBOL);
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

/******************************************************************************
* Front end
******************************************************************************/

bool spelling_context::same_entry (const spelling_context& other) const {
  return contracts && other.contracts && boundary == other.boundary &&
    mode == other.mode && language == other.language &&
    contracts->same_state (*other.contracts);
}

incremental_spell::frame::frame (tree t2, path p2, path focus2):
  t (t2), p (p2), focus (focus2) {
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
  waiting.clear ();
  errors.clear ();
  initialized= false;
}

void
incremental_spell::start (tree t, path root, path focus) {
  reset ();
  spell_initialize ();
  stack.emplace_back (t, root, focus);
  initialized= true;
}

bool
incremental_spell::step (const spelling_context_lookup& lookup,
                        int word_budget, int node_budget, int milliseconds) {
  auto deadline= std::chrono::steady_clock::now () +
                std::chrono::milliseconds (milliseconds);
  int chars= 4096;
  bool changed= false;
  while (!stack.empty () && word_budget > 0 && node_budget > 0 && chars > 0 &&
         std::chrono::steady_clock::now () < deadline) {
    frame& f= stack.back ();
    if (!is_atomic (f.t) && spell_ignored (f.t)) {
      stack.pop_back ();
      --node_budget;
      continue;
    }
    if (!f.context.contracts) {
      f.context= lookup (f.p);
      if (!f.context.contracts) {
        waiting.push_back (f.p);
        stack.pop_back ();
        --node_budget;
        continue;
      }
    }
    if (is_atomic (f.t)) {
      if (f.context.mode != "text" || !is_atomic (f.context.language) ||
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
        string locale= language_to_locale (f.context.language->label);
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
      if (!spell_string (f.context.language, s (word.begin, word.end))) {
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
      if (i < 0 || i >= N(f.t)) continue;
      // Inferred access may await analysis. A provisional child still has to
      // obtain its actual display context before any of its text is checked.
      athena::contracts::editing_query query {*f.context.contracts};
      if (!is_func (f.t, HIDDEN) && !query.mapped_accessible (f.t, i)) continue;
      path focus= !is_nil (f.focus) && i == f.focus->item?
                  f.focus->next: path ();
      frame child (f.t[i], f.p * i, focus);
      stack.push_back (std::move (child));
    }
  }
  return changed;
}

bool incremental_spell::pending_at (path source) const {
  for (path prefix: waiting)
    if (prefix <= source) return true;
  return false;
}

range_set
incremental_spell::selections (path cursor) const {
  range_set result;
  for (const auto& hit: errors)
    if (!(path_less_eq (hit.first, cursor) && path_less (cursor, hit.second)))
      result << hit.first << hit.second;
  return result;
}

namespace {
// Rebase retained marks through the same elementary operations as the source
// tree. Deleted content has no mark; surviving content keeps its presentation.
bool spell_rebase (path& pos, tree before, modification mod, bool end) {
  path p= root (mod);
  if (!(p <= pos)) return true;
  path q= pos / p;
  tree t= subtree (before, p);
  int i= is_nil (q)? 0: q->item;
  int at= (mod->k == MOD_ASSIGN || mod->k == MOD_ASSIGN_NODE ||
           mod->k == MOD_SET_METADATA)? 0:
          mod->k == MOD_INSERT_NODE? last_item (mod->p): index (mod);
  switch (mod->k) {
  case MOD_ASSIGN:
    if (!is_atomic (t) || !is_atomic (mod->t)) return false;
    {
      string a= t->label, b= mod->t->label;
      int left= 0, right= 0;
      while (left < N(a) && left < N(b) && a[left] == b[left]) ++left;
      while (left > 0 && !athena::text::scalar_boundary (
               {a.data (), std::size_t (N(a))}, left)) --left;
      while (right < N(a)-left && right < N(b)-left &&
             a[N(a)-right-1] == b[N(b)-right-1]) ++right;
      while (right > 0 && !athena::text::scalar_boundary (
               {a.data (), std::size_t (N(a))}, N(a)-right)) --right;
      if (i > left || (!end && i == left))
        i= i >= N(a)-right? i + N(b)-N(a): end? N(b)-right: left;
      q= path (i);
    }
    break;
  case MOD_INSERT:
    if (i > at || (i == at && (!end || !is_atomic (t))))
      q= path (i + (is_atomic (mod->t)? N(mod->t->label): N(mod->t)), q->next);
    break;
  case MOD_REMOVE:
    if (is_atomic (t)) q= path (i < at? i: max (at, i-argument (mod)), q->next);
    else if (i >= at && i < at+argument (mod)) return false;
    else if (i >= at+argument (mod)) q= path (i-argument (mod), q->next);
    break;
  case MOD_SPLIT:
    if (i > at) q= path (i+1, q->next);
    else if (i == at && !is_nil (q->next) &&
             (q->next->item > argument (mod) ||
              (q->next->item == argument (mod) && !end)))
      q= path (i+1, path (q->next->item-argument (mod), q->next->next));
    break;
  case MOD_JOIN:
    if (i > at+1) q= path (i-1, q->next);
    else if (i == at+1 && !is_nil (q->next)) {
      int length= is_atomic (t[at])? N(t[at]->label): N(t[at]);
      q= path (at, path (q->next->item+length, q->next->next));
    }
    break;
  case MOD_INSERT_NODE: q= path (at, q); break;
  case MOD_REMOVE_NODE:
    if (is_nil (q) || i != at) return false;
    q= q->next;
    break;
  default: break;
  }
  pos= p * q;
  return true;
}
}

bool incremental_spell_cache::path_order::operator () (path a, path b) const {
  // These keys include subtree prefixes, not just editor cursor positions.
  while (!is_nil (a) && !is_nil (b)) {
    if (a->item != b->item) return a->item < b->item;
    a= a->next;
    b= b->next;
  }
  return is_nil (a) && !is_nil (b);
}

void incremental_spell_cache::reset () {
  invalidate ();
  errors.clear ();
  initialized= false;
}

void incremental_spell_cache::invalidate () {
  jobs.clear ();
  runnable.clear ();
  layout.reset ();
  review= review_remaining= 0;
}

void incremental_spell_cache::change (tree before, modification mod) {
  if (!initialized) return;
  path p= root (mod);
  std::map<path, path, path_order> retained;
  auto first= errors.lower_bound (p), last= first;
  while (last != errors.end () && p <= last->first) ++last;
  for (auto it= first; it != last; ++it) {
    path a= it->first, b= it->second;
    if (spell_rebase (a, before, mod, false) &&
        spell_rebase (b, before, mod, true) &&
        path_up (a) == path_up (b) && path_less (a, b))
      retained.emplace (a, b);
  }
  errors.erase (first, last);
  errors.insert (retained.begin (), retained.end ());
  if (!is_nil (p)) {
    jobs[p->item]= job ();
    runnable.insert (p->item);
    return;
  }
  // Root child edits only change neighbouring blocks and their indices.
  if (mod->k == MOD_INSERT || mod->k == MOD_REMOVE ||
      mod->k == MOD_SPLIT || mod->k == MOD_JOIN) {
    int at= index (mod), removed= 0, added= 0;
    if (mod->k == MOD_INSERT) added= N(mod->t);
    if (mod->k == MOD_REMOVE) removed= argument (mod);
    if (mod->k == MOD_SPLIT) { removed= 1; added= 2; }
    if (mod->k == MOD_JOIN) { removed= 2; added= 1; }
    std::map<int, job> moved;
    for (auto& item: jobs) {
      int i= item.first;
      if (i < at) moved.emplace (i, std::move (item.second));
      else if (i >= at+removed)
        moved.emplace (i+added-removed, std::move (item.second));
    }
    for (int i=0; i<added; ++i) moved.emplace (at+i, job ());
    jobs.swap (moved);
    runnable.clear ();
    for (const auto& item: jobs)
      if (!item.second.complete) runnable.insert (item.first);
  }
  else invalidate ();
}

void incremental_spell_cache::prepare (
    int block, const spelling_context_lookup& lookup) {
  job& work= jobs[block];
  if (work.checked == generation) return;
  work.checked= generation;
  spelling_context entry= lookup (path (block));
  if (!entry.contracts) {
    work.scan.reset ();
    work.context= {};
    work.complete= false;
    runnable.erase (block);
    return;
  }
  if (!entry.same_entry (work.context) || work.scan.pending ()) {
    work.scan.reset ();
    work.complete= false;
  }
  work.context= std::move (entry);
  if (!work.complete) runnable.insert (block);
}

bool incremental_spell_cache::step (
    tree body, const spelling_context_lookup& lookup,
    std::shared_ptr<const void> current_layout, path focus) {
  if (!current_layout) return false;
  if (layout != current_layout) {
    layout= std::move (current_layout);
    ++generation;
    review_remaining= N(body);
  }
  initialized= true;
  auto deadline= std::chrono::steady_clock::now () + std::chrono::milliseconds (4);
  const auto review_deadline= deadline - std::chrono::milliseconds (2);
  bool changed= false;
  int pivot= is_nil (focus)? 0: max (0, min (N(body)-1, focus->item));
  if (N(body) > 0) prepare (pivot, lookup);
  // Layout changes can invalidate untouched later blocks. Review receipts in
  // bounded round-robin slices, without restarting at zero on each keystroke.
  for (int n=0; review_remaining > 0 && n<64 &&
       std::chrono::steady_clock::now () < review_deadline; ++n) {
    if (review >= N(body)) review= 0;
    prepare (review++, lookup);
    --review_remaining;
  }
  while (std::chrono::steady_clock::now () < deadline) {
    auto chosen= runnable.lower_bound (pivot);
    if (chosen != runnable.begin ()) {
      auto previous= chosen;
      --previous;
      if (chosen == runnable.end () || pivot-*previous < *chosen-pivot)
        chosen= previous;
    }
    if (chosen == runnable.end ()) break;
    int i= *chosen;
    prepare (i, lookup);
    job& work= jobs.at (i);
    if (!work.context.contracts) continue;
    if (!work.scan.initialized) {
      work.scan.start (body[i], path (),
        !is_nil (focus) && i == pivot? focus->next: path ());
    }
    int remaining= int (std::chrono::duration_cast<std::chrono::milliseconds> (
      deadline - std::chrono::steady_clock::now ()).count ());
    if (remaining <= 0) break;
    work.scan.step ([&] (path p) { return lookup (path (i, p)); },
                    64, 256, remaining);
    if (!work.scan.done ()) break;
    // Unprepared branches retain their old marks. A missing display context
    // never publishes a false "no spelling errors" result for that subtree.
    auto first= errors.lower_bound (path (i));
    auto last= errors.lower_bound (path (i+1));
    while (first != last) {
      if (work.scan.pending_at (first->first->next)) ++first;
      else first= errors.erase (first);
    }
    range_set result= work.scan.selections (path ());
    for (int n=0; n+1<N(result); n+=2)
      errors.emplace (path (i, result[n]), path (i, result[n+1]));
    runnable.erase (i);
    work.complete= true;
    if (!work.scan.pending ()) work.scan.reset ();
    changed= true;
  }
  return changed;
}

range_set incremental_spell_cache::selections (path root) const {
  range_set result;
  for (const auto& hit: errors) result << root * hit.first << root * hit.second;
  return result;
}
