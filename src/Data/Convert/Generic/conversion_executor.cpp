/******************************************************************************
* Native conversion execution. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#include "conversion_executor.hpp"
#include "conversion_graph.hpp"
#include "conversion_registry.hpp"
#include "conversion_options.hpp"
#include "image_conversion.hpp"
#include "convert.hpp"
#include "Data/Convert/Xml/html_import.hpp"
#include "Data/Convert/Xml/html_export.hpp"
#include "Data/Convert/Verbatim/verbatim.hpp"
#include "new_style.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "Scheme/Scheme/glue.hpp"
#include "Scheme/Scheme/object.hpp"
#include "System/Language/locale.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "analyze.hpp"
#include "node_metadata.hpp"
#include "scheme_execution_context.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include <stdexcept>

namespace {
struct request_context { url source, target; editor_rep* rendering_editor; };
thread_local const request_context* active_request= nullptr;
struct request_scope {
  request_context context;
  const request_context* previous;
  request_scope (url source, url target, editor_rep* rendering_editor):
    context {source, target, rendering_editor}, previous (active_request) {
    active_request= &context;
  }
  ~request_scope () { active_request= previous; }
};
struct temporary_files {
  std::vector<url> files;
  url retained= url_none ();
  ~temporary_files () {
    for (const auto& file: files) if (file != retained) remove (file);
  }
};
object serial (tree value) { return tmscm_to_object (scheme_tree_to_tmscm (value)); }
tree native (object value) {
  return is_tree (value) ? as_tree (value) : scheme_tree_to_tree (as_scheme_tree (value));
}
bool v3_math (tree value) {
  if (!is_tuple (value)) return false;
  for (const char* tag: {"math-block", "math-grid", "scripts", "math-accent",
       "math-annotation", "math-long-arrow", "math-middle", "math-symbol",
       "math-variant", "delimited", "big-operator"})
    if (N(value) && value[0] == tag) return true;
  for (int i=0; i<N(value); ++i) if (v3_math (value[i])) return true;
  return false;
}
void check_latex_properties (const tree& source) {
  if (const auto* metadata= athena::node::get (source)) {
    for (const auto& [name, value]: metadata->properties) {
      const bool supported=
        (L(source) == MATH_SCRIPTS && name == "math:scripts-policy") ||
        (L(source) == MATH_ACCENT && name == "math:accent-stretch") ||
        (L(source) == MATH_BLOCK && name == "math:block-numbered") ||
        (L(source) == MATH_GRID && name == "math:grid-kind") ||
        (L(source) == ROW &&
          (name == "math:row-numbering" || name == "math:row-tag")) ||
        (L(source) == CELL && name == "math:cell-format");
      if (!supported)
        throw std::runtime_error (
          "LaTeX export cannot preserve native node metadata: " + name);
    }
  }
  // XML v3 source UUIDs are identities, not LaTeX output properties. Their
  // omission is intrinsic to exporting to a different document format.
  for (int i=0; i<N(source); ++i) check_latex_properties (source[i]);
}
void check_latex (tree value) {
  if (value == tree ("#f")) throw std::runtime_error ("XML v3 LaTeX export failed: no output");
  if (!is_tuple (value)) return;
  if (N(value) && (value[0] == "nonconverted" || value[0] == "!athena-data-inline" ||
                   value[0] == "athena-preserved-object"))
    throw std::runtime_error ("Unsupported XML v3 LaTeX export: preservation or nonconverted placeholder");
  for (int i=0; i<N(value); ++i) check_latex (value[i]);
}
}

url conversion_source_url () { return active_request ? active_request->source : url_none (); }
url conversion_target_url () { return active_request ? active_request->target : url_none (); }
editor_rep* conversion_rendering_editor () {
  return active_request ? active_request->rendering_editor : nullptr;
}

namespace {
url input_source (object input, string from) {
  if (ends (from, "-file")) return as_url (input);
  if (active_request) return active_request->source;
  const auto* owner= current_scheme_execution_context ();
  if (owner && owner->actor) return owner->actor->current_state ()->master;
  return url_none ();
}
}

object conversion_dispatch (object input, string from, string to, object options) {
  return conversion_execute (input, from, to, options, input_source (input, from),
                             conversion_target_url ());
}

object conversion_to_file (object input, string from, string to, url target, object options) {
  options= cons (cons (symbol_object ("dest"), object (target)), options);
  return conversion_execute (input, from, to, options, input_source (input, from), target);
}

string conversion_file_format (url source) {
  return conversion_format_from_suffix (suffix (source)) * "-file";
}

bool conversion_file_available (url source, url target) {
  string from= conversion_file_format (source), to= conversion_file_format (target);
  return bool (conversion_graph_path ({from.data (), std::size_t (N(from))},
                                     {to.data (), std::size_t (N(to))}));
}

object conversion_file (url source, url target, object options) {
  return conversion_to_file (object (source), conversion_file_format (source),
                            conversion_file_format (target), target, options);
}

string conversion_latex_document (tree input, object options, url source, url target) {
  check_latex_properties (input);
  tree prepared= latex_export_prepare_v3_source (input);
  tree output= latex_export_convert_top (prepared, options, source, target);
  if (v3_math (prepared)) check_latex (output);
  return serialize_latex (output);
}

static string convert_verbatim (tree input, bool wrap, string encoding,
                               url source, editor_rep* editor) {
  if (is_snippet (input) && editor) {
    const tree expanded= editor->exec_verbatim (input, editor->the_path ());
    return tree_to_verbatim (expanded, wrap, encoding);
  }
  tree document= input;
  if (is_snippet (document))
    document= tree (DOCUMENT, compound ("style", tuple ("generic")),
      compound ("body", is_document (input) ? input : tree (DOCUMENT, input)));
  const auto prepare= prepare_document_source_contracts (
    document, is_none (source) ? url ("$PWD/none") : source);
  const auto output= source_to_verbatim (document, wrap, encoding,
    [prepare] (path source) {
      std::vector<int> where;
      for (; !is_nil (source); source= source->next) where.push_back (source->item);
      return prepare (where);
    });
  if (!output) throw std::runtime_error ("Text export source context is unavailable");
  return *output;
}

static object execute_step (object input, const conversion_edge& edge,
    object ordered, const conversion_options& values, url source, url target,
    url destination, bool last, temporary_files& temporary,
    editor_rep* rendering_editor) {
  const auto& action= edge.executor;
  string from (edge.from.c_str ()), to (edge.to.c_str ());
  auto option= [&] (const char* name, const char* fallback) {
    auto found= values.find (name);
    return found == values.end () ? string (fallback) : string (found->second.c_str ());
  };
  if (action == "load-file") {
    string data;
    if (load_string (as_url (input), data, false)) throw std::runtime_error ("Cannot read conversion source");
    if (conversion_format_image (from (0, N(from)-5))) return tmscm_to_object (bytes_to_tmscm (data));
    return object (data);
  }
  if (action == "save-file") {
    url output= last && !is_none (destination) ? destination : url_temp ();
    if (!last || is_none (destination)) temporary.files.push_back (output);
    string data= is_string (input) ? as_string (input) : tmscm_to_bytes (object_to_tmscm (input));
    if (save_string (output, data, false)) throw std::runtime_error ("Cannot write conversion output");
    return object (output);
  }
  if (action == "convert-image") {
    url output= conversion_image_run (from (0, N(from)-5), to (0, N(to)-5),
      as_url (input), last ? destination : url_none (), option ("texmacs->image:raster-resolution", ""));
    if (!last || is_none (destination)) temporary.files.push_back (output);
    return object (output);
  }
  if (action == "tree-to-stree" || action == "write-stm") {
    tree source= native (input);
    if (athena::node::contains_metadata (source))
      throw std::runtime_error (
        "Scheme/STM export cannot preserve native node metadata; use ATHENA XML v3");
    return action == "tree-to-stree" ? serial (tree_to_scheme_tree (source))
                                    : object (tree_to_scheme_document (source));
  }
  if (action == "stree-to-tree") return object (native (input));
  if (action == "read-document") return object (conversion_read_document (as_string (input)));
  if (action == "read-fragment") return object (conversion_read_fragment (as_string (input)));
  if (action == "write-document") return object (conversion_write_document (native (input)));
  if (action == "write-fragment") return object (conversion_write_fragment (native (input)));
  if (action == "read-stm-document") return object (scheme_document_to_tree (as_string (input)));
  if (action == "read-stm-snippet") return object (scheme_to_tree (as_string (input)));
  if (action == "write-code") return object (convert_verbatim (
    native (input), false, get_locale_charset (), source, rendering_editor));
  if (action == "write-source-code") return object (convert_verbatim (
    native (input), false, "SourceCode", source, rendering_editor));
  if (action == "read-code-snippet") return object (verbatim_to_tree (as_string (input), false, "SourceCode"));
  if (action == "read-code-document") return object (verbatim_document_to_tree (as_string (input), false, "SourceCode"));
  if (action == "read-verbatim-document" || action == "read-verbatim-snippet") {
    bool wrap= option ("verbatim->texmacs:wrap", "off") == "on";
    string encoding= option ("verbatim->texmacs:encoding", "auto");
    if (encoding == "cork") encoding= "utf-8";
    return object (action == "read-verbatim-document"
      ? verbatim_document_to_tree (as_string (input), wrap, encoding)
      : verbatim_to_tree (as_string (input), wrap, encoding));
  }
  if (action == "write-verbatim-document" || action == "write-verbatim-snippet") {
    string encoding= option ("texmacs->verbatim:encoding", "auto");
    if (encoding == "cork") encoding= "utf-8";
    bool wrap= option ("texmacs->verbatim:wrap", "off") == "on";
    if (action == "write-verbatim-snippet" && rendering_editor != nullptr) {
      if (rendering_editor->get_env_string ("mode") == "prog" ||
          rendering_editor->get_env_string ("font-family") == "tt")
        wrap= false;
    }
    return object (convert_verbatim (native (input), wrap, encoding, source, rendering_editor));
  }
  if (action == "parse-html-document") return serial (parse_html_document (as_string (input)));
  if (action == "parse-html-snippet") return serial (parse_html_snippet (as_string (input)));
  if (action == "serialize-html") return object (serialize_html (as_scheme_tree (input)));
  if (action == "import-html-tree") return object (html_import_document (as_scheme_tree (input)));
  if (action == "import-html-stree") return serial (html_import_document_serial (as_scheme_tree (input)));
  if (action == "export-html" || action == "export-html-document") {
    tree html= html_export_document (input, ordered, source, target, rendering_editor);
    return action == "export-html" ? serial (html) : object (serialize_html (html));
  }
  if (action == "export-latex" || action == "export-latex-document") {
    if (is_tree (input)) check_latex_properties (as_tree (input));
    tree prepared= is_tree (input) ? latex_export_prepare_v3_source (as_tree (input)) : as_scheme_tree (input);
    bool structured= v3_math (prepared);
    if (action == "export-latex-document" && !structured)
      return object (conservative_texmacs_to_latex (scheme_tree_to_tree (prepared), ordered));
    tree output= latex_export_convert_top (prepared, ordered, source, target);
    if (structured) check_latex (output);
    return action == "export-latex" ? serial (output) : object (serialize_latex (output));
  }
  if (action == "serialize-latex") return object (serialize_latex (as_scheme_tree (input)));
  if (action == "parse-latex-document") return object (parse_latex_document (as_string (input)));
  if (action == "parse-latex-snippet") return object (parse_latex (as_string (input)));
  if (action == "import-latex-document") return object (latex_native_document_to_texmacs (
    as_string (input), option ("latex->texmacs:fallback-on-pictures", "off") == "on"));
  if (action == "import-latex-class") return object (latex_class_document_to_tree (as_string (input)));
  if (action == "import-latex-tree") return object (latex_to_tree (native (input)));
  if (action == "import-mathml-xml") return object (mathml_xml_to_tree_v3 (as_string (input)));
  if (action == "import-mathml-stree") return object (mathml_to_tree_v3 (as_scheme_tree (input)));
  if (action == "import-markdown") return object (aofm_markdown_to_tree (as_string (input)));
  throw std::runtime_error ("Unknown native conversion executor: " + action);
}

object conversion_execute (object input, string from, string to,
                           object options, url source, url target,
                           editor_rep* rendering_editor) {
  // A direct Scheme request may already belong to an editor. Passive actor
  // exports supply their actual rendering editor explicitly instead.
  if (rendering_editor == nullptr) {
    const auto* owner= current_scheme_execution_context ();
    if (owner && owner->editor && owner->has (SCHEME_CAPABILITY_BUFFER))
      rendering_editor= owner->editor;
  }
  auto path= conversion_graph_path ({from.data (), std::size_t (N(from))},
                                    {to.data (), std::size_t (N(to))});
  if (!path)
    throw std::runtime_error ("Unsupported native conversion route: " +
                              std::string (from.data (), N(from)) + " -> " +
                              std::string (to.data (), N(to)));
  object settings= null_object ();
  url destination= url_none ();
  bool have_destination= false;
  for (object tail= options; !is_null (tail); tail= cdr (tail)) {
    object entry= car (tail), key= car (entry), value= cdr (entry);
    if (is_string (key)) settings= cons (entry, settings);
    else if (is_symbol (key) && as_symbol (key) == "dest" && !have_destination) {
      destination= as_url (value); have_destination= true;
    }
  }
  object ordered= null_object ();
  for (; !is_null (settings); settings= cdr (settings)) ordered= cons (car (settings), ordered);
  auto values= conversion_options_from_scheme (ordered);
  for (const auto& entry: conversion_options_snapshot ()) values.emplace (entry);
  ordered= conversion_options_to_scheme (values);
  // (convert ... '(dest . URL)) has no separate target argument. Its resource
  // writers still need that destination, just as convert-to-file does.
  url effective_target= is_none (target) ? destination : target;
  request_scope scope (source, effective_target, rendering_editor);
  temporary_files temporary;
  object result= input;
  for (std::size_t i=0; i<path->size (); ++i)
    result= execute_step (result, (*path)[i], ordered, values, source, effective_target,
                          destination, i+1 == path->size (), temporary,
                          rendering_editor);
  if (is_url (result)) {
    url output= as_url (result);
    if (!is_none (destination) && output != destination) {
      copy (output, destination);
      result= object (destination);
    }
    temporary.retained= as_url (result);
  }
  return result;
}
