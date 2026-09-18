/******************************************************************************
* MODULE     : totex_metadata.cpp
* DESCRIPTION: Shared document metadata support for native LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"

namespace {

using namespace latex_export_internal;

scheme_tree
metadata_make_inline (scheme_tree t) {
  if (!stree_list (t)) return t;
  if (func_is (t, "new-line", 0)) return stree_apply ("next-line");
  scheme_tree out (TUPLE);
  for (int i=0; i<N(t); ++i) out << metadata_make_inline (t[i]);
  return out;
}

scheme_tree
metadata_inline (scheme_tree t) {
  return tmtex_convert (metadata_make_inline (t));
}

scheme_tree
metadata_remove_line_feeds (scheme_tree t) {
  if (!stree_list (t) || N(t) == 0) return t;
  if (head_is (t, "next-line")) {
    scheme_tree out= stree_apply ("!concat");
    out << stree_apply ("tmSep") << stree_apply ("!linefeed");
    return out;
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(t); ++i) out << metadata_remove_line_feeds (t[i]);
  return out;
}

scheme_tree
metadata_replace_documents (scheme_tree t) {
  if (!stree_list (t) || N(t) == 0) return t;
  scheme_tree children (TUPLE);
  for (int i=1; i<N(t); ++i) children << metadata_replace_documents (t[i]);
  if (!head_is (t, "document")) {
    scheme_tree out (TUPLE);
    out << t[0];
    for (int i=0; i<N(children); ++i) out << children[i];
    return out;
  }
  scheme_tree out= stree_apply ("concat");
  for (int i=0; i<N(children); ++i) {
    if (i) out << stree_apply ("next-line");
    out << children[i];
  }
  return out;
}

bool
metadata_contains_tags (scheme_tree t, scheme_tree tags) {
  if (!stree_list (t) || N(t) == 0) return false;
  if (stree_list (tags)) {
    string head= atom_text (t[0]);
    for (int i=0; i<N(tags); ++i)
      if (atom_text (tags[i]) == head) return true;
  }
  for (int i=0; i<N(t); ++i)
    if (metadata_contains_tags (t[i], tags)) return true;
  return false;
}

bool
metadata_contains_stree (scheme_tree t, scheme_tree needle) {
  if (t == needle) return true;
  if (!stree_list (t)) return false;
  for (int i=0; i<N(t); ++i)
    if (metadata_contains_stree (t[i], needle)) return true;
  return false;
}

scheme_tree
metadata_select (string name, scheme_tree values) {
  scheme_tree out (TUPLE);
  if (!stree_list (values)) return out;
  for (int i=0; i<N(values); ++i)
    if (head_is (values[i], name)) out << values[i];
  return out;
}

scheme_tree
metadata_transform (string name, scheme_tree values) {
  scheme_tree selected= metadata_select (name, values);
  scheme_tree out (TUPLE);
  for (int i=0; i<N(selected); ++i) out << tmtex_convert (selected[i]);
  return out;
}

scheme_tree
metadata_field (string key, scheme_tree t) {
  if (!stree_list (t) || N(t) < 2) return scheme_tree (TUPLE);
  bool strip_lines= key == "doc-subtitle" || key == "doc-note" ||
                    key == "doc-misc" || key == "author-email" ||
                    key == "author-homepage" || key == "author-note" ||
                    key == "author-misc";
  scheme_tree source= strip_lines ? metadata_remove_line_feeds (t) : t;
  scheme_tree body= source[1];
  bool inline_body= key == "doc-title" || key == "doc-running-title" ||
                    key == "doc-subtitle" || key == "doc-date" ||
                    key == "doc-running-author" || key == "author-name" ||
                    key == "author-email" || key == "author-homepage";
  scheme_tree converted= inline_body ? metadata_inline (body) : tmtex_convert (body);

  string command;
  if      (key == "doc-title") command= "title";
  else if (key == "doc-running-title") command= "tmrunningtitle";
  else if (key == "doc-subtitle") command= "tmsubtitle";
  else if (key == "doc-note" || key == "author-note") command= "tmnote";
  else if (key == "doc-misc" || key == "author-misc") command= "tmmisc";
  else if (key == "doc-date") command= "date";
  else if (key == "doc-running-author") command= "tmrunningauthor";
  else if (key == "author-name") command= "author";
  else if (key == "author-affiliation") command= "tmaffiliation";
  else if (key == "author-email") command= "tmemail";
  else if (key == "author-homepage") command= "tmhomepage";
  else return scheme_tree (TUPLE);

  scheme_tree out= stree_apply (command);
  out << converted;
  return out;
}

scheme_tree
replace_exact (scheme_tree t, scheme_tree what, scheme_tree by) {
  if (t == what) return by;
  if (!stree_list (t)) return t;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(t); ++i) out << replace_exact (t[i], what, by);
  return out;
}

bool
next_occurrence (scheme_tree t, string tag, scheme_tree& found) {
  if (!stree_list (t) || N(t) == 0) return false;
  if (atom_text (t[0]) == tag) { found= t; return true; }
  for (int i=0; i<N(t); ++i)
    if (next_occurrence (t[i], tag, found)) return true;
  return false;
}

scheme_tree
metadata_make_references (scheme_tree values, string tag,
                          bool author, bool global_counter) {
  scheme_tree current (TUPLE);
  current << values;
  int n= global_counter ? latex_export_ref_count_get () : 1;
  string ref_tag= tag * "-ref";
  string label_tag= tag * "-label";

  for (;;) {
    scheme_tree occurrence;
    if (!next_occurrence (current[0], tag, occurrence)) break;
    string number= as_string (n++);
    scheme_tree reference= stree_apply (ref_tag);
    reference << stree_string (number);
    scheme_tree replaced= replace_exact (current[0], occurrence, reference);
    scheme_tree labels= N(current) > 1 ? current[1] : scheme_tree (TUPLE);
    scheme_tree next_labels (TUPLE);
    if (stree_list (labels))
      for (int i=0; i<N(labels); ++i) next_labels << labels[i];
    scheme_tree label= stree_apply (label_tag);
    label << stree_string (number)
          << (N(occurrence) > 1 ? occurrence[1] : stree_string (""));
    next_labels << label;
    current= scheme_tree (TUPLE);
    current << replaced << next_labels;
  }
  if (global_counter) latex_export_ref_count_set (n);

  scheme_tree result (TUPLE);
  scheme_tree refs= current[0];
  if (stree_list (refs)) for (int i=0; i<N(refs); ++i) result << refs[i];
  else result << refs;
  scheme_tree labels= N(current) > 1 ? current[1] : scheme_tree (TUPLE);
  if (author) {
    scheme_tree author_data= stree_apply ("author-data");
    if (stree_list (labels))
      for (int i=0; i<N(labels); ++i) author_data << labels[i];
    scheme_tree doc_author= stree_apply ("doc-author");
    doc_author << author_data;
    result << doc_author;
  }
  else if (stree_list (labels))
    for (int i=0; i<N(labels); ++i) result << labels[i];
  return result;
}

void
append_items (scheme_tree& out, scheme_tree values) {
  if (!stree_list (values)) return;
  for (int i=0; i<N(values); ++i) out << values[i];
}

scheme_tree metadata_make_author (scheme_tree args);
scheme_tree metadata_prepare_doc_data (scheme_tree values);
scheme_tree metadata_append_authors (scheme_tree values);
scheme_tree metadata_make_doc_data (scheme_tree args);
scheme_tree metadata_abstract_field (string key, scheme_tree t);
scheme_tree metadata_make_abstract_data (scheme_tree args);

scheme_tree
call_hook (string name, const array<scheme_tree>& args) {
  scheme_tree native;
  if (publisher_hook (name, args, native)) return native;

  scheme_tree packed (TUPLE);
  for (int i=0; i<N(args); ++i) packed << args[i];
  if (name == "tmtex-prepare-author-data")
    return N(args) > 0 ? args[0] : scheme_tree (TUPLE);
  if (name == "tmtex-make-author") return metadata_make_author (packed);
  if (name == "tmtex-prepare-doc-data")
    return N(args) > 0 ? metadata_prepare_doc_data (args[0]) : scheme_tree (TUPLE);
  if (name == "tmtex-append-authors")
    return N(args) > 0 ? metadata_append_authors (args[0]) : scheme_tree (TUPLE);
  if (name == "tmtex-make-doc-data") return metadata_make_doc_data (packed);
  if ((name == "tmtex-abstract" || name == "tmtex-abstract-keywords" ||
       name == "tmtex-abstract-acm" || name == "tmtex-abstract-arxiv" ||
       name == "tmtex-abstract-msc" || name == "tmtex-abstract-pacs") &&
      N(args) > 0)
    return metadata_abstract_field (name (6, N(name)), args[0]);
  if (name == "tmtex-make-abstract-data")
    return metadata_make_abstract_data (packed);
  return stree_string ("");
}

scheme_tree
metadata_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 11) return scheme_tree (TUPLE);
  scheme_tree names= args[0], affiliations= args[1], emails= args[2];
  scheme_tree urls= args[3], miscs= args[4], notes= args[5];

  scheme_tree name_values (TUPLE);
  if (stree_list (names))
    for (int i=0; i<N(names); ++i)
      if (stree_list (names[i]) && N(names[i]) > 1) name_values << names[i][1];
  scheme_tree joined= latex_tmtex_concat_Sep (name_values);

  scheme_tree content (TUPLE);
  append_items (content, joined);
  append_items (content, notes);
  append_items (content, miscs);
  append_items (content, affiliations);
  append_items (content, emails);
  append_items (content, urls);
  if (N(content) == 0) return scheme_tree (TUPLE);

  scheme_tree paragraph= stree_apply ("!paragraph");
  append_items (paragraph, content);
  scheme_tree author= stree_apply ("author");
  author << paragraph;
  return author;
}

scheme_tree
append_lists (scheme_tree first, scheme_tree second) {
  scheme_tree out (TUPLE);
  append_items (out, first);
  append_items (out, second);
  return out;
}

scheme_tree
metadata_doc_author (scheme_tree t) {
  if (!stree_list (t) || N(t) < 2 || !head_is (t[1], "author-data"))
    return scheme_tree (TUPLE);
  scheme_tree source (TUPLE);
  for (int i=1; i<N(t[1]); ++i) source << t[1][i];
  array<scheme_tree> prepare_args; prepare_args << source;
  scheme_tree values= call_hook ("tmtex-prepare-author-data", prepare_args);

  scheme_tree names= metadata_transform ("author-name", values);
  scheme_tree emails= metadata_transform ("author-email", values);
  scheme_tree urls= metadata_transform ("author-homepage", values);
  scheme_tree affs= metadata_transform ("author-affiliation", values);
  scheme_tree miscs= metadata_transform ("author-misc", values);
  scheme_tree notes= metadata_transform ("author-note", values);
  scheme_tree emails_ref= metadata_transform ("author-email-ref", values);
  scheme_tree urls_ref= metadata_transform ("author-homepage-ref", values);
  scheme_tree affs_ref= metadata_transform ("author-affiliation-ref", values);
  scheme_tree miscs_ref= metadata_transform ("author-misc-ref", values);
  scheme_tree notes_ref= metadata_transform ("author-note-ref", values);
  affs= append_lists (affs, metadata_transform ("author-affiliation-label", values));
  urls= append_lists (urls, metadata_transform ("author-homepage-label", values));
  miscs= append_lists (miscs, metadata_transform ("author-misc-label", values));
  notes= append_lists (notes, metadata_transform ("author-note-label", values));
  emails= append_lists (emails, metadata_transform ("author-email-label", values));

  array<scheme_tree> make_args;
  make_args << names << affs << emails << urls << miscs << notes
            << affs_ref << emails_ref << urls_ref << miscs_ref << notes_ref;
  return call_hook ("tmtex-make-author", make_args);
}

scheme_tree
metadata_prepare_doc_data (scheme_tree values) {
  scheme_tree out (TUPLE);
  if (!stree_list (values)) return out;
  for (int i=0; i<N(values); ++i) out << metadata_replace_documents (values[i]);
  return out;
}

bool
metadata_nonempty (scheme_tree value) {
  return !stree_list (value) || N(value) != 0;
}

scheme_tree
metadata_make_title (scheme_tree titles, scheme_tree subtitles,
                     scheme_tree notes, scheme_tree miscs) {
  scheme_tree title_values (TUPLE);
  if (stree_list (titles))
    for (int i=0; i<N(titles); ++i)
      if (stree_list (titles[i]) && N(titles[i]) > 1) title_values << titles[i][1];
  scheme_tree joined= latex_tmtex_concat_Sep (title_values);
  scheme_tree content (TUPLE);
  append_items (content, joined);
  append_items (content, subtitles);
  append_items (content, notes);
  append_items (content, miscs);
  scheme_tree result (TUPLE);
  if (N(content) == 0) return result;
  scheme_tree paragraph= stree_apply ("!paragraph"); append_items (paragraph, content);
  scheme_tree indent= stree_apply ("!indent"); indent << paragraph;
  scheme_tree title= stree_apply ("title"); title << indent;
  result << title;
  return result;
}

scheme_tree
metadata_append_authors (scheme_tree values) {
  scheme_tree authors (TUPLE);
  if (stree_list (values))
    for (int i=0; i<N(values); ++i)
      if (metadata_nonempty (values[i])) authors << values[i];
  scheme_tree result (TUPLE);
  if (N(authors) == 0) return result;

  scheme_tree concat= stree_apply ("!concat");
  if (N(authors) == 1) {
    if (stree_list (authors[0]))
      for (int i=1; i<N(authors[0]); ++i) concat << authors[0][i];
  }
  else {
    scheme_tree separator= stree_apply ("!concat");
    separator << stree_apply ("!linefeed") << stree_apply ("and")
              << stree_apply ("!linefeed");
    for (int i=0; i<N(authors); ++i) {
      if (i) concat << separator;
      if (stree_list (authors[i]) && N(authors[i]) > 1) concat << authors[i][1];
    }
  }
  scheme_tree indent= stree_apply ("!indent"); indent << concat;
  scheme_tree author= stree_apply ("author"); author << indent;
  result << author;
  return result;
}

scheme_tree
metadata_make_doc_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 12) return stree_string ("");
  scheme_tree titles= args[0], subtitles= args[1], authors= args[2];
  scheme_tree dates= args[3], miscs= args[4], notes= args[5];
  scheme_tree out= stree_apply ("!document");
  append_items (out, metadata_make_title (titles, subtitles, notes, miscs));
  array<scheme_tree> author_args; author_args << authors;
  append_items (out, call_hook ("tmtex-append-authors", author_args));
  if (!stree_list (dates) || N(dates) == 0) {
    scheme_tree date= stree_apply ("date"); date << stree_string (""); out << date;
  }
  else append_items (out, dates);
  out << stree_apply ("maketitle");
  return out;
}

scheme_tree
metadata_title_options (scheme_tree values) {
  scheme_tree selected= metadata_select ("doc-title-options", values);
  scheme_tree out (TUPLE);
  for (int i=0; i<N(selected); ++i)
    for (int j=1; j<N(selected[i]); ++j) out << selected[i][j];
  return out;
}

scheme_tree
metadata_doc_data (scheme_tree values) {
  array<scheme_tree> prepare_args; prepare_args << values;
  scheme_tree source= call_hook ("tmtex-prepare-doc-data", prepare_args);
  scheme_tree titles= metadata_transform ("doc-title", source);
  scheme_tree running_title= metadata_transform ("doc-running-title", source);
  scheme_tree subtitles= metadata_transform ("doc-subtitle", source);
  scheme_tree authors= metadata_transform ("doc-author", source);
  scheme_tree running_author= metadata_transform ("doc-running-author", source);
  scheme_tree dates= metadata_transform ("doc-date", source);
  scheme_tree miscs= metadata_transform ("doc-misc", source);
  scheme_tree notes= metadata_transform ("doc-note", source);
  scheme_tree subtitle_labels= metadata_transform ("doc-subtitle-label", source);
  scheme_tree date_labels= metadata_transform ("doc-date-label", source);
  scheme_tree misc_labels= metadata_transform ("doc-misc-label", source);
  scheme_tree note_labels= metadata_transform ("doc-note-label", source);
  subtitles= append_lists (subtitles, metadata_transform ("doc-subtitle-ref", source));
  dates= append_lists (dates, metadata_transform ("doc-date-ref", source));
  miscs= append_lists (miscs, metadata_transform ("doc-misc-ref", source));
  notes= append_lists (notes, metadata_transform ("doc-note-ref", source));
  array<scheme_tree> make_args;
  make_args << titles << subtitles << authors << dates << miscs << notes
            << subtitle_labels << date_labels << misc_labels << note_labels
            << running_title << running_author;
  return call_hook ("tmtex-make-doc-data", make_args);
}

scheme_tree
metadata_abstract_field (string key, scheme_tree t) {
  if (key == "abstract") {
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("abstract");
    scheme_tree out (TUPLE); out << begin;
    out << (N(t) > 1 ? tmtex_convert (t[1]) : stree_string (""));
    return out;
  }
  string command;
  if      (key == "abstract-keywords") command= "tmkeywords";
  else if (key == "abstract-acm") command= "tmacm";
  else if (key == "abstract-arxiv") command= "tmarxiv";
  else if (key == "abstract-msc") command= "tmmsc";
  else if (key == "abstract-pacs") command= "tmpacs";
  else return stree_string ("");
  scheme_tree converted (TUPLE);
  for (int i=1; i<N(t); ++i) converted << tmtex_convert (t[i]);
  scheme_tree out= stree_apply ("!concat"); out << stree_apply (command);
  for (int i=0; i<N(converted); ++i) {
    if (i) { scheme_tree g= stree_apply ("!group"); g << stree_apply ("tmsep"); out << g; }
    scheme_tree g= stree_apply ("!group"); g << converted[i]; out << g;
  }
  return out;
}

scheme_tree
metadata_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree all (TUPLE);
  append_items (all, args[5]); append_items (all, args[1]); append_items (all, args[2]);
  append_items (all, args[3]); append_items (all, args[4]); append_items (all, args[0]);
  if (N(all) == 0) return stree_string ("");
  scheme_tree out= stree_apply ("!document"); append_items (out, all); return out;
}

scheme_tree
map_hook_over_selected (string hook, string tag, scheme_tree values) {
  scheme_tree selected= metadata_select (tag, values), out (TUPLE);
  for (int i=0; i<N(selected); ++i) {
    array<scheme_tree> args; args << selected[i]; out << call_hook (hook, args);
  }
  return out;
}

scheme_tree
metadata_abstract_data (scheme_tree values) {
  scheme_tree acm= map_hook_over_selected ("tmtex-abstract-acm", "abstract-acm", values);
  scheme_tree arxiv= map_hook_over_selected ("tmtex-abstract-arxiv", "abstract-arxiv", values);
  scheme_tree msc= map_hook_over_selected ("tmtex-abstract-msc", "abstract-msc", values);
  scheme_tree pacs= map_hook_over_selected ("tmtex-abstract-pacs", "abstract-pacs", values);
  scheme_tree keywords= map_hook_over_selected ("tmtex-abstract-keywords", "abstract-keywords", values);
  scheme_tree abstract= map_hook_over_selected ("tmtex-abstract", "abstract", values);
  array<scheme_tree> args; args << keywords << acm << arxiv << msc << pacs << abstract;
  return call_hook ("tmtex-make-abstract-data", args);
}

scheme_tree
metadata_default (string key, scheme_tree args) {
  if (key == "prepare-author-data") return N(args) > 0 ? args[0] : scheme_tree (TUPLE);
  if (key == "make-author") return metadata_make_author (args);
  if (key == "doc-author") return N(args) > 0 ? metadata_doc_author (args[0]) : scheme_tree (TUPLE);
  if (key == "prepare-doc-data") return N(args) > 0 ? metadata_prepare_doc_data (args[0]) : scheme_tree (TUPLE);
  if (key == "append-authors") return N(args) > 0 ? metadata_append_authors (args[0]) : scheme_tree (TUPLE);
  if (key == "make-doc-data") return metadata_make_doc_data (args);
  if (key == "get-title-option") return N(args) > 0 ? metadata_title_options (args[0]) : scheme_tree (TUPLE);
  if (key == "doc-data") return N(args) > 0 ? metadata_doc_data (args[0]) : scheme_tree (TUPLE);
  if (key == "abstract-data") return N(args) > 0 ? metadata_abstract_data (args[0]) : stree_string ("");
  if (key == "abstract" || starts (key, "abstract-"))
    return N(args) > 0 ? metadata_abstract_field (key, args[0]) : stree_string ("");
  if (key == "make-abstract-data") return metadata_make_abstract_data (args);
  return stree_string ("");
}

} // namespace

scheme_tree latex_export_metadata_make_inline (scheme_tree t) {
  return metadata_make_inline (t);
}

scheme_tree latex_export_metadata_inline (scheme_tree t) {
  return metadata_inline (t);
}

scheme_tree latex_export_metadata_field (string key, scheme_tree t) {
  return metadata_field (key, t);
}

scheme_tree latex_export_metadata_select (string name, scheme_tree values) {
  return metadata_select (name, values);
}

scheme_tree latex_export_metadata_transform (string name, scheme_tree values) {
  return metadata_transform (name, values);
}

scheme_tree latex_export_metadata_remove_line_feeds (scheme_tree t) {
  return metadata_remove_line_feeds (t);
}

scheme_tree latex_export_metadata_replace_documents (scheme_tree t) {
  return metadata_replace_documents (t);
}

bool latex_export_metadata_contains_tags (scheme_tree t, scheme_tree tags) {
  return metadata_contains_tags (t, tags);
}

bool latex_export_metadata_contains_stree (scheme_tree t, scheme_tree needle) {
  return metadata_contains_stree (t, needle);
}

scheme_tree latex_export_metadata_make_references (
    scheme_tree values, string tag, bool author, bool global_counter) {
  return metadata_make_references (values, tag, author, global_counter);
}

scheme_tree latex_export_metadata_default (string key, scheme_tree args) {
  return metadata_default (key, args);
}
