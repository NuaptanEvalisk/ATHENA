/******************************************************************************
* MODULE     : totex_publishers.cpp
* DESCRIPTION: Publisher-specific native LaTeX export policy
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"

namespace {

using namespace latex_export_internal;

struct PublisherState {
  string source_style;
  bool revtex_clustered= false;
  bool revtex_showkeys= false;
  bool revtex_showpacs= false;
  bool elsevier_clustered= false;
  bool ieee_conference= false;
  bool ieee_clustered= false;
};

thread_local PublisherState publisher_state;

bool
ams_style () {
  return publisher_state.source_style == "amsart";
}

bool
revtex_style () {
  return publisher_state.source_style == "aip" ||
         publisher_state.source_style == "aps";
}

bool
elsevier_style () {
  return publisher_state.source_style == "elsarticle" ||
         publisher_state.source_style == "ifac";
}

bool
ifac_style () {
  return publisher_state.source_style == "ifac";
}

bool
acm_style () {
  static const char* names[]= {
    "acmsmall", "acmlarge", "acmtog", "sigconf", "sigchi", "sigplan", "acmart"
  };
  for (auto name: names)
    if (publisher_state.source_style == name) return true;
  return false;
}

bool
ieee_conf_style () {
  return publisher_state.source_style == "ieeeconf";
}

bool
ieee_tran_style () {
  return publisher_state.source_style == "ieeetran";
}

bool
ieee_style () {
  return ieee_conf_style () || ieee_tran_style ();
}

bool
beamer_style () {
  return publisher_state.source_style == "beamer";
}

bool
svjour_style () {
  return publisher_state.source_style == "svjour" ||
         publisher_state.source_style == "svjour3";
}

bool
llncs_style () {
  return publisher_state.source_style == "llncs";
}

bool
svmono_style () {
  return publisher_state.source_style == "svmono";
}

bool
springer_style () {
  return svjour_style () || llncs_style ();
}

bool
springer_any_style () {
  return springer_style () || svmono_style ();
}

void append_items (scheme_tree& out, scheme_tree values);
scheme_tree tag_value (string head, string value);

scheme_tree
copy_list (scheme_tree value) {
  scheme_tree out (TUPLE);
  if (stree_list (value))
    for (int i=0; i<N(value); ++i) out << value[i];
  return out;
}

bool
nonempty_value (scheme_tree value) {
  return !stree_list (value) || N(value) > 0;
}

scheme_tree
second_values (scheme_tree values) {
  scheme_tree out (TUPLE);
  if (!stree_list (values)) return out;
  for (int i=0; i<N(values); ++i)
    if (stree_list (values[i]) && N(values[i]) > 1) out << values[i][1];
  return out;
}

scheme_tree
comma_concat (scheme_tree values) {
  scheme_tree parts (TUPLE);
  if (stree_list (values))
    for (int i=0; i<N(values); ++i) {
      if (i) parts << stree_string (",");
      parts << values[i];
    }
  return latex_tex_concat_strings (parts);
}

scheme_tree
elsevier_note_ref (string prefix, scheme_tree value, bool author) {
  if (ifac_style ()) {
    if (stree_list (value)) {
      scheme_tree out= stree_apply ("!concat");
      for (int i=0; i<N(value); ++i) {
        scheme_tree ref= stree_apply ("thanksref"); ref << value[i]; out << ref;
      }
      return out;
    }
    scheme_tree out= stree_apply ("thanksref");
    out << stree_string (prefix * atom_text (value));
    return out;
  }
  scheme_tree out= stree_apply (author ? "fnref" : "tnoteref");
  out << (stree_list (value) ? comma_concat (value)
                             : stree_string (prefix * atom_text (value)));
  return out;
}

scheme_tree
option_command (string command, string option, scheme_tree body) {
  scheme_tree opt= stree_apply ("!option"); opt << stree_string (option);
  scheme_tree out= stree_apply (command); out << opt << body; return out;
}

scheme_tree
elsevier_label_output (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) < 2 || !string_atom (args[0]))
    return scheme_tree (TUPLE);
  string n= atom_text (args[0]);
  scheme_tree body= tmtex_convert (args[1]);
  if (key == "doc-subtitle-label")
    return option_command (ifac_style () ? "thankssubtitle" : "tsubtitletext",
                           "sub-" * n, body);
  if (key == "doc-note-label")
    return option_command (ifac_style () ? "thanks" : "tnotetext",
                           "note-" * n, body);
  if (key == "doc-date-label")
    return option_command (ifac_style () ? "thanksdate" : "tdatetext",
                           "date-" * n, body);
  if (key == "doc-misc-label")
    return option_command (ifac_style () ? "thanksmisc" : "tmisctext",
                           "misc-" * n, body);
  if (key == "author-note-label")
    return option_command (ifac_style () ? "thanks" : "fntext",
                           "author-note-" * n, body);
  if (key == "author-misc-label")
    return option_command (ifac_style () ? "thanksamisc" : "fmtext",
                           "author-misc-" * n, body);
  if (key == "author-affiliation-label")
    return option_command ("address", "affiliation-" * n, body);
  if (key == "author-email-label") {
    if (ifac_style ()) return option_command ("thanksemail", "author-email-" * n, body);
    scheme_tree out= stree_apply ("ead"); out << body; return out;
  }
  if (key == "author-homepage-label") {
    if (ifac_style ()) return option_command ("thankshomepage", "author-url-" * n, body);
    return option_command ("ead", "url", body);
  }
  return scheme_tree (TUPLE);
}

scheme_tree
elsevier_reference_output (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return scheme_tree (TUPLE);
  string prefix;
  bool author= false;
  if      (key == "doc-subtitle-ref") prefix= "sub-";
  else if (key == "doc-note-ref") prefix= "note-";
  else if (key == "doc-date-ref") prefix= "date-";
  else if (key == "doc-misc-ref") prefix= "misc-";
  else if (key == "author-note-ref") { prefix= "author-note-"; author= true; }
  else if (key == "author-misc-ref") { prefix= "author-misc-"; author= true; }
  else if (key == "author-affiliation-ref") { prefix= "affiliation-"; author= true; }
  else if (key == "author-email-ref") prefix= "author-email-";
  else if (key == "author-homepage-ref") prefix= "author-url-";
  else return scheme_tree (TUPLE);
  if (ifac_style () && (author || starts (key, "author-"))) author= false;
  return elsevier_note_ref (prefix, args[0], author);
}

scheme_tree
elsevier_author_field (string key, scheme_tree source) {
  if (!stree_list (source) || N(source) < 2) return scheme_tree (TUPLE);
  scheme_tree body= tmtex_convert (source[1]);
  if (key == "author-name") {
    scheme_tree out= stree_apply ("author"); out << body; return out;
  }
  if (key == "author-affiliation") {
    scheme_tree out= stree_apply ("address"); out << body; return out;
  }
  if (key == "author-email") {
    scheme_tree out= stree_apply ("ead"); out << body; return out;
  }
  if (key == "author-homepage") return option_command ("ead", "url", body);
  return scheme_tree (TUPLE);
}

scheme_tree
elsevier_prepare_doc_data (scheme_tree values) {
  publisher_state.elsevier_clustered=
    latex_export_metadata_contains_stree (
      values, tag_value ("doc-title-options", "cluster-by-affiliation")) ||
    latex_export_metadata_contains_stree (
      values, tag_value ("doc-title-options", "cluster-all"));

  scheme_tree out (TUPLE);
  if (stree_list (values))
    for (int i=0; i<N(values); ++i)
      out << (ifac_style () ? latex_export_metadata_replace_documents (values[i])
                            : values[i]);
  out= latex_export_metadata_make_references (out, "doc-subtitle", false, false);
  out= latex_export_metadata_make_references (out, "doc-note", false, false);
  out= latex_export_metadata_make_references (out, "doc-misc", false, false);
  out= latex_export_metadata_make_references (out, "doc-date", false, false);
  out= latex_export_metadata_make_references (out, "author-note", true, false);
  out= latex_export_metadata_make_references (out, "author-misc", true, false);
  if (ifac_style ()) {
    out= latex_export_metadata_make_references (out, "author-email", true, false);
    out= latex_export_metadata_make_references (out, "author-homepage", true, false);
  }
  if (publisher_state.elsevier_clustered)
    out= latex_export_metadata_make_references (out, "author-affiliation", true, false);
  return out;
}

scheme_tree
elsevier_make_doc_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 10) return stree_string ("");
  scheme_tree authors (TUPLE);
  if (stree_list (args[2]))
    for (int i=0; i<N(args[2]); ++i)
      if (nonempty_value (args[2][i])) authors << args[2][i];
  scheme_tree author_block (TUPLE);
  if (N(authors) > 0) {
    scheme_tree p= stree_apply ("!paragraph"); append_items (p, authors); author_block << p;
  }

  scheme_tree titles= latex_tmtex_concat_Sep (second_values (args[0]));
  scheme_tree notes (TUPLE);
  append_items (notes, args[1]); append_items (notes, args[3]);
  append_items (notes, args[4]); append_items (notes, args[5]);
  scheme_tree title_content (TUPLE); append_items (title_content, titles);
  if (N(notes) > 0) title_content << elsevier_note_ref ("", second_values (notes), false);

  scheme_tree result (TUPLE);
  if (N(title_content) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, title_content);
    scheme_tree title= stree_apply ("title"); title << concat; result << title;
  }
  append_items (result, args[6]); // subtitle labels
  append_items (result, args[9]); // note labels
  append_items (result, args[8]); // misc labels
  append_items (result, args[7]); // date labels
  append_items (result, author_block);
  if (N(result) == 0) return stree_string ("");
  scheme_tree out= stree_apply ("!document"); append_items (out, result); return out;
}

scheme_tree
elsevier_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 11) return scheme_tree (TUPLE);
  scheme_tree names= latex_tmtex_concat_Sep (second_values (args[0]));
  scheme_tree refs (TUPLE);
  if (ifac_style ()) {
    append_items (refs, args[7]); append_items (refs, args[8]);
  }
  append_items (refs, args[9]); append_items (refs, args[10]);

  scheme_tree content (TUPLE); append_items (content, names);
  if (N(refs) > 0)
    content << elsevier_note_ref ("", second_values (refs), !ifac_style ());

  scheme_tree paragraph= stree_apply ("!paragraph");
  if (N(content) > 0) {
    scheme_tree author= stree_apply ("author");
    if (stree_list (args[6]) && N(args[6]) > 0) {
      scheme_tree labels= second_values (args[6]);
      scheme_tree parts (TUPLE);
      for (int i=0; i<N(labels); ++i) {
        if (i) parts << stree_string (",");
        parts << labels[i];
      }
      scheme_tree option= stree_apply ("!option");
      scheme_tree concat= stree_apply ("!concat"); append_items (concat, parts);
      option << concat; author << option;
    }
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, content);
    author << concat; paragraph << author;
  }
  append_items (paragraph, args[1]); append_items (paragraph, args[2]);
  append_items (paragraph, args[3]); append_items (paragraph, args[4]);
  append_items (paragraph, args[5]);
  return N(paragraph) > 1 ? paragraph : scheme_tree (TUPLE);
}

scheme_tree
sep_joined_source (scheme_tree value) {
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  scheme_tree body= stree_apply ("!concat");
  for (int i=0; i<N(converted); ++i) {
    if (i) {
      scheme_tree sep= stree_apply ("!concat");
      sep << stree_apply ("sep") << stree_string (" ");
      body << sep;
    }
    body << converted[i];
  }
  return body;
}

scheme_tree
elsevier_abstract_field (string key, scheme_tree value) {
  scheme_tree body= sep_joined_source (value);
  if (key == "tmtex-abstract-keywords") {
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("keyword");
    scheme_tree out (TUPLE); out << begin << body; return out;
  }
  scheme_tree out= stree_apply ("!concat");
  out << stree_apply (key == "tmtex-abstract-msc" ? "MSC" : "PACS")
      << stree_string (" ") << body;
  return out;
}

scheme_tree
elsevier_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree keywords= copy_list (args[0]);
  bool classes= (stree_list (args[1]) && N(args[1]) > 0) ||
                (stree_list (args[2]) && N(args[2]) > 0) ||
                (stree_list (args[3]) && N(args[3]) > 0) ||
                (stree_list (args[4]) && N(args[4]) > 0);
  if (classes) {
    scheme_tree body= stree_apply ("!document");
    if (stree_list (keywords))
      for (int i=0; i<N(keywords); ++i)
        if (stree_list (keywords[i]) && N(keywords[i]) > 1) body << keywords[i][1];
    append_items (body, args[4]); append_items (body, args[3]);
    append_items (body, args[1]); append_items (body, args[2]);
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("keyword");
    scheme_tree env (TUPLE); env << begin << body;
    keywords= scheme_tree (TUPLE); keywords << env;
  }
  scheme_tree out= stree_apply ("!document");
  append_items (out, args[5]); append_items (out, keywords); return out;
}

scheme_tree
elsevier_equation_output (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  latex_export_env_set ("mode", object ("math"));
  scheme_tree body= tmtex_convert (args[0]);
  latex_export_env_reset ("mode");
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string (key == "equation" ? "eqnarray" : "eqnarray*");
  scheme_tree out (TUPLE); out << begin << body; return out;
}

bool
elsevier_frontmatter_node (scheme_tree value) {
  return head_is (value, "abstract-data") || head_is (value, "doc-data") ||
         head_is (value, "abstract");
}

scheme_tree
elsevier_create_frontmatter (scheme_tree value) {
  if (!stree_list (value) || N(value) < 2) return value;
  bool has_frontmatter= false;
  for (int i=1; i<N(value); ++i)
    if (elsevier_frontmatter_node (value[i])) {
      has_frontmatter= true;
      break;
    }
  if (!has_frontmatter) {
    scheme_tree out (TUPLE); out << value[0];
    for (int i=1; i<N(value); ++i) out << elsevier_create_frontmatter (value[i]);
    return out;
  }

  scheme_tree out (TUPLE); out << value[0];
  int i= 1;
  while (i < N(value)) {
    bool front= elsevier_frontmatter_node (value[i]);
    int j= i + 1;
    while (j < N(value) && elsevier_frontmatter_node (value[j]) == front) ++j;
    scheme_tree block (TUPLE); block << value[0];
    for (int k=i; k<j; ++k) block << value[k];
    if (front) {
      scheme_tree wrapper= stree_apply ("elsevier-frontmatter");
      wrapper << block;
      out << wrapper;
    }
    else out << block;
    i= j;
  }
  return out;
}

scheme_tree
elsevier_frontmatter_output (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("frontmatter");
  scheme_tree out (TUPLE); out << begin << tmtex_convert (args[0]); return out;
}

void
append_items (scheme_tree& out, scheme_tree values) {
  if (!stree_list (values)) return;
  for (int i=0; i<N(values); ++i) out << values[i];
}

scheme_tree
source_apply (string head, scheme_tree args) {
  scheme_tree out= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << args[i];
  return out;
}

bool
contains_head (scheme_tree value, string head) {
  if (!stree_list (value)) return false;
  if (N(value) > 0 && is_atomic (value[0]) && value[0]->label == head)
    return true;
  for (int i=0; i<N(value); ++i)
    if (contains_head (value[i], head)) return true;
  return false;
}

bool
contains_exact (scheme_tree value, scheme_tree needle) {
  if (value == needle) return true;
  if (!stree_list (value)) return false;
  for (int i=0; i<N(value); ++i)
    if (contains_exact (value[i], needle)) return true;
  return false;
}

scheme_tree
tag_value (string head, string value) {
  scheme_tree out= stree_apply (head);
  out << stree_string (value);
  return out;
}

scheme_tree
insert_maketitle_after (scheme_tree value, string head) {
  if (!stree_list (value)) return value;
  if (N(value) > 0 && is_atomic (value[0]) && value[0]->label == head) {
    scheme_tree out= stree_apply ("!document");
    out << value << stree_apply ("maketitle");
    return out;
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i)
    out << (i == 0 ? value[i] : insert_maketitle_after (value[i], head));
  return out;
}

scheme_tree
converted_child (scheme_tree value) {
  return stree_list (value) && N(value) > 1
           ? tmtex_convert (value[1]) : stree_string ("");
}

scheme_tree
ams_metadata_field (string key, scheme_tree value) {
  if (key == "tmtex-doc-running-title") return converted_child (value);

  bool strip_lines= key == "tmtex-doc-note" || key == "tmtex-doc-misc" ||
                    key == "tmtex-author-email" ||
                    key == "tmtex-author-homepage" ||
                    key == "tmtex-author-note" || key == "tmtex-author-misc";
  scheme_tree source= strip_lines
    ? latex_export_metadata_remove_line_feeds (value) : value;
  scheme_tree body= converted_child (source);
  string command;
  if      (key == "tmtex-doc-subtitle") command= "tmsubtitle";
  else if (key == "tmtex-doc-note" || key == "tmtex-author-note")
    command= "tmnote";
  else if (key == "tmtex-doc-misc" || key == "tmtex-author-misc")
    command= "tmmisc";
  else if (key == "tmtex-doc-date") command= "date";
  else if (key == "tmtex-author-affiliation") command= "address";
  else if (key == "tmtex-author-email") command= "email";
  else if (key == "tmtex-author-homepage") command= "urladdr";
  else return scheme_tree (TUPLE);
  scheme_tree out= stree_apply (command);
  out << body;
  return out;
}

scheme_tree
ams_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return scheme_tree (TUPLE);
  scheme_tree names= args[0];
  scheme_tree name_bodies (TUPLE);
  if (stree_list (names))
    for (int i=0; i<N(names); ++i)
      if (stree_list (names[i]) && N(names[i]) > 1)
        name_bodies << names[i][1];

  scheme_tree joined (TUPLE);
  for (int i=0; i<N(name_bodies); ++i) {
    if (i) joined << stree_apply ("tmSep");
    joined << name_bodies[i];
  }
  scheme_tree result (TUPLE);
  for (int i=0; i<N(joined); ++i) {
    scheme_tree author= stree_apply ("author"); author << joined[i];
    result << author;
  }
  append_items (result, args[1]); // affiliations
  append_items (result, args[2]); // emails
  append_items (result, args[3]); // urls
  append_items (result, args[5]); // notes
  append_items (result, args[4]); // misc
  if (N(result) == 0) return scheme_tree (TUPLE);
  scheme_tree paragraph= stree_apply ("!paragraph");
  append_items (paragraph, result);
  return paragraph;
}

scheme_tree
ams_make_doc_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 12) return scheme_tree (TUPLE);
  scheme_tree titles= args[0], subtitles= args[1], authors= args[2];
  scheme_tree dates= args[3], miscs= args[4], notes= args[5];
  scheme_tree running_title= args[10];

  scheme_tree title_bodies (TUPLE);
  if (stree_list (titles))
    for (int i=0; i<N(titles); ++i)
      if (stree_list (titles[i]) && N(titles[i]) > 1)
        title_bodies << titles[i][1];
  scheme_tree joined_titles= latex_tmtex_concat_Sep (title_bodies);

  scheme_tree title_command;
  if (N(joined_titles) > 0) {
    title_command= stree_apply ("title");
    if (stree_list (running_title) && N(running_title) > 0) {
      scheme_tree option= stree_apply ("!option");
      scheme_tree running= latex_tmtex_concat_Sep (running_title);
      append_items (option, running);
      title_command << option;
    }
    append_items (title_command, joined_titles);
  }

  scheme_tree title_data (TUPLE);
  if (!is_nil (title_command)) title_data << title_command;
  append_items (title_data, subtitles);
  append_items (title_data, notes);
  append_items (title_data, miscs);

  scheme_tree authors_nonempty (TUPLE);
  if (stree_list (authors))
    for (int i=0; i<N(authors); ++i)
      if (stree_list (authors[i]) && N(authors[i]) > 0)
        authors_nonempty << authors[i];
  if (N(title_data) == 0 && N(authors_nonempty) == 0 &&
      (!stree_list (dates) || N(dates) == 0))
    return scheme_tree (TUPLE);

  scheme_tree out= stree_apply ("!document");
  if (N(title_data) > 0) {
    scheme_tree paragraph= stree_apply ("!paragraph");
    append_items (paragraph, title_data);
    out << paragraph;
  }
  append_items (out, authors_nonempty);
  append_items (out, dates);
  return out;
}

scheme_tree
converted_joined (scheme_tree value, bool capital_separator) {
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  return capital_separator ? latex_tmtex_concat_Sep (converted)
                           : latex_tmtex_concat_sep (converted);
}

scheme_tree
ams_abstract_field (string key, scheme_tree value) {
  string command= key == "tmtex-abstract-keywords" ? string ("keywords")
                                                      : string ("subjclass");
  scheme_tree joined= converted_joined (
    value, key == "tmtex-abstract-msc");
  scheme_tree out= stree_apply (command);
  append_items (out, joined);
  return out;
}

scheme_tree
ams_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree keywords= args[0], acm= args[1], arxiv= args[2];
  scheme_tree msc= args[3], pacs= args[4], abstract= args[5];
  scheme_tree classes (TUPLE);
  append_items (classes, acm); append_items (classes, arxiv); append_items (classes, pacs);

  scheme_tree abstract_envs (TUPLE);
  if (stree_list (abstract) && N(abstract) > 0) {
    scheme_tree body= stree_apply ("!document");
    for (int i=0; i<N(abstract); ++i)
      if (stree_list (abstract[i]) && N(abstract[i]) > 1) body << abstract[i][1];
    append_items (body, classes);
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("abstract");
    scheme_tree env (TUPLE); env << begin << body;
    abstract_envs << env;
  }
  else if (N(classes) > 0) {
    scheme_tree body= stree_apply ("document"); append_items (body, classes);
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("abstract");
    scheme_tree env (TUPLE); env << begin << body;
    abstract_envs << env;
  }

  scheme_tree result (TUPLE);
  append_items (result, abstract_envs);
  append_items (result, msc);
  append_items (result, keywords);
  if (N(result) == 0) return stree_string ("");
  scheme_tree out= stree_apply ("!document"); append_items (out, result); return out;
}

scheme_tree
revtex_metadata_field (string key, scheme_tree value) {
  bool strip_lines= key == "tmtex-doc-note" || key == "tmtex-doc-misc" ||
                    key == "tmtex-author-email" ||
                    key == "tmtex-author-homepage" ||
                    key == "tmtex-author-note" || key == "tmtex-author-misc";
  scheme_tree source= strip_lines
    ? latex_export_metadata_remove_line_feeds (value) : value;
  scheme_tree body= converted_child (source);
  if (key == "tmtex-author-affiliation" &&
      source == source_apply ("author-affiliation",
                              scheme_tree (TUPLE, stree_apply ("noaffiliation"))))
    return stree_apply ("noaffiliation");

  string command;
  if      (key == "tmtex-doc-subtitle") command= "tmsubtitle";
  else if (key == "tmtex-doc-note" || key == "tmtex-author-note") command= "tmnote";
  else if (key == "tmtex-doc-misc" || key == "tmtex-author-misc") command= "tmmisc";
  else if (key == "tmtex-doc-date") command= "date";
  else if (key == "tmtex-author-affiliation") command= "affiliation";
  else if (key == "tmtex-author-email") command= "email";
  else if (key == "tmtex-author-homepage") command= "homepage";
  else return scheme_tree (TUPLE);

  scheme_tree out= stree_apply (command);
  if (key == "tmtex-author-email" || key == "tmtex-author-homepage") {
    scheme_tree option= stree_apply ("!option");
    option << stree_string (key == "tmtex-author-email" ? "Email: " : "Web: ");
    out << option;
  }
  out << body;
  return out;
}

scheme_tree
revtex_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return scheme_tree (TUPLE);
  scheme_tree names= args[0], affiliations= args[1], emails= args[2];
  scheme_tree urls= args[3], miscs= args[4], notes= args[5];
  scheme_tree affs= affiliations;
  if ((!stree_list (affs) || N(affs) == 0) && !publisher_state.revtex_clustered) {
    affs= scheme_tree (TUPLE); affs << stree_apply ("noaffiliation");
  }

  scheme_tree name_bodies (TUPLE);
  if (stree_list (names))
    for (int i=0; i<N(names); ++i)
      if (stree_list (names[i]) && N(names[i]) > 1) name_bodies << names[i][1];
  scheme_tree separated= latex_tmtex_concat_Sep (name_bodies);

  scheme_tree out= stree_apply ("!paragraph");
  if (stree_list (separated))
    for (int i=0; i<N(separated); ++i) {
      scheme_tree author= stree_apply ("author"); author << separated[i]; out << author;
    }
  append_items (out, emails); append_items (out, urls); append_items (out, notes);
  append_items (out, miscs); append_items (out, affs);
  return N(out) > 1 ? out : scheme_tree (TUPLE);
}

scheme_tree
revtex_make_doc_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return scheme_tree (TUPLE);
  scheme_tree title_data (TUPLE);
  append_items (title_data, args[0]); append_items (title_data, args[1]);
  append_items (title_data, args[5]); append_items (title_data, args[4]);
  scheme_tree authors (TUPLE);
  if (stree_list (args[2]))
    for (int i=0; i<N(args[2]); ++i)
      if (stree_list (args[2][i]) && N(args[2][i]) > 0) authors << args[2][i];
  scheme_tree dates= args[3];
  if (N(title_data) == 0 && N(authors) == 0 &&
      (!stree_list (dates) || N(dates) == 0)) return scheme_tree (TUPLE);

  scheme_tree out= stree_apply ("!document");
  if (N(title_data) > 0) {
    scheme_tree p= stree_apply ("!paragraph"); append_items (p, title_data); out << p;
  }
  append_items (out, authors); append_items (out, dates);
  return out;
}

scheme_tree
revtex_abstract_field (string key, scheme_tree value) {
  string command= key == "tmtex-abstract-keywords" ? string ("keywords")
                                                     : string ("pacs");
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  scheme_tree joined= latex_tmtex_concat_sep (converted);
  scheme_tree out= stree_apply (command); append_items (out, joined); return out;
}

scheme_tree
revtex_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree classes (TUPLE);
  append_items (classes, args[1]); append_items (classes, args[2]);
  append_items (classes, args[3]);
  scheme_tree abstract= args[5], abstract_envs (TUPLE);
  if (stree_list (abstract) && N(abstract) > 0) {
    scheme_tree body= stree_apply ("!document");
    for (int i=0; i<N(abstract); ++i)
      if (stree_list (abstract[i]) && N(abstract[i]) > 1) body << abstract[i][1];
    append_items (body, classes);
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("abstract");
    scheme_tree env (TUPLE); env << begin << body; abstract_envs << env;
  }
  else if (N(classes) > 0) {
    scheme_tree body= stree_apply ("document"); append_items (body, classes);
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("abstract");
    scheme_tree env (TUPLE); env << begin << body; abstract_envs << env;
  }
  scheme_tree result (TUPLE); append_items (result, abstract_envs);
  append_items (result, args[4]); append_items (result, args[0]);
  if (N(result) == 0) return stree_string ("");
  scheme_tree out= stree_apply ("!document"); append_items (out, result); return out;
}

scheme_tree
select_source_tags (scheme_tree values, string tag) {
  scheme_tree out (TUPLE);
  if (!stree_list (values)) return out;
  for (int i=0; i<N(values); ++i)
    if (head_is (values[i], tag)) out << values[i];
  return out;
}

bool
source_list_contains (scheme_tree values, scheme_tree needle) {
  if (!stree_list (values)) return false;
  for (int i=0; i<N(values); ++i)
    if (values[i] == needle) return true;
  return false;
}

scheme_tree
remove_source_tags (scheme_tree author, scheme_tree tags) {
  if (!stree_list (author) || N(author) == 0) return author;
  scheme_tree out (TUPLE); out << author[0];
  for (int i=1; i<N(author); ++i)
    if (!source_list_contains (tags, author[i])) out << author[i];
  return out;
}

scheme_tree
add_noaffiliation_source (scheme_tree author) {
  if (!stree_list (author) || N(author) == 0) return author;
  scheme_tree out= copy_list (author);
  scheme_tree noaff= stree_apply ("noaffiliation");
  scheme_tree affiliation= stree_apply ("author-affiliation");
  affiliation << noaff;
  out << affiliation;
  return out;
}

scheme_tree
wrap_doc_author (scheme_tree author) {
  scheme_tree out= stree_apply ("doc-author"); out << author; return out;
}

scheme_tree
revtex_cluster_authors (scheme_tree authors) {
  scheme_tree remaining= copy_list (authors);
  scheme_tree result (TUPLE);
  while (N(remaining) > 0) {
    scheme_tree first= remaining[0];
    scheme_tree affiliations= select_source_tags (first, "author-affiliation");
    scheme_tree same (TUPLE), others (TUPLE);
    for (int i=0; i<N(remaining); ++i) {
      scheme_tree aff= select_source_tags (remaining[i], "author-affiliation");
      if (aff == affiliations) same << remaining[i];
      else others << remaining[i];
    }

    int last_index= N(same) - 1;
    for (int i=0; i<last_index; ++i)
      result << wrap_doc_author (remove_source_tags (same[i], affiliations));
    scheme_tree last= same[last_index];
    if (N(affiliations) == 0) last= add_noaffiliation_source (last);
    result << wrap_doc_author (last);
    remaining= others;
  }
  return result;
}

scheme_tree
convert_selected_revtex_field (scheme_tree values, string tag, string hook) {
  scheme_tree selected= select_source_tags (values, tag), out (TUPLE);
  for (int i=0; i<N(selected); ++i) {
    if (hook == "doc-title") out << latex_export_metadata_field ("doc-title", selected[i]);
    else out << revtex_metadata_field ("tmtex-" * hook, selected[i]);
  }
  return out;
}

scheme_tree
revtex_clustered_doc_data (scheme_tree args) {
  scheme_tree values (TUPLE);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i)
      values << latex_export_metadata_replace_documents (args[i]);

  scheme_tree subtitles= convert_selected_revtex_field (values, "doc-subtitle", "doc-subtitle");
  scheme_tree notes= convert_selected_revtex_field (values, "doc-note", "doc-note");
  scheme_tree miscs= convert_selected_revtex_field (values, "doc-misc", "doc-misc");
  scheme_tree dates= convert_selected_revtex_field (values, "doc-date", "doc-date");
  scheme_tree titles= convert_selected_revtex_field (values, "doc-title", "doc-title");

  scheme_tree selected_authors= select_source_tags (values, "doc-author");
  scheme_tree author_data (TUPLE);
  for (int i=0; i<N(selected_authors); ++i)
    if (stree_list (selected_authors[i]) && N(selected_authors[i]) > 1)
      author_data << selected_authors[i][1];
  scheme_tree clustered= revtex_cluster_authors (author_data);
  scheme_tree converted_authors= stree_apply ("!document");
  for (int i=0; i<N(clustered); ++i) {
    scheme_tree wrapper (TUPLE); wrapper << clustered[i];
    converted_authors << latex_export_metadata_default ("doc-author", wrapper);
  }
  scheme_tree authors (TUPLE); authors << converted_authors;

  scheme_tree packed (TUPLE);
  packed << titles << subtitles << authors << dates << miscs << notes;
  return revtex_make_doc_data (packed);
}

scheme_tree
acm_metadata_field (string key, scheme_tree source) {
  if (!stree_list (source) || N(source) < 2) return scheme_tree (TUPLE);
  bool strip_lines= key == "doc-note" || key == "doc-misc" ||
                    key == "author-email" || key == "author-homepage" ||
                    key == "author-note" || key == "author-misc";
  scheme_tree input= strip_lines
    ? latex_export_metadata_remove_line_feeds (source) : source;
  scheme_tree body= input[1];
  scheme_tree converted= key == "author-name"
    ? latex_export_metadata_inline (body) : tmtex_convert (body);
  string command;
  if      (key == "doc-subtitle") command= "subtitle";
  else if (key == "doc-note") command= "titlenote";
  else if (key == "doc-misc" || key == "author-misc") command= "tmacmmisc";
  else if (key == "doc-date") command= "date";
  else if (key == "author-name") command= "author";
  else if (key == "author-email") command= "email";
  else if (key == "author-homepage") command= "tmacmhomepage";
  else if (key == "author-note") command= "authornote";
  else return scheme_tree (TUPLE);
  scheme_tree out= stree_apply (command); out << converted; return out;
}

scheme_tree
acm_affiliation (scheme_tree source) {
  scheme_tree lines (TUPLE);
  if (stree_list (source) && N(source) > 1) {
    scheme_tree body= source[1];
    if (head_is (body, "concat")) {
      for (int i=1; i<N(body); ++i)
        if (!head_is (body[i], "next-line")) lines << body[i];
    }
    else lines << body;
  }
  static const char* commands[]= {"institution", "streetaddress", "city", "country"};
  scheme_tree paragraph= stree_apply ("!paragraph");
  int count= N(lines) < 4 ? N(lines) : 4;
  for (int i=0; i<count; ++i) {
    scheme_tree item= stree_apply (commands[i]); item << tmtex_convert (lines[i]);
    paragraph << item;
  }
  scheme_tree out= stree_apply ("affiliation"); out << paragraph; return out;
}

scheme_tree
acm_append_authors (scheme_tree authors) {
  scheme_tree flattened= stree_apply ("!document");
  if (stree_list (authors))
    for (int i=0; i<N(authors); ++i) {
      scheme_tree author= authors[i];
      if (!nonempty_value (author)) continue;
      if (func_is (author, "author", 1) && head_is (author[1], "!paragraph") &&
          N(author[1]) > 1) {
        scheme_tree first= stree_apply ("author"); first << author[1][1];
        flattened << first;
        for (int j=2; j<N(author[1]); ++j) flattened << author[1][j];
      }
      else flattened << author;
    }
  scheme_tree result (TUPLE); result << flattened; return result;
}

scheme_tree
acm_make_doc_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 12) return stree_string ("");
  scheme_tree title_values= second_values (args[0]);
  scheme_tree titles= latex_tmtex_concat_Sep (title_values);
  scheme_tree title_content (TUPLE); append_items (title_content, titles);
  append_items (title_content, args[4]);

  scheme_tree out= stree_apply ("!document");
  if (N(title_content) > 0) {
    scheme_tree paragraph= stree_apply ("!paragraph"); append_items (paragraph, title_content);
    scheme_tree indent= stree_apply ("!indent"); indent << paragraph;
    scheme_tree title= stree_apply ("title"); title << indent; out << title;
  }
  append_items (out, args[1]);
  append_items (out, args[5]);
  append_items (out, acm_append_authors (args[2]));
  append_items (out, args[3]);
  out << stree_apply ("maketitle");
  return out;
}

scheme_tree
acm_abstract_keywords (scheme_tree value) {
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  scheme_tree joined= latex_tmtex_concat_sep (converted);
  scheme_tree out= stree_apply ("keywords");
  for (int i=0; i<N(joined); ++i) out << tmtex_convert (joined[i]);
  return out;
}

scheme_tree
acm_abstract_category (scheme_tree value) {
  scheme_tree raw (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) raw << value[i];
  while (N(raw) < 3) raw << stree_string ("");
  scheme_tree out= stree_apply ("category");
  int first_count= N(raw) < 3 ? N(raw) : 3;
  for (int i=0; i<first_count; ++i) out << tmtex_convert (raw[i]);
  if (N(raw) > 3) {
    scheme_tree option= stree_apply ("!option"); option << tmtex_convert (raw[3]);
    out << option;
    for (int i=4; i<N(raw); ++i) out << tmtex_convert (raw[i]);
  }
  return out;
}

scheme_tree
acm_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree result (TUPLE);
  append_items (result, args[5]); append_items (result, args[1]);
  append_items (result, args[2]); append_items (result, args[3]);
  append_items (result, args[4]); append_items (result, args[0]);
  if (N(result) == 0) return stree_string ("");
  scheme_tree out= stree_apply ("!document"); append_items (out, result); return out;
}

scheme_tree
acm_passthrough_command (string key, scheme_tree args) {
  scheme_tree out= stree_apply (key);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << tmtex_convert (args[i]);
  return out;
}

bool
abstract_environment (scheme_tree value) {
  return stree_list (value) && N(value) > 0 && stree_list (value[0]) &&
         head_is (value[0], "!begin") && N(value[0]) > 1 &&
         string_atom (value[0][1]) && atom_text (value[0][1]) == "abstract";
}

scheme_tree
acm_remove_maketitle (scheme_tree value, bool& removed) {
  if (!stree_list (value)) return value;
  if (head_is (value, "!document") && N(value) > 1 &&
      head_is (value[N(value)-1], "maketitle")) {
    scheme_tree out (TUPLE);
    for (int i=0; i<N(value)-1; ++i) out << value[i];
    removed= true;
    return out;
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << acm_remove_maketitle (value[i], removed);
  return out;
}

scheme_tree
acm_add_maketitle (scheme_tree value, bool& added) {
  if (!stree_list (value)) return value;
  if (head_is (value, "!document")) {
    scheme_tree out (TUPLE); out << value[0];
    for (int i=1; i<N(value); ++i) {
      if (abstract_environment (value[i])) {
        out << value[i] << stree_apply ("maketitle");
        added= true;
      }
      else out << acm_add_maketitle (value[i], added);
    }
    return out;
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << acm_add_maketitle (value[i], added);
  return out;
}

scheme_tree
acm_postprocess (scheme_tree value) {
  bool removed= false, added= false;
  scheme_tree without= acm_remove_maketitle (value, removed);
  scheme_tree moved= acm_add_maketitle (without, added);
  return removed && added ? moved : value;
}

scheme_tree
ieee_join_authors (scheme_tree authors, scheme_tree separator) {
  scheme_tree values (TUPLE);
  if (stree_list (authors))
    for (int i=0; i<N(authors); ++i)
      if (nonempty_value (authors[i]) && stree_list (authors[i]) &&
          N(authors[i]) > 1)
        values << authors[i][1];
  scheme_tree concat= stree_apply ("!concat");
  for (int i=0; i<N(values); ++i) {
    if (i) concat << separator;
    concat << values[i];
  }
  scheme_tree result (TUPLE);
  if (N(values) == 0) return result;
  scheme_tree indent= stree_apply ("!indent"); indent << concat;
  scheme_tree author= stree_apply ("author"); author << indent;
  result << author;
  return result;
}

scheme_tree
ieee_conf_append_authors (scheme_tree authors) {
  scheme_tree separator= stree_apply ("!concat");
  separator << stree_apply ("!linefeed") << stree_apply ("and")
            << stree_apply ("!linefeed");
  return ieee_join_authors (authors, separator);
}

scheme_tree
ieee_tran_append_authors (scheme_tree authors) {
  if (!publisher_state.ieee_clustered) {
    scheme_tree separator= stree_apply ("!concat");
    separator << stree_apply ("!linefeed");
    if (publisher_state.ieee_conference)
      separator << stree_apply ("and") << stree_apply ("!linefeed");
    else separator << stree_string ("and~");
    return ieee_join_authors (authors, separator);
  }

  scheme_tree name_bodies (TUPLE), blocks (TUPLE);
  if (stree_list (authors))
    for (int i=0; i<N(authors); ++i) {
      scheme_tree author= authors[i];
      if (!stree_list (author)) continue;
      for (int j=0; j<N(author); ++j) {
        if (head_is (author[j], "IEEEauthorblockN") && N(author[j]) > 1)
          name_bodies << author[j][1];
        else if (stree_list (author[j]) && N(author[j]) > 0)
          blocks << author[j];
      }
    }

  scheme_tree result_items (TUPLE);
  scheme_tree joined_names= latex_tmtex_concat_sep (name_bodies);
  if (stree_list (joined_names) && N(joined_names) > 0) {
    scheme_tree names= stree_apply ("IEEEauthorblockN");
    append_items (names, joined_names);
    result_items << names;
  }
  append_items (result_items, blocks);
  if (N(result_items) == 0) return scheme_tree (TUPLE);

  scheme_tree concat= stree_apply ("!concat");
  for (int i=0; i<N(result_items); ++i) {
    if (i) {
      scheme_tree sep= stree_apply ("!concat"); sep << stree_apply ("!linefeed");
      concat << sep;
    }
    concat << result_items[i];
  }
  scheme_tree indent= stree_apply ("!indent"); indent << concat;
  scheme_tree author= stree_apply ("author"); author << indent;
  scheme_tree out (TUPLE); out << author; return out;
}

scheme_tree
ieee_conf_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return scheme_tree (TUPLE);
  scheme_tree names= latex_tmtex_concat_Sep (second_values (args[0]));
  scheme_tree main (TUPLE); append_items (main, names);
  append_items (main, args[3]); append_items (main, args[5]); append_items (main, args[4]);

  scheme_tree content (TUPLE);
  if (N(main) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, main);
    content << concat;
  }
  append_items (content, args[1]); append_items (content, args[2]);
  if (N(content) == 0) return scheme_tree (TUPLE);
  scheme_tree paragraph= stree_apply ("!paragraph"); append_items (paragraph, content);
  scheme_tree author= stree_apply ("author"); author << paragraph; return author;
}

scheme_tree
ieee_tran_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 11 || !publisher_state.ieee_conference)
    return scheme_tree (TUPLE);
  scheme_tree names= latex_tmtex_concat_Sep (second_values (args[0]));
  scheme_tree affiliations= publisher_state.ieee_clustered ? args[1]
                                                           : second_values (args[1]);

  scheme_tree name_content (TUPLE); append_items (name_content, names);
  append_items (name_content, args[6]); append_items (name_content, args[7]);
  append_items (name_content, args[3]); append_items (name_content, args[5]);
  append_items (name_content, args[4]);

  scheme_tree author_blocks (TUPLE);
  if (N(name_content) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, name_content);
    scheme_tree block= stree_apply ("IEEEauthorblockN"); block << concat;
    author_blocks << block;
  }

  scheme_tree address_content (TUPLE); append_items (address_content, affiliations);
  append_items (address_content, args[2]);
  scheme_tree address_blocks (TUPLE);
  if (publisher_state.ieee_clustered) {
    for (int i=0; i<N(address_content); ++i) {
      scheme_tree block= stree_apply ("IEEEauthorblockA"); block << address_content[i];
      address_blocks << block;
    }
  }
  else if (N(address_content) > 0) {
    scheme_tree concat= stree_apply ("!concat");
    for (int i=0; i<N(address_content); ++i) {
      if (i) concat << stree_apply ("!nextline");
      concat << address_content[i];
    }
    scheme_tree block= stree_apply ("IEEEauthorblockA"); block << concat;
    address_blocks << block;
  }

  if (N(author_blocks) == 0 && N(address_blocks) == 0) return scheme_tree (TUPLE);
  if (publisher_state.ieee_clustered) {
    scheme_tree out (TUPLE); append_items (out, author_blocks); append_items (out, address_blocks);
    return out;
  }
  scheme_tree paragraph= stree_apply ("!paragraph");
  append_items (paragraph, author_blocks); append_items (paragraph, address_blocks);
  scheme_tree author= stree_apply ("author"); author << paragraph; return author;
}

scheme_tree
ieee_prepare_doc_data (scheme_tree values) {
  if (!publisher_state.ieee_clustered) return values;
  scheme_tree out (TUPLE);
  if (stree_list (values))
    for (int i=0; i<N(values); ++i)
      out << latex_export_metadata_replace_documents (values[i]);
  out= latex_export_metadata_make_references (out, "author-affiliation", true, true);
  out= latex_export_metadata_make_references (out, "author-email", true, true);
  return out;
}

scheme_tree
ieee_reference_mark (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return scheme_tree (TUPLE);
  scheme_tree out= stree_apply ("IEEEauthorrefmark"); out << args[0]; return out;
}

scheme_tree
ieee_reference_label (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) < 2) return scheme_tree (TUPLE);
  scheme_tree out= stree_apply ("!concat"); out << ieee_reference_mark (args);
  if (key == "author-email-label") {
    scheme_tree email= stree_apply ("tmieeeemail"); email << tmtex_convert (args[1]); out << email;
  }
  else out << tmtex_convert (args[1]);
  return out;
}

scheme_tree
ieee_author_field (string key, scheme_tree source) {
  if (!stree_list (source) || N(source) < 2) return scheme_tree (TUPLE);
  scheme_tree clean= (key == "author-email" || key == "author-homepage")
    ? latex_export_metadata_remove_line_feeds (source) : source;
  scheme_tree body= key == "author-homepage"
    ? latex_export_metadata_inline (clean[1]) : tmtex_convert (clean[1]);
  if (ieee_conf_style ()) {
    if (key == "author-affiliation") {
      scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("affiliation");
      scheme_tree out (TUPLE); out << begin << body; return out;
    }
    if (key == "author-email") { scheme_tree out= stree_apply ("email"); out << body; return out; }
    if (key == "author-homepage") { scheme_tree out= stree_apply ("tmfnhomepage"); out << body; return out; }
  }
  if (ieee_tran_style () && publisher_state.ieee_conference) {
    if (key == "author-affiliation") {
      scheme_tree out= stree_apply ("IEEEauthorblockA"); out << body; return out;
    }
    if (key == "author-email") {
      scheme_tree out= stree_apply ("tmieeeemail"); out << body; return out;
    }
  }
  return scheme_tree (TUPLE);
}

scheme_tree
ieee_keywords (scheme_tree value) {
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  scheme_tree body= stree_apply ("!concat");
  for (int i=0; i<N(converted); ++i) {
    if (i) {
      scheme_tree sep= stree_apply ("!concat");
      sep << stree_apply ("tmsep") << stree_string (" ");
      body << sep;
    }
    body << converted[i];
  }
  scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("IEEEkeywords");
  scheme_tree out (TUPLE); out << begin << body; return out;
}

scheme_tree
ieee_replace_symbols (scheme_tree value) {
  if (!stree_list (value)) return value;
  if (func_is (value, "hbar", 0)) return stree_apply ("ieeehbar");
  if (func_is (value, "jmath", 0)) return stree_apply ("ieeejmath");
  if (func_is (value, "amalg", 0)) return stree_apply ("ieeeamalg");
  if (func_is (value, "coprod", 0)) return stree_apply ("ieeecoprod");
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << ieee_replace_symbols (value[i]);
  return out;
}

scheme_tree
beamer_make_slides (scheme_tree value) {
  if (!stree_list (value) || N(value) == 0) return value;
  string head= atom_text (value[0]);
  scheme_tree out (TUPLE);
  out << ((head == "hidden" || head == "shown") ? tree ("slide") : value[0]);
  for (int i=1; i<N(value); ++i) out << beamer_make_slides (value[i]);
  return out;
}

scheme_tree
beamer_metadata_field (string key, scheme_tree source) {
  if (!stree_list (source) || N(source) < 2) return scheme_tree (TUPLE);
  scheme_tree body= tmtex_convert (source[1]);
  string command;
  if      (key == "doc-running-title") command= "titlerunning";
  else if (key == "doc-subtitle") command= "subtitle";
  else if (key == "doc-note" || key == "author-note") command= "tmnote";
  else if (key == "doc-misc" || key == "author-misc") command= "tmmisc";
  else if (key == "doc-date") command= "date";
  else if (key == "doc-running-author") command= "authorrunning";
  else if (key == "author-affiliation") command= "institute";
  else if (key == "author-email") command= "email";
  else if (key == "author-homepage") command= "tmfnhomepage";
  else return scheme_tree (TUPLE);
  scheme_tree out= stree_apply (command); out << body; return out;
}

scheme_tree
beamer_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return scheme_tree (TUPLE);
  scheme_tree names= latex_tmtex_concat_Sep (second_values (args[0]));
  scheme_tree content (TUPLE); append_items (content, names);
  append_items (content, args[3]); append_items (content, args[5]);
  append_items (content, args[4]);
  if (N(content) == 0) return scheme_tree (TUPLE);
  scheme_tree paragraph= stree_apply ("!paragraph"); append_items (paragraph, content);
  scheme_tree author= stree_apply ("author"); author << paragraph; return author;
}

scheme_tree
beamer_append (string command, scheme_tree values) {
  scheme_tree bodies (TUPLE);
  if (stree_list (values))
    for (int i=0; i<N(values); ++i)
      if (nonempty_value (values[i]) && stree_list (values[i]) && N(values[i]) > 1)
        bodies << values[i][1];
  scheme_tree out (TUPLE);
  if (N(bodies) == 0) return out;
  scheme_tree concat= stree_apply ("!concat");
  for (int i=0; i<N(bodies); ++i) {
    if (i) {
      scheme_tree sep= stree_apply ("!concat");
      sep << stree_apply ("!linefeed") << stree_apply ("and")
          << stree_apply ("!linefeed");
      concat << sep;
    }
    concat << bodies[i];
  }
  scheme_tree indent= stree_apply ("!indent"); indent << concat;
  scheme_tree wrapper= stree_apply (command); wrapper << indent; out << wrapper;
  return out;
}

bool
beamer_author_data (scheme_tree author, scheme_tree& data) {
  if (!head_is (author, "doc-author") || N(author) < 2 ||
      !head_is (author[1], "author-data")) return false;
  data= author[1]; return true;
}

bool
beamer_find_affiliation (scheme_tree value, scheme_tree& affiliation) {
  if (!stree_list (value)) return false;
  if (head_is (value, "author-affiliation")) { affiliation= value; return true; }
  for (int i=0; i<N(value); ++i)
    if (beamer_find_affiliation (value[i], affiliation)) return true;
  return false;
}

scheme_tree
beamer_clear_affiliation (scheme_tree affiliation, scheme_tree author,
                          bool remove_single) {
  scheme_tree data;
  if (!beamer_author_data (author, data)) return scheme_tree (TUPLE);
  int aff_count=0;
  for (int i=1; i<N(data); ++i)
    if (head_is (data[i], "author-affiliation")) ++aff_count;
  if (remove_single && aff_count == 1) {
    for (int i=1; i<N(data); ++i)
      if (data[i] == affiliation) return scheme_tree (TUPLE);
  }
  scheme_tree next_data= stree_apply ("author-data");
  for (int i=1; i<N(data); ++i)
    if (!(data[i] == affiliation)) next_data << data[i];
  scheme_tree out= stree_apply ("doc-author"); out << next_data; return out;
}

bool
beamer_author_has_affiliation (scheme_tree author, scheme_tree affiliation) {
  scheme_tree data;
  if (!beamer_author_data (author, data)) return false;
  for (int i=1; i<N(data); ++i)
    if (data[i] == affiliation) return true;
  return false;
}

scheme_tree
beamer_cluster_affiliations (scheme_tree authors) {
  scheme_tree out (TUPLE);
  if (!stree_list (authors)) return out;
  if (N(authors) == 0) return out;
  scheme_tree affiliation;
  bool has_affiliation= beamer_find_affiliation (authors, affiliation);

  scheme_tree group_authors (TUPLE), remaining (TUPLE);
  for (int i=0; i<N(authors); ++i) {
    bool in_group= !has_affiliation ||
                   beamer_author_has_affiliation (authors[i], affiliation);
    if (in_group)
      group_authors << beamer_clear_affiliation (affiliation, authors[i], false);
    scheme_tree next= beamer_clear_affiliation (affiliation, authors[i], true);
    if (nonempty_value (next) && (!stree_list (next) || N(next) > 0))
      remaining << next;
  }

  scheme_tree group= stree_apply ("affiliation-group");
  group << (has_affiliation && N(affiliation) > 1 ? affiliation[1]
                                                    : scheme_tree (TUPLE));
  append_items (group, group_authors);
  out << group;
  if (has_affiliation) append_items (out, beamer_cluster_affiliations (remaining));
  return out;
}

scheme_tree
beamer_group_author_name (scheme_tree author) {
  scheme_tree data;
  if (!beamer_author_data (author, data)) return scheme_tree (TUPLE);
  scheme_tree names (TUPLE);
  bool has_email=false;
  for (int i=1; i<N(data); ++i) {
    if (head_is (data[i], "author-name") && N(data[i]) > 1)
      names << tmtex_convert (data[i][1]);
    if (head_is (data[i], "author-email")) has_email=true;
  }
  if (N(names) == 0 && !has_email) return scheme_tree (TUPLE);
  scheme_tree joined= latex_tmtex_concat_Sep (names);
  scheme_tree concat= stree_apply ("!concat"); append_items (concat, joined); return concat;
}

scheme_tree
beamer_affiliation_group (scheme_tree group) {
  if (!head_is (group, "affiliation-group") || N(group) < 2)
    return scheme_tree (TUPLE);
  scheme_tree content= stree_apply ("!concat");
  bool first_author=true;
  for (int i=2; i<N(group); ++i) {
    scheme_tree author= beamer_group_author_name (group[i]);
    if (!nonempty_value (author) || (stree_list (author) && N(author) == 0)) continue;
    if (!first_author) {
      scheme_tree sep= stree_apply ("!concat");
      sep << stree_string (" ") << stree_apply ("and") << stree_string (" ");
      content << sep;
    }
    content << author;
    first_author=false;
  }
  if (nonempty_value (group[1]) && (!stree_list (group[1]) || N(group[1]) > 0)) {
    scheme_tree aff= stree_apply ("!concat");
    aff << stree_apply ("!linefeed") << stree_apply ("at")
        << stree_apply ("!linefeed") << tmtex_convert (group[1]);
    content << aff;
  }
  if (N(content) == 1) return scheme_tree (TUPLE);
  scheme_tree out= stree_apply ("institute"); out << content; return out;
}

scheme_tree
map_converted_selected (string tag, scheme_tree values) {
  scheme_tree selected= latex_export_metadata_select (tag, values), out (TUPLE);
  for (int i=0; i<N(selected); ++i) out << tmtex_convert (selected[i]);
  return out;
}

scheme_tree
beamer_doc_data (scheme_tree args) {
  scheme_tree values (TUPLE);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i)
      values << latex_export_metadata_replace_documents (args[i]);

  scheme_tree subtitles= map_converted_selected ("doc-subtitle", values);
  scheme_tree notes= map_converted_selected ("doc-note", values);
  scheme_tree miscs= map_converted_selected ("doc-misc", values);
  scheme_tree dates= map_converted_selected ("doc-date", values);
  scheme_tree authors= map_converted_selected ("doc-author", values);
  scheme_tree running_author= map_converted_selected ("doc-running-author", values);
  scheme_tree titles= map_converted_selected ("doc-title", values);
  scheme_tree running_title= map_converted_selected ("doc-running-title", values);
  scheme_tree raw_authors= latex_export_metadata_select ("doc-author", values);
  scheme_tree groups= beamer_cluster_affiliations (raw_authors), affiliations (TUPLE);
  for (int i=0; i<N(groups); ++i) {
    scheme_tree converted= beamer_affiliation_group (groups[i]);
    if (nonempty_value (converted) && (!stree_list (converted) || N(converted) > 0))
      affiliations << converted;
  }

  scheme_tree title_content= latex_tmtex_concat_Sep (second_values (titles));
  append_items (title_content, notes); append_items (title_content, miscs);

  scheme_tree out= stree_apply ("!document");
  if (N(title_content) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, title_content);
    scheme_tree title= stree_apply ("title"); title << concat; out << title;
  }
  append_items (out, subtitles); append_items (out, running_title);
  append_items (out, running_author);
  append_items (out, beamer_append ("author", authors));
  append_items (out, beamer_append ("institute", affiliations));
  append_items (out, dates); out << stree_apply ("maketitle");
  return out;
}

scheme_tree
beamer_abstract_field (string key, scheme_tree value) {
  string command;
  if      (key == "tmtex-abstract-keywords") command= "keywords";
  else if (key == "tmtex-abstract-msc") command= "subclass";
  else if (key == "tmtex-abstract-acm") command= "CRclass";
  else if (key == "tmtex-abstract-pacs") command= "PACS";
  else return scheme_tree (TUPLE);
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  scheme_tree concat= stree_apply ("!concat");
  for (int i=0; i<N(converted); ++i) {
    if (i) { scheme_tree g= stree_apply ("!group"); g << stree_apply ("and"); concat << g; }
    concat << converted[i];
  }
  scheme_tree out= stree_apply (command); out << concat; return out;
}

scheme_tree
beamer_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree result (TUPLE); append_items (result, args[5]);
  append_items (result, args[2]); append_items (result, args[1]);
  append_items (result, args[3]); append_items (result, args[4]);
  append_items (result, args[0]);
  if (N(result) == 0) return stree_string ("");
  scheme_tree out= stree_apply ("!document"); append_items (out, result); return out;
}

scheme_tree
beamer_slide (scheme_tree args) {
  scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("frame");
  scheme_tree out (TUPLE); out << begin;
  out << (stree_list (args) && N(args) > 0 ? tmtex_convert (args[0])
                                            : stree_string (""));
  return out;
}

scheme_tree
beamer_title (scheme_tree args) {
  scheme_tree out= stree_apply ("frametitle");
  out << (stree_list (args) && N(args) > 0 ? tmtex_convert (args[0])
                                            : stree_string (""));
  return out;
}

scheme_tree
springer_metadata_field (string key, scheme_tree source) {
  if (!stree_list (source) || N(source) < 2) return scheme_tree (TUPLE);
  string command;
  if      (key == "doc-running-title") command= "titlerunning";
  else if (key == "doc-subtitle") command= "subtitle";
  else if (key == "doc-note" || key == "author-note") command= "tmnote";
  else if (key == "doc-misc" || key == "author-misc") command= "tmmisc";
  else if (key == "doc-date") command= "date";
  else if (key == "doc-running-author") command= "authorrunning";
  else if (key == "author-affiliation") command= "institute";
  else if (key == "author-email") command= "email";
  else if (key == "author-homepage") command= "tmfnhomepage";
  else return scheme_tree (TUPLE);
  scheme_tree out= stree_apply (command); out << tmtex_convert (source[1]); return out;
}

scheme_tree
springer_group_author (scheme_tree author) {
  scheme_tree data;
  if (!beamer_author_data (author, data)) return scheme_tree (TUPLE);
  scheme_tree names (TUPLE), emails (TUPLE);
  for (int i=1; i<N(data); ++i) {
    if (head_is (data[i], "author-name") && N(data[i]) > 1)
      names << tmtex_convert (data[i][1]);
    else if (head_is (data[i], "author-email"))
      emails << tmtex_convert (data[i]);
  }
  scheme_tree joined= latex_tmtex_concat_Sep (names);
  if (N(joined) == 0 && N(emails) == 0) return scheme_tree (TUPLE);
  scheme_tree out= stree_apply ("!concat"); append_items (out, joined);
  if (N(joined) > 0 && N(emails) > 0) out << stree_string (" ");
  append_items (out, emails);
  return out;
}

scheme_tree
springer_affiliation_group (scheme_tree group) {
  if (!head_is (group, "affiliation-group") || N(group) < 2)
    return scheme_tree (TUPLE);
  scheme_tree content= stree_apply ("!concat");
  bool first_author=true;
  for (int i=2; i<N(group); ++i) {
    scheme_tree author= springer_group_author (group[i]);
    if (!nonempty_value (author) || (stree_list (author) && N(author) == 0)) continue;
    if (!first_author) {
      scheme_tree sep= stree_apply ("!concat");
      sep << stree_string (" ") << stree_apply ("and") << stree_string (" ");
      content << sep;
    }
    content << author;
    first_author=false;
  }
  if (nonempty_value (group[1]) && (!stree_list (group[1]) || N(group[1]) > 0)) {
    scheme_tree aff= stree_apply ("!concat");
    aff << stree_apply ("!linefeed") << stree_apply ("at")
        << stree_apply ("!linefeed") << tmtex_convert (group[1]);
    content << aff;
  }
  if (N(content) == 1) return scheme_tree (TUPLE);
  scheme_tree out= stree_apply ("institute"); out << content; return out;
}

scheme_tree
springer_doc_data (scheme_tree args) {
  scheme_tree values (TUPLE);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i)
      values << latex_export_metadata_replace_documents (args[i]);
  scheme_tree subtitles= map_converted_selected ("doc-subtitle", values);
  scheme_tree notes= map_converted_selected ("doc-note", values);
  scheme_tree miscs= map_converted_selected ("doc-misc", values);
  scheme_tree dates= map_converted_selected ("doc-date", values);
  scheme_tree authors= map_converted_selected ("doc-author", values);
  scheme_tree running_author= map_converted_selected ("doc-running-author", values);
  scheme_tree titles= map_converted_selected ("doc-title", values);
  scheme_tree running_title= map_converted_selected ("doc-running-title", values);
  scheme_tree raw_authors= latex_export_metadata_select ("doc-author", values);
  scheme_tree groups= beamer_cluster_affiliations (raw_authors), affiliations (TUPLE);
  for (int i=0; i<N(groups); ++i) {
    scheme_tree converted= springer_affiliation_group (groups[i]);
    if (nonempty_value (converted) && (!stree_list (converted) || N(converted) > 0))
      affiliations << converted;
  }

  scheme_tree title_content= latex_tmtex_concat_Sep (second_values (titles));
  append_items (title_content, notes); append_items (title_content, miscs);
  scheme_tree out= stree_apply ("!document");
  if (N(title_content) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, title_content);
    scheme_tree title= stree_apply ("title"); title << concat; out << title;
  }
  append_items (out, subtitles); append_items (out, running_title);
  append_items (out, running_author);
  append_items (out, beamer_append ("author", authors));
  append_items (out, beamer_append ("institute", affiliations));
  append_items (out, dates); out << stree_apply ("maketitle");
  return out;
}

scheme_tree
svmono_make_doc_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 12) return stree_string ("");
  scheme_tree title_content= latex_tmtex_concat_Sep (second_values (args[0]));
  append_items (title_content, args[5]); append_items (title_content, args[4]);
  scheme_tree out= stree_apply ("!document");
  if (N(title_content) > 0) {
    scheme_tree paragraph= stree_apply ("!paragraph"); append_items (paragraph, title_content);
    scheme_tree indent= stree_apply ("!indent"); indent << paragraph;
    scheme_tree title= stree_apply ("title"); title << indent; out << title;
  }
  append_items (out, args[1]);
  scheme_tree author_args (TUPLE); author_args << args[2];
  append_items (out, latex_export_metadata_default ("append-authors", author_args));
  append_items (out, args[3]); out << stree_apply ("maketitle");
  return out;
}

scheme_tree
llncs_collect_affiliations (scheme_tree authors) {
  scheme_tree out (TUPLE), remaining= authors;
  for (;;) {
    scheme_tree affiliation;
    if (!beamer_find_affiliation (remaining, affiliation)) break;
    scheme_tree group= stree_apply ("affiliation-group");
    group << (N(affiliation) > 1 ? affiliation[1] : scheme_tree (TUPLE));
    out << group;
    scheme_tree next (TUPLE);
    if (stree_list (remaining))
      for (int i=0; i<N(remaining); ++i) {
        scheme_tree cleaned= beamer_clear_affiliation (affiliation, remaining[i], true);
        if (nonempty_value (cleaned) && (!stree_list (cleaned) || N(cleaned) > 0))
          next << cleaned;
      }
    remaining= next;
  }
  return out;
}

scheme_tree
llncs_replace_affiliation (scheme_tree author, scheme_tree affiliation, int number) {
  scheme_tree data;
  if (!beamer_author_data (author, data)) return scheme_tree (TUPLE);
  scheme_tree next_data= stree_apply ("author-data");
  for (int i=1; i<N(data); ++i) {
    if (data[i] == affiliation) {
      scheme_tree ref= stree_apply ("author-affiliation-ref");
      ref << stree_string (as_string (number)); next_data << ref;
    }
    else next_data << data[i];
  }
  scheme_tree out= stree_apply ("doc-author"); out << next_data; return out;
}

scheme_tree
llncs_replace_affiliations (scheme_tree authors) {
  scheme_tree current= authors;
  int number=0;
  for (;;) {
    scheme_tree affiliation;
    if (!beamer_find_affiliation (current, affiliation)) break;
    ++number;
    scheme_tree next (TUPLE);
    if (stree_list (current))
      for (int i=0; i<N(current); ++i)
        next << llncs_replace_affiliation (current[i], affiliation, number);
    current= next;
  }
  return current;
}

scheme_tree
llncs_affiliation_ref (scheme_tree args) {
  scheme_tree out= stree_apply ("inst");
  if (stree_list (args) && N(args) > 0) out << tmtex_convert (args[0]);
  return out;
}

scheme_tree
llncs_make_author (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return scheme_tree (TUPLE);
  scheme_tree names= latex_tmtex_concat_Sep (second_values (args[0]));
  scheme_tree main (TUPLE); append_items (main, names); append_items (main, args[1]);
  scheme_tree content (TUPLE);
  if (N(main) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, main); content << concat;
  }
  append_items (content, args[3]); append_items (content, args[5]); append_items (content, args[4]);
  if (N(content) == 0) return scheme_tree (TUPLE);
  scheme_tree paragraph= stree_apply ("!paragraph"); append_items (paragraph, content);
  scheme_tree author= stree_apply ("author"); author << paragraph; return author;
}

scheme_tree
llncs_doc_author (scheme_tree source) {
  scheme_tree cleaned= latex_export_metadata_replace_documents (source);
  scheme_tree data;
  if (!beamer_author_data (cleaned, data)) return scheme_tree (TUPLE);
  scheme_tree values (TUPLE); for (int i=1; i<N(data); ++i) values << data[i];
  scheme_tree names= map_converted_selected ("author-name", values);
  scheme_tree miscs= map_converted_selected ("author-misc", values);
  scheme_tree notes= map_converted_selected ("author-note", values);
  scheme_tree emails= map_converted_selected ("author-email", values);
  scheme_tree urls= map_converted_selected ("author-homepage", values);
  scheme_tree refs_raw= latex_export_metadata_select ("author-affiliation-ref", values);
  scheme_tree refs (TUPLE);
  for (int i=0; i<N(refs_raw); ++i) {
    scheme_tree args (TUPLE);
    for (int j=1; j<N(refs_raw[i]); ++j) args << refs_raw[i][j];
    refs << llncs_affiliation_ref (args);
  }
  scheme_tree packed (TUPLE); packed << names << refs << emails << urls << miscs << notes
                                    << scheme_tree (TUPLE) << scheme_tree (TUPLE)
                                    << scheme_tree (TUPLE) << scheme_tree (TUPLE)
                                    << scheme_tree (TUPLE);
  return llncs_make_author (packed);
}

scheme_tree
llncs_doc_data (scheme_tree args) {
  scheme_tree values (TUPLE);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i)
      values << latex_export_metadata_replace_documents (args[i]);
  scheme_tree subtitles= map_converted_selected ("doc-subtitle", values);
  scheme_tree notes= map_converted_selected ("doc-note", values);
  scheme_tree miscs= map_converted_selected ("doc-misc", values);
  scheme_tree dates= map_converted_selected ("doc-date", values);
  scheme_tree running_author= map_converted_selected ("doc-running-author", values);
  scheme_tree titles= map_converted_selected ("doc-title", values);
  scheme_tree running_title= map_converted_selected ("doc-running-title", values);
  scheme_tree raw_authors= latex_export_metadata_select ("doc-author", values);

  scheme_tree affiliation_groups= llncs_collect_affiliations (raw_authors), affiliations (TUPLE);
  for (int i=0; i<N(affiliation_groups); ++i) {
    if (N(affiliation_groups[i]) > 1) {
      scheme_tree source= stree_apply ("author-affiliation"); source << affiliation_groups[i][1];
      affiliations << springer_metadata_field ("author-affiliation", source);
    }
  }
  scheme_tree replaced= llncs_replace_affiliations (raw_authors), authors (TUPLE);
  for (int i=0; i<N(replaced); ++i) authors << llncs_doc_author (replaced[i]);

  scheme_tree title_content= latex_tmtex_concat_Sep (second_values (titles));
  append_items (title_content, notes); append_items (title_content, miscs);
  scheme_tree out= stree_apply ("!document");
  if (N(title_content) > 0) {
    scheme_tree concat= stree_apply ("!concat"); append_items (concat, title_content);
    scheme_tree title= stree_apply ("title"); title << concat; out << title;
  }
  append_items (out, subtitles); append_items (out, running_title); append_items (out, running_author);
  append_items (out, beamer_append ("author", authors));
  append_items (out, beamer_append ("institute", affiliations));
  append_items (out, dates); out << stree_apply ("maketitle");
  return out;
}

scheme_tree
llncs_keywords (scheme_tree value) {
  scheme_tree converted (TUPLE);
  if (stree_list (value))
    for (int i=1; i<N(value); ++i) converted << tmtex_convert (value[i]);
  scheme_tree concat= stree_apply ("!concat");
  for (int i=0; i<N(converted); ++i) {
    if (i) { scheme_tree g= stree_apply ("!group"); g << stree_apply ("tmsep"); concat << g; }
    concat << converted[i];
  }
  scheme_tree out= stree_apply ("keywords"); out << concat; return out;
}

scheme_tree
llncs_make_abstract_data (scheme_tree args) {
  if (!stree_list (args) || N(args) < 6) return stree_string ("");
  scheme_tree classes (TUPLE); append_items (classes, args[0]); append_items (classes, args[1]);
  append_items (classes, args[2]); append_items (classes, args[3]); append_items (classes, args[4]);
  scheme_tree abstract= args[5];
  if (N(classes) > 0) {
    scheme_tree body= stree_apply ("!document");
    if (stree_list (abstract))
      for (int i=0; i<N(abstract); ++i)
        if (stree_list (abstract[i]) && N(abstract[i]) > 1) body << abstract[i][1];
    append_items (body, classes);
    scheme_tree begin= stree_apply ("!begin"); begin << stree_string ("abstract");
    scheme_tree env (TUPLE); env << begin << body;
    abstract= scheme_tree (TUPLE); abstract << env;
  }
  scheme_tree out= stree_apply ("!document"); append_items (out, abstract); return out;
}

} // namespace

namespace latex_export_internal {

string
publisher_source_style () {
  return publisher_state.source_style;
}

void
publisher_initialize (string source_style, scheme_tree body) {
  publisher_state= PublisherState ();
  publisher_state.source_style= source_style;
  if (revtex_style ()) {
    publisher_state.revtex_showkeys= contains_head (body, "abstract-keywords");
    publisher_state.revtex_showpacs= contains_head (body, "abstract-msc");
    publisher_state.revtex_clustered=
      contains_exact (body, tag_value ("doc-title-options", "cluster-all")) ||
      contains_exact (body, tag_value ("doc-title-options",
                                       "cluster-by-affiliation"));
  }
  if (ieee_tran_style ()) {
    publisher_state.ieee_conference= contains_head (body, "author-email") ||
                                     contains_head (body, "author-affiliation");
    publisher_state.ieee_clustered= publisher_state.ieee_conference &&
      (contains_exact (body, tag_value ("doc-title-options", "cluster-all")) ||
       contains_exact (body, tag_value ("doc-title-options",
                                        "cluster-by-affiliation")));
  }
}

bool
publisher_transform_style (scheme_tree style, scheme_tree& result) {
  if (string_atom (style) && elsevier_style ()) {
    string name= atom_text (style);
    if (name == "elsarticle") result= stree_string ("elsarticle");
    else if (name == "ifac") result= stree_string ("ifacconf");
    else return false;
    return true;
  }
  if (string_atom (style) && atom_text (style) == "amsart") {
    result= style;
    return true;
  }
  if (string_atom (style) && revtex_style ()) {
    scheme_tree out (TUPLE);
    out << stree_string ("reprint")
        << stree_string (publisher_state.source_style);
    if (publisher_state.source_style == "aps") {
      if (publisher_state.revtex_showpacs) out << stree_string ("showpacs");
      if (publisher_state.revtex_showkeys) out << stree_string ("showkeys");
    }
    out << stree_string ("revtex4-1");
    result= out;
    return true;
  }
  return false;
}

bool
publisher_prepare (scheme_tree, scheme_tree document, scheme_tree& result) {
  if (springer_any_style ()) {
    result= document;
    return true;
  }
  if (beamer_style ()) {
    result= beamer_make_slides (document);
    return true;
  }
  if (acm_style ()) {
    result= document;
    return true;
  }
  if (ieee_style ()) {
    result= document;
    return true;
  }
  if (elsevier_style ()) {
    if (ifac_style ()) {
      array<object> packages; packages << object ("natbib");
      latex_export_set_latex_packages (as_list_object (packages));
      latex_export_recompute_dependencies ();
    }
    result= elsevier_create_frontmatter (document);
    return true;
  }
  if (!ams_style () && !revtex_style ()) return false;
  if (contains_head (document, "abstract-data"))
    result= insert_maketitle_after (document, "abstract-data");
  else if (contains_head (document, "doc-data"))
    result= insert_maketitle_after (document, "doc-data");
  else result= document;
  return true;
}

bool
publisher_postprocess (scheme_tree value, scheme_tree& result) {
  if (!acm_style ()) return false;
  result= acm_postprocess (value);
  return true;
}

bool
publisher_postprocess_body (scheme_tree value, scheme_tree& result) {
  if (!ieee_conf_style ()) return false;
  result= ieee_replace_symbols (value);
  return true;
}

bool
publisher_hook (string name, const array<scheme_tree>& args,
                scheme_tree& result) {
  if (springer_any_style ()) {
    if (name == "tmtex-make-author" && springer_style ()) {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= llncs_style () ? llncs_make_author (packed)
                             : beamer_make_author (packed);
      return true;
    }
    if (name == "tmtex-make-doc-data" && svmono_style ()) {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= svmono_make_doc_data (packed);
      return true;
    }
    if ((name == "tmtex-abstract-keywords" || name == "tmtex-abstract-msc" ||
         name == "tmtex-abstract-acm" || name == "tmtex-abstract-pacs") &&
        N(args) > 0 && springer_style ()) {
      if (llncs_style () && name == "tmtex-abstract-keywords")
        result= llncs_keywords (args[0]);
      else if (!llncs_style ()) result= beamer_abstract_field (name, args[0]);
      else {
        string key= name (6, N(name));
        scheme_tree wrapper (TUPLE); wrapper << args[0];
        result= latex_export_metadata_default (key, wrapper);
      }
      return true;
    }
    if (name == "tmtex-make-abstract-data" && springer_style ()) {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= llncs_style () ? llncs_make_abstract_data (packed)
                             : beamer_make_abstract_data (packed);
      return true;
    }
  }
  if (beamer_style ()) {
    if (name == "tmtex-make-author") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= beamer_make_author (packed);
      return true;
    }
    if ((name == "tmtex-abstract-keywords" || name == "tmtex-abstract-msc" ||
         name == "tmtex-abstract-acm" || name == "tmtex-abstract-pacs") &&
        N(args) > 0) {
      result= beamer_abstract_field (name, args[0]);
      return true;
    }
    if (name == "tmtex-make-abstract-data") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= beamer_make_abstract_data (packed);
      return true;
    }
  }
  if (ieee_style ()) {
    if (name == "tmtex-append-authors" && N(args) > 0) {
      result= ieee_conf_style () ? ieee_conf_append_authors (args[0])
                                 : ieee_tran_append_authors (args[0]);
      return true;
    }
    if (name == "tmtex-make-author") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      if (ieee_conf_style ()) {
        result= ieee_conf_make_author (packed);
        return true;
      }
      if (ieee_tran_style () && publisher_state.ieee_conference) {
        result= ieee_tran_make_author (packed);
        return true;
      }
    }
    if (name == "tmtex-prepare-doc-data" && N(args) > 0 &&
        ieee_tran_style () && publisher_state.ieee_clustered) {
      result= ieee_prepare_doc_data (args[0]);
      return true;
    }
    if (name == "tmtex-abstract-keywords" && N(args) > 0 && ieee_tran_style ()) {
      result= ieee_keywords (args[0]);
      return true;
    }
  }
  if (acm_style ()) {
    if (name == "tmtex-make-doc-data") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= acm_make_doc_data (packed);
      return true;
    }
    if (name == "tmtex-abstract-keywords" && N(args) > 0) {
      result= acm_abstract_keywords (args[0]);
      return true;
    }
    if (name == "tmtex-abstract-acm" && N(args) > 0) {
      result= acm_abstract_category (args[0]);
      return true;
    }
    if (name == "tmtex-make-abstract-data") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= acm_make_abstract_data (packed);
      return true;
    }
  }
  if (elsevier_style ()) {
    if (name == "tmtex-prepare-doc-data" && N(args) > 0) {
      result= elsevier_prepare_doc_data (args[0]);
      return true;
    }
    if (name == "tmtex-make-author") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= elsevier_make_author (packed);
      return true;
    }
    if (name == "tmtex-make-doc-data") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= elsevier_make_doc_data (packed);
      return true;
    }
    if ((name == "tmtex-abstract-keywords" ||
         name == "tmtex-abstract-msc" ||
         name == "tmtex-abstract-pacs") && N(args) > 0) {
      result= elsevier_abstract_field (name, args[0]);
      return true;
    }
    if (name == "tmtex-make-abstract-data") {
      scheme_tree packed (TUPLE);
      for (int i=0; i<N(args); ++i) packed << args[i];
      result= elsevier_make_abstract_data (packed);
      return true;
    }
  }
  if (revtex_style ()) {
    if ((name == "tmtex-doc-subtitle" || name == "tmtex-doc-note" ||
         name == "tmtex-doc-misc" || name == "tmtex-doc-date" ||
         name == "tmtex-author-affiliation" || name == "tmtex-author-email" ||
         name == "tmtex-author-homepage" || name == "tmtex-author-note" ||
         name == "tmtex-author-misc") && N(args) > 0) {
      result= revtex_metadata_field (name, args[0]); return true;
    }
    if (name == "tmtex-make-author") {
      scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
      result= revtex_make_author (packed); return true;
    }
    if (name == "tmtex-make-doc-data") {
      scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
      result= revtex_make_doc_data (packed); return true;
    }
    if ((name == "tmtex-abstract-keywords" || name == "tmtex-abstract-pacs") &&
        N(args) > 0) {
      result= revtex_abstract_field (name, args[0]); return true;
    }
    if (name == "tmtex-make-abstract-data") {
      scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
      result= revtex_make_abstract_data (packed); return true;
    }
  }
  if (!ams_style ()) return false;
  if ((name == "tmtex-doc-running-title" || name == "tmtex-doc-subtitle" ||
       name == "tmtex-doc-note" || name == "tmtex-doc-misc" ||
       name == "tmtex-doc-date" || name == "tmtex-author-affiliation" ||
       name == "tmtex-author-email" || name == "tmtex-author-homepage" ||
       name == "tmtex-author-note" || name == "tmtex-author-misc") &&
      N(args) > 0) {
    result= ams_metadata_field (name, args[0]);
    return true;
  }
  if (name == "tmtex-make-author") {
    scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
    result= ams_make_author (packed); return true;
  }
  if (name == "tmtex-make-doc-data") {
    scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
    result= ams_make_doc_data (packed); return true;
  }
  if ((name == "tmtex-abstract-keywords" || name == "tmtex-abstract-msc") &&
      N(args) > 0) {
    result= ams_abstract_field (name, args[0]); return true;
  }
  if (name == "tmtex-make-abstract-data") {
    scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
    result= ams_make_abstract_data (packed); return true;
  }
  return false;
}

bool
publisher_dispatch (string key, scheme_tree args, scheme_tree& result) {
  if (!ams_style () && !revtex_style () && !elsevier_style () &&
      !acm_style () && !ieee_style () && !beamer_style () &&
      !springer_any_style ())
    return false;
  scheme_tree source= source_apply (key, args);
  array<scheme_tree> hook_args;

  if (springer_any_style ()) {
    if (springer_style () && key == "doc-data") {
      result= llncs_style () ? llncs_doc_data (args) : springer_doc_data (args);
      return true;
    }
    if (llncs_style () && key == "doc-author") {
      result= llncs_doc_author (source);
      return true;
    }
    if (llncs_style () && key == "author-affiliation-ref") {
      result= llncs_affiliation_ref (args);
      return true;
    }
    if ((springer_style () &&
         (key == "doc-running-title" || key == "doc-subtitle" ||
          key == "doc-note" || key == "doc-misc" || key == "doc-date" ||
          key == "doc-running-author" || key == "author-affiliation" ||
          key == "author-email" || key == "author-homepage" ||
          key == "author-note" || key == "author-misc")) ||
        (svmono_style () && key == "doc-subtitle")) {
      result= springer_metadata_field (key, source);
      return true;
    }
    if (springer_style () &&
        (key == "abstract-keywords" || key == "abstract-msc" ||
         key == "abstract-acm" || key == "abstract-pacs")) {
      if (llncs_style () && key == "abstract-keywords")
        result= llncs_keywords (source);
      else if (!llncs_style ()) result= beamer_abstract_field ("tmtex-" * key, source);
      else {
        scheme_tree wrapper (TUPLE); wrapper << source;
        result= latex_export_metadata_default (key, wrapper);
      }
      return true;
    }
  }

  if (beamer_style ()) {
    if (key == "doc-data") {
      result= beamer_doc_data (args);
      return true;
    }
    if (key == "doc-running-title" || key == "doc-subtitle" ||
        key == "doc-note" || key == "doc-misc" || key == "doc-date" ||
        key == "doc-running-author" || key == "author-affiliation" ||
        key == "author-email" || key == "author-homepage" ||
        key == "author-note" || key == "author-misc") {
      result= beamer_metadata_field (key, source);
      return true;
    }
    if (key == "abstract-keywords" || key == "abstract-msc" ||
        key == "abstract-acm" || key == "abstract-pacs") {
      result= beamer_abstract_field ("tmtex-" * key, source);
      return true;
    }
    if (key == "slide") {
      result= beamer_slide (args);
      return true;
    }
    if (key == "tit") {
      result= beamer_title (args);
      return true;
    }
  }

  if (ieee_style ()) {
    if (key == "doc-date") {
      result= latex_export_metadata_field (key, source);
      return true;
    }
    if ((key == "author-affiliation" || key == "author-email" ||
         key == "author-homepage") &&
        (ieee_conf_style () || publisher_state.ieee_conference)) {
      result= ieee_author_field (key, source);
      if (!is_nil (result) && (!stree_list (result) || N(result) > 0)) return true;
    }
    if (ieee_tran_style () && publisher_state.ieee_clustered &&
        (key == "author-affiliation-ref" || key == "author-email-ref")) {
      result= ieee_reference_mark (args);
      return true;
    }
    if (ieee_tran_style () && publisher_state.ieee_clustered &&
        (key == "author-affiliation-label" || key == "author-email-label")) {
      result= ieee_reference_label (key, args);
      return true;
    }
    if (ieee_tran_style () && key == "abstract-keywords") {
      result= ieee_keywords (source);
      return true;
    }
  }

  if (acm_style ()) {
    if (key == "doc-subtitle" || key == "doc-note" || key == "doc-misc" ||
        key == "doc-date" || key == "author-name" || key == "author-email" ||
        key == "author-homepage" || key == "author-note" || key == "author-misc") {
      result= acm_metadata_field (key, source);
      return true;
    }
    if (key == "author-affiliation") {
      result= acm_affiliation (source);
      return true;
    }
    if (key == "conferenceinfo" || key == "CopyrightYear" || key == "crdata") {
      result= acm_passthrough_command (key, args);
      return true;
    }
  }

  if (revtex_style () && publisher_state.revtex_clustered && key == "doc-data") {
    result= revtex_clustered_doc_data (args);
    return true;
  }

  if (elsevier_style ()) {
    if (key == "elsevier-frontmatter") {
      result= elsevier_frontmatter_output (args);
      return true;
    }
    if (key == "doc-subtitle-ref" || key == "doc-note-ref" ||
        key == "doc-date-ref" || key == "doc-misc-ref" ||
        key == "author-note-ref" || key == "author-misc-ref" ||
        key == "author-affiliation-ref" || key == "author-email-ref" ||
        key == "author-homepage-ref") {
      result= elsevier_reference_output (key, args);
      return true;
    }
    if (key == "doc-subtitle-label" || key == "doc-note-label" ||
        key == "doc-date-label" || key == "doc-misc-label" ||
        key == "author-note-label" || key == "author-misc-label" ||
        key == "author-affiliation-label" || key == "author-email-label" ||
        key == "author-homepage-label") {
      result= elsevier_label_output (key, args);
      return true;
    }
    if (key == "author-name" || key == "author-affiliation" ||
        key == "author-email" || key == "author-homepage") {
      result= elsevier_author_field (key, source);
      return true;
    }
    if (key == "equation" || key == "equation*") {
      result= elsevier_equation_output (key, args);
      return true;
    }
  }

  if (key == "doc-running-title" || key == "doc-subtitle" ||
      key == "doc-note" || key == "doc-misc" || key == "doc-date" ||
      key == "author-affiliation" || key == "author-email" ||
      key == "author-homepage" || key == "author-note" ||
      key == "author-misc" || key == "abstract-keywords" ||
      key == "abstract-msc" || key == "abstract-pacs") {
    hook_args << source;
    if (publisher_hook ("tmtex-" * key, hook_args, result)) return true;
  }
  if (key == "doc-title" || key == "doc-running-title" ||
      key == "doc-subtitle" || key == "doc-note" || key == "doc-misc" ||
      key == "doc-date" || key == "doc-running-author" ||
      key == "author-name" || key == "author-affiliation" ||
      key == "author-email" || key == "author-homepage" ||
      key == "author-note" || key == "author-misc") {
    result= latex_export_metadata_field (key, source);
    return true;
  }
  if (key == "doc-author") {
    scheme_tree wrapper (TUPLE); wrapper << source;
    result= latex_export_metadata_default (key, wrapper);
    return true;
  }
  if (key == "doc-data" || key == "abstract-data") {
    scheme_tree packed (TUPLE); for (int i=0; i<N(args); ++i) packed << args[i];
    scheme_tree wrapper (TUPLE); wrapper << packed;
    result= latex_export_metadata_default (key, wrapper);
    return true;
  }
  if (key == "abstract" || key == "abstract-acm" ||
      key == "abstract-arxiv" || key == "abstract-pacs") {
    scheme_tree wrapper (TUPLE); wrapper << source;
    result= latex_export_metadata_default (key, wrapper);
    return true;
  }
  if (key == "equation" || key == "equation*") {
    result= latex_export_core_dispatch (key, args);
    return true;
  }
  if (starts (key, "doc-") || starts (key, "author-")) {
    result= latex_export_generic_function (key, args);
    return true;
  }
  return false;
}

} // namespace latex_export_internal
