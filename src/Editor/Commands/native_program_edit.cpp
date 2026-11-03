/******************************************************************************
 * MODULE     : native_program_edit.cpp
 * DESCRIPTION: Generic structured code editing, without language-specific rules
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * See the file LICENSE in the root directory.
 ******************************************************************************/

#include "Editor/Commands/native_program_edit.hpp"
#include "boot.hpp"
#include "Document/Convert/Generic/conversion_graph.hpp"
#include "analyze.hpp"
#include "modification.hpp"
#include "new_view.hpp"
#include "program_syntax.hpp"
#include <vector>

namespace
{
bool
enabled (const char *name)
{
  return get_preference (name) == "on";
}
int
tab_width ()
{
  int width = as_int (get_preference ("editor:verbatim:tabstop"));
  return width > 0 ? width : 4;
}
string
spaces (int n)
{
  string s;
  for (int i = 0; i < n; ++i)
    s << " ";
  return s;
}

struct program_context
{
  tree root;
  path document;
  int row = -1;
  explicit program_context (editor_rep *ed) : root (ed->the_root ())
  {
    path p = ed->the_path ();
    while (N (p) > N (ed->the_buffer_path ()))
      {
        path parent = path_up (p);
        if (is_func (subtree (root, parent), DOCUMENT))
          {
            document = parent;
            row = last_item (p);
            return;
          }
        p = parent;
      }
  }
};

// Traverse text formatting, but never treat an embedded formula or object as code.
bool
leaves (tree t, path p, std::vector<path> &out)
{
  if (is_atomic (t))
    {
      out.push_back (p);
      return true;
    }
  if (is_func (t, WITH) && N (t) > 0)
    return leaves (t[N (t) - 1], p * (N (t) - 1), out);
  if (is_compound (t, "strong", 1) || is_compound (t, "em", 1))
    return leaves (t[0], p * 0, out);
  if (is_func (t, CONCAT))
    {
      for (int i = 0; i < N (t); ++i)
        if (!leaves (t[i], p * i, out))
          return false;
      return true;
    }
  return false;
}

string
leading (tree root, path line, path cursor)
{
  std::vector<path> text;
  leaves (subtree (root, line), line, text);
  string result;
  for (path p : text)
    {
      if (path_inf (path_up (cursor), p))
        break;
      string s = subtree (root, p)->label;
      int end = p == path_up (cursor) ? last_item (cursor) : N (s);
      for (int i = 0; i < end; ++i)
        {
          if (s[i] != ' ' && s[i] != '\t')
            return result;
          result << s (i, i + 1);
        }
      if (p == path_up (cursor))
        break;
    }
  return result;
}

void
indent_line (tree root, path line, bool backwards)
{
  std::vector<path> text;
  leaves (subtree (root, line), line, text);
  if (!backwards)
    {
      if (!text.empty ())
        ::apply (root, mod_insert (text.front (), 0, spaces (tab_width ())));
      else
        {
          tree source = subtree (root, line);
          if (is_func (source, CONCAT))
            ::apply (root, mod_insert (line, 0, tree (CONCAT, spaces (tab_width ()))));
          else
            ::apply (root, mod_insert_node_preserving_identity (
                               line, 1, tree (CONCAT, spaces (tab_width ())), source));
        }
      return;
    }
  int remaining = tab_width ();
  for (path p : text)
    {
      string s = subtree (root, p)->label;
      int i = 0;
      while (i < N (s) && remaining > 0 && (s[i] == ' ' || s[i] == '\t'))
        {
          remaining -= s[i] == '\t' ? tab_width () : 1;
          ++i;
        }
      if (i > 0)
        ::apply (root, mod_remove (p, 0, i));
      if (remaining <= 0 || i < N (s))
        break;
    }
}

void
indent (editor_rep *ed, bool backwards, bool all)
{
  program_context c (ed);
  if (c.row < 0)
    return;
  int first = c.row, last = c.row;
  bool selected = ed->selection_active_normal ();
  if (all)
    {
      first = 0;
      last = N (subtree (c.root, c.document)) - 1;
    }
  else if (selected)
    {
      path a = ed->selection_get_start (), b = ed->selection_get_end ();
      if (!(c.document <= a) || !(c.document <= b))
        return;
      first = a[N (c.document)];
      last = b[N (c.document)];
      std::vector<path> end_text;
      leaves (subtree (c.root, c.document * last), c.document * last, end_text);
      if (last > first && !end_text.empty () && b == end_text.front () * 0)
        --last;
    }
  observer cursor = ed->position_new (ed->the_path ());
  observer a, b;
  if (selected)
    {
      a = ed->position_new (ed->selection_get_start ());
      b = ed->position_new (ed->selection_get_end ());
    }
  for (int row = last; row >= first; --row)
    indent_line (c.root, c.document * row, backwards);
  ed->go_to (ed->position_get (cursor));
  ed->position_delete (cursor);
  if (selected)
    {
      ed->selection_set_paths (ed->position_get (a), ed->position_get (b));
      ed->position_delete (a);
      ed->position_delete (b);
    }
}

struct character
{
  path where;
  char value;
};
void
characters (tree t, path p, std::vector<character> &out, int &budget)
{
  if (budget <= 0)
    return;
  --budget;
  if (is_atomic (t))
    {
      for (int i = 0; i < N (t->label) && budget > 0; ++i, --budget)
        if (program_token_at (t, i) == program_token::code)
          out.push_back ({ p * i, t->label[i] });
    }
  else if (is_func (t, WITH) && N (t) > 0)
    characters (t[N (t) - 1], p * (N (t) - 1), out, budget);
  else if (is_compound (t, "strong", 1) || is_compound (t, "em", 1))
    characters (t[0], p * 0, out, budget);
  else if (is_func (t, CONCAT))
    for (int i = 0; i < N (t); ++i)
      characters (t[i], p * i, out, budget);
}

bool
pair_at (editor_rep *ed, path &left, path &right, bool enclosing)
{
  program_context c (ed);
  if (c.row < 0)
    return false;
  tree doc = subtree (c.root, c.document);
  prepare_program_syntax (doc, ed->get_env_string ("prog-language"));
  std::vector<character> chars;
  int budget = 32768;
  for (int row = max (0, c.row - 128); row < min (N (doc), c.row + 129); ++row)
    characters (doc[row], c.document * row, chars, budget);
  std::vector<character> stack;
  path cursor = ed->the_path ();
  path a = ed->selection_active_normal () ? ed->selection_get_start () : cursor;
  path b = ed->selection_active_normal () ? ed->selection_get_end () : cursor;
  bool found = false;
  for (const auto &ch : chars)
    {
      if (ch.value == '(' || ch.value == '[' || ch.value == '{')
        stack.push_back (ch);
      else if (ch.value == ')' || ch.value == ']' || ch.value == '}')
        {
          if (stack.empty ())
            continue;
          auto open = stack.back ();
          stack.pop_back ();
          if (!((open.value == '(' && ch.value == ')')
                || (open.value == '[' && ch.value == ']')
                || (open.value == '{' && ch.value == '}')))
            {
              stack.clear ();
              continue;
            }
          path after = path_up (ch.where) * (last_item (ch.where) + 1);
          bool hit = enclosing ? (path_inf_eq (open.where, a) && path_inf_eq (b, after)
                                  && !(a == open.where && b == after))
                               : (cursor == open.where || cursor == ch.where
                                  || cursor == after);
          if (hit && (!found || path_inf (left, open.where)))
            {
              left = open.where;
              right = ch.where;
              found = true;
            }
        }
    }
  return found;
}

bool
textual (tree t)
{
  if (is_atomic (t))
    return true;
  if (!is_func (t, CONCAT) && !is_func (t, DOCUMENT))
    return false;
  for (int i = 0; i < N (t); ++i)
    if (!textual (t[i]))
      return false;
  return true;
}
}

