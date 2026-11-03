// Title rendering and author transformations, formerly generic/title-*.scm.
// Copyright (C) 2012-2013 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Typesetting/Env/title_markup.hpp"
#include "node_metadata.hpp"
#include <vector>
#include <algorithm>

namespace {
using trees= std::vector<tree>;
bool tag (tree t, const char* name) {
  return is_compound (t, name);
}
trees fields (tree t, const char* name) {
  trees out;
  for (int i= 0; i < N (t); ++i)
    if (tag (t[i], name))
      out.push_back (t[i]);
  return out;
}
trees authors (tree t) {
  trees out;
  for (auto a : fields (t, "doc-author"))
    for (auto d : fields (a, "author-data"))
      out.push_back (d);
  return out;
}
void append (tree& out, const trees& values) {
  for (auto t : values)
    out << t;
}
tree joined (const trees& values, const char* label= "comma-separated") {
  if (values.empty ())
    return "";
  if (values.size () == 1)
    return values[0];
  tree out (as_tree_label (label));
  append (out, values);
  return out;
}
tree comma_names (const trees& names) {
  tree out (CONCAT);
  for (size_t i= 0; i < names.size (); ++i) {
    if (i)
      out << tree (", ");
    out << names[i];
  }
  return out;
}
tree excluding (tree t, std::initializer_list<const char*> names) {
  tree out (L (t));
  for (int i= 0; i < N (t); ++i) {
    bool omit= false;
    for (auto name : names)
      omit= omit || tag (t[i], name);
    if (!omit)
      out << t[i];
  }
  return out;
}
trees contents (tree t, const char* field) {
  trees out;
  for (auto f : fields (t, field))
    if (N (f))
      out.push_back (f[0]);
  return out;
}
struct note {
  tree source, symbol;
  string id;
};
using notes= std::vector<note>;
int lookup (const notes& values, tree item) {
  for (size_t i= 0; i < values.size (); ++i)
    if (athena::node::content_equal (values[i].source, item))
      return i;
  return -1;
}
tree reference (tree name, const note& n) {
  return compound ("doc-note-ref", n.symbol, compound ("noteref-sep"), n.id, name);
}
tree author_references (tree author, notes& known, trees& inline_notes,
                        trees& footnotes) {
  auto names= contents (author, "author-name");
  if (names.empty ())
    return tree ();
  tree name= names[0];
  for (auto kind : {"author-affiliation", "author-email", "author-homepage",
                    "author-misc", "author-note"}) {
    bool foot= std::string (kind) == "author-note";
    for (auto item : fields (author, kind)) {
      int at= lookup (known, item);
      if (at < 0) {
        string nr= as_string (known.size () + 1);
        note n{item,
               tree (WITH, "font-shape", "italic",
                     compound ("number", nr, foot ? "Alpha" : "alpha")),
               "authref-" * nr};
        known.push_back (n);
        at= known.size () - 1;
        tree entry=
            compound (foot ? string ("doc-footnote-text") : string (kind) * "-note",
                      n.symbol, n.id, item[0]);
        (foot ? footnotes : inline_notes).push_back (entry);
      }
      name= reference (name, known[at]);
    }
  }
  return name;
}
tree cluster (tree t, bool by_affiliation) {
  auto all= authors (t);
  if (all.size () <= 1)
    return t;
  std::vector<trees> groups;
  for (auto author : all) {
    bool added= false;
    for (auto& group : groups) {
      auto a= fields (author, "author-affiliation"),
           b= fields (group[0], "author-affiliation");
      bool same= a.size () == b.size ();
      for (size_t i= 0; same && i < a.size (); ++i)
        same= athena::node::content_equal (a[i], b[i]);
      if (!by_affiliation || same) {
        group.push_back (author);
        added= true;
        break;
      }
    }
    if (!added)
      groups.push_back ({author});
  }
  tree out= excluding (t, {"doc-author"});
  notes known;
  trees footnotes;
  for (const auto& group : groups) {
    trees names, inline_notes, preserved;
    for (auto author : group) {
      if (by_affiliation)
        author= excluding (author, {"author-affiliation"});
      if (by_affiliation && group.size () == 1) {
        for (auto name : {"author-email", "author-homepage", "author-misc"}) {
          auto extra= fields (author, name);
          preserved.insert (preserved.end (), extra.begin (), extra.end ());
        }
        author= excluding (author, {"author-email"});
      }
      if (!fields (author, "author-name").empty ())
        names.push_back (author_references (author, known, inline_notes, footnotes));
    }
    tree data= compound ("author-data", compound ("author-name", comma_names (names)));
    if (by_affiliation)
      append (data, fields (group[0], "author-affiliation"));
    append (data, inline_notes);
    append (data, preserved);
    out << compound ("doc-author", data);
  }
  append (out, footnotes);
  return out;
}
tree abbreviate (tree t, bool second_pass) {
  tree out= excluding (t, {"doc-author"});
  for (auto a : authors (t)) {
    trees names= contents (a, "author-name"), first_lines, affiliation;
    for (auto f : fields (a, "author-affiliation"))
      for (auto d : fields (f, "document")) {
        if (N (d))
          first_lines.push_back (d[0]);
        for (int i= 0; i < N (d); ++i)
          affiliation.push_back (d[i]);
      }
    tree data (as_tree_label ("author-data"));
    if (second_pass) {
      data << (affiliation.empty () ? compound ("author-name", joined (names))
                                    : compound ("author-name-affiliation",
                                                joined (names), joined (affiliation)));
      tree other= excluding (a, {"author-name", "author-affiliation"});
      for (int i= 0; i < N (other); ++i)
        data << other[i];
    } else {
      data << compound ("author-name", joined (names))
           << compound ("author-affiliation", tree (DOCUMENT, joined (first_lines)));
      trees other= affiliation.size () == 1 ? trees () : affiliation;
      for (auto field :
           {"author-email", "author-webpage", "author-note", "author-misc"}) {
        auto items= contents (a, field);
        other.insert (other.end (), items.begin (), items.end ());
      }
      if (!other.empty ())
        data << compound ("author-note", joined (other));
    }
    out << compound ("doc-author", data);
  }
  return out;
}
tree annotations (tree t, const notes& selected) {
  for (const auto& n : selected)
    t= reference (t, n);
  return t;
}
tree annotate (tree t, const notes& all) {
  if (tag (t, "doc-title") && N (t) == 1) {
    notes selected;
    for (auto n : all)
      if (tag (n.source, "doc-note"))
        selected.push_back (n);
    return compound ("doc-title", annotations (t[0], selected));
  }
  if (tag (t, "author-name") && N (t) == 1)
    return compound ("author-name", annotations (t[0], all));
  if (tag (t, "doc-author")) {
    notes selected;
    for (auto data : fields (t, "author-data"))
      for (auto item : fields (data, "author-note")) {
        int at= lookup (all, item);
        if (at >= 0)
          selected.push_back (all[at]);
      }
    tree out (L (t));
    for (int i= 0; i < N (t); ++i)
      out << annotate (t[i], selected);
    return out;
  }
  if (tag (t, "author-data")) {
    tree out (L (t));
    for (int i= 0; i < N (t); ++i)
      out << annotate (t[i], all);
    return out;
  }
  return t;
}
tree remove_notes (tree t) {
  tree out (L (t));
  for (int i= 0; i < N (t); ++i) {
    tree c= t[i];
    if ((tag (c, "doc-note") || tag (c, "author-note")) && N (c) == 1)
      continue;
    out << ((tag (c, "doc-author") || tag (c, "author-data")) ? remove_notes (c) : c);
  }
  return out;
}
tree add_notes (tree t) {
  trees found= fields (t, "doc-note");
  for (auto a : authors (t)) {
    auto n= fields (a, "author-note");
    found.insert (found.end (), n.begin (), n.end ());
  }
  notes all;
  for (auto item : found)
    if (lookup (all, item) < 0) {
      string nr= as_string (all.size () + 1);
      all.push_back ({item, compound ("number", nr, "fnsymbol"), "title-note-" * nr});
    }
  if (all.empty ())
    return t;
  tree annotated (L (t));
  for (int i= 0; i < N (t); ++i)
    annotated << annotate (t[i], all);
  tree out= remove_notes (annotated);
  for (auto n : all)
    out << compound ("doc-footnote-text", n.symbol, n.id, n.source[0]);
  return out;
}
tree remove_annotations (tree t) {
  if (is_compound (t, "doc-note-ref", 4))
    return remove_annotations (t[3]);
  if (tag (t, "new-line") || tag (t, "next-line"))
    return " ";
  if (!is_concat (t))
    return t;
  tree out (CONCAT);
  for (int i= 0; i < N (t); ++i)
    out << remove_annotations (t[i]);
  return out;
}
tree hidden_data (tree t) {
  tree out (CONCAT);
  append (out, fields (t, "doc-footnote-text"));
  for (auto title : fields (t, "doc-title"))
    out << compound ("doc-running-title", remove_annotations (title[0]));
  append (out, fields (t, "doc-running-title"));
  tree names= compound ("comma-separated");
  for (auto author : authors (t))
    for (int i= 0; i < N (author); ++i)
      if ((tag (author[i], "author-name") ||
           tag (author[i], "author-name-affiliation")) &&
          N (author[i]))
        names << remove_annotations (author[i][0]);
  out << compound ("doc-running-author", names);
  append (out, fields (t, "doc-running-author"));
  return out;
}
tree main_data (tree t) {
  tree out (DOCUMENT);
  append (out, fields (t, "doc-title"));
  append (out, fields (t, "doc-subtitle"));
  auto names= fields (t, "doc-author");
  if (names.size () <= 1)
    append (out, names);
  else {
    tree group= compound ("doc-authors");
    append (group, names);
    out << group;
  }
  for (auto field : {"doc-date", "doc-misc", "doc-inactive"})
    append (out, fields (t, field));
  return out;
}
tree ams_title (tree t) {
  tree hidden (CONCAT);
  for (auto field : {"doc-date", "doc-note"})
    for (auto item : fields (t, field))
      hidden << compound ("render-plain-footnote", item);
  tree rest= hidden_data (t);
  for (int i= 0; i < N (rest); ++i)
    hidden << rest[i];
  trees names;
  for (auto author : authors (t)) {
    tree data (DOCUMENT);
    for (auto field : {"author-affiliation", "author-email", "author-homepage",
                       "author-note", "author-misc"})
      append (data, fields (author, field));
    hidden << compound ("render-plain-footnote", data);
    auto n= fields (author, "author-name");
    names.insert (names.end (), n.begin (), n.end ());
  }
  tree main (DOCUMENT);
  append (main, fields (t, "doc-title"));
  append (main, fields (t, "doc-subtitle"));
  if (!names.empty ()) {
    tree joined_names (CONCAT);
    for (size_t i= 0; i < names.size (); ++i) {
      if (i) {
        joined_names << tree (names.size () > 2 ? ", " : " ");
        if (i == names.size () - 1)
          joined_names << compound ("localize", "and") << tree (" ");
      }
      joined_names << names[i];
    }
    main << compound ("author-name", names.size () == 1 ? names[0] : joined_names);
  }
  append (main, fields (t, "doc-misc"));
  append (main, fields (t, "doc-inactive"));
  return compound ("doc-make-rich-title", hidden, main);
}
void abstract_bodies (tree t, tree& out) {
  if (tag (t, "abstract") && N (t))
    out << t[0];
  else
    for (int i= 0; i < N (t); ++i)
      abstract_bodies (t[i], out);
}
} // namespace
tree native_doc_data (tree t, tree options) {
  std::vector<string> opts;
  for (auto field : fields (t, "doc-title-options"))
    for (int i= 0; i < N (field); ++i)
      if (is_atomic (field[i]))
        opts.push_back (field[i]->label);
  for (int i= 0; i < N (options); ++i)
    if (is_atomic (options[i]))
      opts.push_back (options[i]->label);
  auto has= [&] (const char* key) {
    return std::find (opts.begin (), opts.end (), string (key)) != opts.end ();
  };
  if (has ("ams-title"))
    return ams_title (t);
  if (has ("abbreviate-authors"))
    t= abbreviate (t, false);
  if (has ("cluster-all"))
    t= cluster (t, false);
  else if (has ("cluster-by-affiliation"))
    t= cluster (t, true);
  t= add_notes (t);
  if (has ("abbreviate-authors"))
    t= abbreviate (t, true);
  return compound ("doc-make-rich-title", hidden_data (t),
                   tree (DOCUMENT, main_data (t)));
}
tree native_author_data (tree t) {
  tree out (DOCUMENT);
  for (auto field :
       {"author-name", "author-name-affiliation", "author-affiliation",
        "author-affiliation-note", "author-email", "author-email-note",
        "author-homepage", "author-homepage-note", "author-misc", "author-misc-note"})
    append (out, fields (t, field));
  return out;
}
tree native_abstract_data (tree t) {
  tree options (DOCUMENT), body (DOCUMENT);
  for (auto field : {"abstract-keywords", "abstract-acm", "abstract-arxiv",
                     "abstract-pacs", "abstract-msc"})
    append (options, fields (t, field));
  abstract_bodies (t, body);
  return N (options) ? compound ("render-abstract*", body, options)
                     : compound ("render-abstract", body);
}
