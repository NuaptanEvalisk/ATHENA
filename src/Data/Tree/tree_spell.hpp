/******************************************************************************
* MODULE     : tree_spell.hpp
* DESCRIPTION: Resumable, viewport-first spell traversal owned by an editor
* COPYRIGHT  : (C) 2026  Felix Lian
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TREE_SPELL_H
#define TREE_SPELL_H
#include "tree_search.hpp"
#include "unicode_text.hpp"
#include "modification.hpp"
#include <map>
#include <optional>
#include <set>
#include <vector>

// Live spelling ranges refer to one atomic source leaf, not a structured selection.
std::optional<string> spelling_range_text (tree document, path first, path last);

// One admitted source entry. Absence means pending, not text mode or an empty
// spelling result. The provider only reads prepared layout; it must not execute.
struct spelling_context {
  std::shared_ptr<const athena::contracts::contract_scope> contracts;
  athena::contracts::evaluation_boundary_handle boundary;
  tree mode, language;
  bool same_entry (const spelling_context&) const;
};
using spelling_context_lookup= std::function<spelling_context (path)>;

class incremental_spell {
  struct frame {
    tree t;
    spelling_context context;
    path p, focus;
    int next= 0, pivot= 0, offset= 0, stop= 0, limit= 0;
    bool skip_first= false;
    std::vector<athena::text::word_span> words;
    std::size_t word= 0;
    frame (tree t, path p, path focus);
  };
  struct path_order {
    bool operator () (path a, path b) const { return path_less (a, b); }
  };
  std::vector<frame> stack;
  std::vector<path> waiting;
  std::map<path, path, path_order> errors;
public:
  bool initialized= false;
  void reset ();
  void start (tree t, path root, path focus);
  bool step (const spelling_context_lookup&, int word_budget= 64,
             int node_budget= 256, int milliseconds= 4);
  bool done () const { return stack.empty (); }
  bool pending () const { return !waiting.empty (); }
  bool pending_at (path source) const;
  range_set selections (path cursor) const;
};

// Committed marks survive rescans. Each source block has an independent scan;
// changing one block never discards the work or results of its siblings.
class incremental_spell_cache {
  struct job {
    incremental_spell scan;
    spelling_context context;
    std::uint64_t checked= 0;
    bool complete= false;
  };
  struct path_order {
    bool operator () (path a, path b) const;
  };
  std::map<int, job> jobs;
  std::set<int> runnable;
  std::map<path, path, path_order> errors;
  std::shared_ptr<const void> layout;
  std::uint64_t generation= 0;
  int review= 0, review_remaining= 0;
  void prepare (int block, const spelling_context_lookup&);
public:
  bool initialized= false;
  void reset ();
  void invalidate ();
  void change (tree before, modification mod);
  bool step (tree body, const spelling_context_lookup&,
             std::shared_ptr<const void> layout, path focus);
  range_set selections (path root) const;
};

#endif