void
program_indent (bool backwards)
{
  indent (get_current_editor ().operator->(), backwards, false);
}
void
program_indent_all (bool backwards)
{
  indent (get_current_editor ().operator->(), backwards, true);
}
void
program_remove_tabstop ()
{
  program_indent (true);
}

void
program_insert_tabstop ()
{
  editor ed = get_current_editor ();
  if (ed->selection_active_normal ())
    {
      program_indent (false);
      return;
    }
  program_context c (ed.operator->());
  int col = 0;
  if (ed->get_env_string ("mode") != "prog") {
    path cursor = ed->the_path ();
    tree leaf = subtree (c.root, path_up (cursor));
    if (is_atomic (leaf)) {
      for (int i = 0; i < last_item (cursor);) {
        col += leaf->label[i] == '\t' ? tab_width () - col % tab_width () : 1;
        tm_char_forwards (leaf->label, i);
      }
    }
  }
  else if (c.row >= 0)
    {
      std::vector<path> text;
      leaves (subtree (c.root, c.document * c.row), c.document * c.row, text);
      path cursor = ed->the_path ();
      for (path p : text)
        {
          if (path_inf (path_up (cursor), p))
            break;
          string s = subtree (c.root, p)->label;
          int end = p == path_up (cursor) ? last_item (cursor) : N (s);
          for (int i = 0; i < end;)
            {
              if (s[i] == '\t')
                col += tab_width () - col % tab_width ();
              else
                ++col;
              tm_char_forwards (s, i);
            }
          if (p == path_up (cursor))
            break;
        }
    }
  ed->insert_tree (spaces (tab_width () - col % tab_width ()));
}

