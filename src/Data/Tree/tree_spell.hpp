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
#include <map>
#include <vector>

class incremental_spell {
  struct frame {
    tree t, mode, lan;
    path p, focus;
    int next= 0, pivot= 0, offset= 0, begin= -1, stop= 0, limit= 0;
    frame (tree t, tree mode, tree lan, path p, path focus);
  };
  struct path_order {
    bool operator () (path a, path b) const { return path_less (a, b); }
  };
  std::vector<frame> stack;
  std::map<path, path, path_order> errors;
public:
  bool initialized= false;
  void reset ();
  void start (tree t, tree mode, tree lan, path root, path focus);
  bool step (int word_budget= 64, int node_budget= 256, int milliseconds= 4);
  bool done () const { return stack.empty (); }
  range_set selections (path cursor) const;
};

#endif