bool
program_return (editor_rep *ed)
{
  if (ed->get_env_string ("mode") != "prog")
    return false;
  if (ed->selection_active_any ())
    ed->selection_cut ("none");
  program_context c (ed);
  if (c.row < 0)
    return false;
  string whitespace = leading (c.root, c.document * c.row, ed->the_path ());
  if (!ed->insert_return ())
    ed->insert_tree (whitespace);
  return true;
}

void
program_bracket (string left, string right, bool closing)
{
  editor ed = get_current_editor ();
  if (!enabled ("prog:automatic brackets")) {
    ed->insert_tree (closing ? right : left);
    return;
  }
  if (!closing && ed->selection_active_normal ()) {
    ed->var_insert_tree (tree (CONCAT, left, "", right), path (1, 0));
    return;
  }
  program_context c (ed.operator->());
  path p = ed->the_path ();
  tree leaf = subtree (c.root, path_up (p));
  int offset = last_item (p);
  if (c.row >= 0)
    prepare_program_syntax (subtree (c.root, c.document),
                            ed->get_env_string ("prog-language"));
  int escapes = 0;
  if (is_atomic (leaf))
    for (int i = offset - 1; i >= 0 && leaf->label[i] == '\\'; --i)
      ++escapes;
  bool code = c.row < 0 || program_insertion_token (
    subtree (c.root, c.document), c.row, leaf, offset,
    ed->get_env_string ("prog-language")) == program_token::code;
  if ((closing || left == right) && is_atomic (leaf)
      && offset < N (leaf->label) && leaf->label (offset, offset + N (right)) == right
      && escapes % 2 == 0)
    {
      path a, b;
      if (left == right || pair_at (ed.operator->(), a, b, false))
        {
          ed->go_to (path_up (p) * (offset + N (right)));
          return;
        }
    }
  if (closing || !code || escapes % 2 != 0)
    {
      ed->insert_tree (closing ? right : left);
      return;
    }
  ed->insert_tree (left * right, path (N (left)));
}

void
program_cursor_moved (editor_rep *ed)
{
  path a, b;
  if (enabled ("prog:highlight brackets") && ed->get_env_string ("mode") == "prog"
      && pair_at (ed, a, b, false))
    {
      range_set ranges;
      ranges << a << path_up (a) * (last_item (a) + 1) << b
             << path_up (b) * (last_item (b) + 1);
      ed->set_alt_selection ("brackets", ranges);
    }
  else if (N (ed->get_alt_selection ("brackets")) > 0)
    ed->cancel_alt_selection ("brackets");
}

void
program_select_enlarge ()
{
  editor ed = get_current_editor ();
  path a, b;
  if (enabled ("prog:select brackets") && pair_at (ed.operator->(), a, b, true))
    ed->selection_set_paths (a, path_up (b) * (last_item (b) + 1));
  else if (ed->selection_active_enlarging ())
    ed->select_enlarge ();
  else {
    ed->selection_cancel ();
    ed->selection_set_start ();
    ed->select_from_keyboard (true);
  }
}

bool
program_clipboard (editor_rep *ed, bool paste)
{
  if (ed->get_env_string ("mode") != "prog")
    return false;
  tree t = paste ? ed->selection_get ("primary") : ed->selection_get ();
  if (paste)
    {
      if (N (t) < 2)
        return false;
      t = t[1];
    }
  if (!textual (t))
    return false;
  string lan = ed->get_env_string ("prog-language"), format = "verbatim";
  string from = paste ? string ("texmacs") : lan, to = paste ? lan : string ("texmacs");
  if (conversion_graph_path (std::string ((from * "-snippet").data (), N (from) + 8),
                             std::string ((to * "-snippet").data (), N (to) + 8)))
    format = lan;
  struct format_scope
  {
    editor_rep *ed;
    bool paste;
    string old;
    ~format_scope ()
    {
      if (paste)
        ed->selection_set_import (old);
      else
        ed->selection_set_export (old);
    }
  } restore{ ed, paste,
             paste ? ed->selection_get_import () : ed->selection_get_export () };
  if (paste)
    {
      ed->selection_set_import (format);
      ed->selection_paste ("primary");
    }
  else
    {
      ed->selection_set_export (format);
      ed->selection_cut ("primary");
    }
  return true;
}
