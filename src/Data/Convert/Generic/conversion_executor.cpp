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
#include "System/Language/locale.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "analyze.hpp"
#include "node_metadata.hpp"
#include "execution_context.hpp"
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
// Serialized values previously crossed a reconstructing marshalling boundary.
// Keep that detached snapshot contract without allocating runtime pairs.
conversion_payload serial (tree value) { return conversion_stree {copy (value)}; }
tree serialized (const conversion_payload& value) {
  return copy (std::get<conversion_stree> (value).data);
}
tree native (const conversion_payload& value) {
  if (const auto* source= std::get_if<tree> (&value)) return *source;
  return scheme_tree_to_tree (serialized (value));
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
url input_source (const conversion_payload& input, string from) {
  if (ends (from, "-file")) return std::get<url> (input);
  if (active_request) return active_request->source;
  const auto* owner= current_execution_context ();
  if (owner && owner->actor) return owner->actor->current_state ()->master;
  return url_none ();
}
}

conversion_payload conversion_dispatch (conversion_payload input, string from,
                                         string to, conversion_options options) {
  return conversion_execute (input, from, to, std::move (options), input_source (input, from),
                             conversion_target_url ());
}

conversion_payload conversion_to_file (conversion_payload input, string from,
                                        string to, url target,
                                        conversion_options options) {
  return conversion_execute (input, from, to, std::move (options),
    input_source (input, from), target, nullptr, target);
}

string conversion_file_format (url source) {
  return conversion_format_from_suffix (suffix (source)) * "-file";
}

bool conversion_file_available (url source, url target) {
  string from= conversion_file_format (source), to= conversion_file_format (target);
  return bool (conversion_graph_path ({from.data (), std::size_t (N(from))},
                                     {to.data (), std::size_t (N(to))}));
}

string conversion_latex_document (tree input, const conversion_options& options, url source, url target) {
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

static conversion_payload execute_step (const conversion_payload& input,
    const conversion_edge& edge,
    const conversion_options& values, url source, url target,
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
    if (load_string (std::get<url> (input), data, false)) throw std::runtime_error ("Cannot read conversion source");
    if (conversion_format_image (from (0, N(from)-5))) return conversion_bytes {data};
    return data;
  }
  if (action == "save-file") {
    url output= last && !is_none (destination) ? destination : url_temp ();
    if (!last || is_none (destination)) temporary.files.push_back (output);
    const auto* text= std::get_if<string> (&input);
    string data= text ? *text : std::get<conversion_bytes> (input).data;
    if (save_string (output, data, false)) throw std::runtime_error ("Cannot write conversion output");
    return output;
  }
  if (action == "convert-image") {
    url output= conversion_image_run (from (0, N(from)-5), to (0, N(to)-5),
      std::get<url> (input), last ? destination : url_none (), option ("texmacs->image:raster-resolution", ""));
    if (!last || is_none (destination)) temporary.files.push_back (output);
    return output;
  }
  if (action == "tree-to-stree" || action == "write-stm") {
    tree source= native (input);
    if (athena::node::contains_metadata (source))
      throw std::runtime_error (
        "Scheme/STM export cannot preserve native node metadata; use ATHENA XML v3");
    if (action == "tree-to-stree") return serial (tree_to_scheme_tree (source));
    return tree_to_scheme_document (source);
  }
  if (action == "stree-to-tree") return native (input);
  if (action == "read-document") return conversion_read_document (std::get<string> (input));
  if (action == "read-fragment") return conversion_read_fragment (std::get<string> (input));
  if (action == "write-document") return conversion_write_document (native (input));
  if (action == "write-fragment") return conversion_write_fragment (native (input));
  if (action == "read-stm-document") return scheme_document_to_tree (std::get<string> (input));
  if (action == "read-stm-snippet") return scheme_to_tree (std::get<string> (input));
  if (action == "write-code") return convert_verbatim (
    native (input), false, get_locale_charset (), source, rendering_editor);
  if (action == "write-source-code") return convert_verbatim (
    native (input), false, "SourceCode", source, rendering_editor);
  if (action == "read-code-snippet") return verbatim_to_tree (std::get<string> (input), false, "SourceCode");
  if (action == "read-code-document") return verbatim_document_to_tree (std::get<string> (input), false, "SourceCode");
  if (action == "read-verbatim-document" || action == "read-verbatim-snippet") {
    bool wrap= option ("verbatim->texmacs:wrap", "off") == "on";
    string encoding= option ("verbatim->texmacs:encoding", "auto");
    if (encoding == "cork") encoding= "utf-8";
    return action == "read-verbatim-document"
      ? verbatim_document_to_tree (std::get<string> (input), wrap, encoding)
      : verbatim_to_tree (std::get<string> (input), wrap, encoding);
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
    return convert_verbatim (native (input), wrap, encoding, source, rendering_editor);
  }
  if (action == "parse-html-document") return serial (parse_html_document (std::get<string> (input)));
  if (action == "parse-html-snippet") return serial (parse_html_snippet (std::get<string> (input)));
  if (action == "serialize-html") return serialize_html (serialized (input));
  if (action == "import-html-tree") return html_import_document (serialized (input));
  if (action == "import-html-stree") return serial (html_import_document_serial (serialized (input)));
  if (action == "export-html" || action == "export-html-document") {
    const auto* document= std::get_if<tree> (&input);
    tree prepared= document ? latex_export_prepare_v3_source (*document)
                            : serialized (input);
    tree html= html_export_document (prepared, values, source, target, rendering_editor);
    if (action == "export-html") return serial (html);
    return serialize_html (html);
  }
  if (action == "export-latex" || action == "export-latex-document") {
    const auto* document= std::get_if<tree> (&input);
    if (document) check_latex_properties (*document);
    tree prepared= document ? latex_export_prepare_v3_source (*document)
                            : serialized (input);
    bool structured= v3_math (prepared);
    if (action == "export-latex-document" && !structured)
      return conservative_texmacs_to_latex (
        scheme_tree_to_tree (prepared), values);
    tree output= latex_export_convert_top (prepared, values, source, target);
    if (structured) check_latex (output);
    if (action == "export-latex") return serial (output);
    return serialize_latex (output);
  }
  if (action == "serialize-latex") return serialize_latex (serialized (input));
  if (action == "parse-latex-document") return parse_latex_document (std::get<string> (input));
  if (action == "parse-latex-snippet") return parse_latex (std::get<string> (input));
  if (action == "import-latex-document") return latex_native_document_to_texmacs (
    std::get<string> (input), option ("latex->texmacs:fallback-on-pictures", "off") == "on");
  if (action == "import-latex-class") return latex_class_document_to_tree (std::get<string> (input));
  if (action == "import-latex-tree") return latex_to_tree (native (input));
  if (action == "import-mathml-xml") return mathml_xml_to_tree_v3 (std::get<string> (input));
  if (action == "import-mathml-stree") return mathml_to_tree_v3 (serialized (input));
  if (action == "import-markdown") return aofm_markdown_to_tree (std::get<string> (input));
  throw std::runtime_error ("Unknown native conversion executor: " + action);
}

conversion_payload conversion_execute (conversion_payload input, string from,
                                        string to, conversion_options values,
                                        url source, url target,
                                        editor_rep* rendering_editor, url destination) {
  // A direct request may already belong to an editor. Passive actor
  // exports supply their actual rendering editor explicitly instead.
  if (rendering_editor == nullptr) {
    const auto* owner= current_execution_context ();
    if (owner && owner->editor && owner->has (EXECUTION_CAPABILITY_BUFFER))
      rendering_editor= owner->editor;
  }
  auto path= conversion_graph_path ({from.data (), std::size_t (N(from))},
                                    {to.data (), std::size_t (N(to))});
  if (!path)
    throw std::runtime_error ("Unsupported native conversion route: " +
                              std::string (from.data (), N(from)) + " -> " +
                              std::string (to.data (), N(to)));
  for (const auto& entry: conversion_options_snapshot ()) values.emplace (entry);
  // Resource writers need the final destination even when no separate target
  // context was supplied.
  url effective_target= is_none (target) ? destination : target;
  request_scope scope (source, effective_target, rendering_editor);
  temporary_files temporary;
  conversion_payload result= std::move (input);
  for (std::size_t i=0; i<path->size (); ++i)
    result= execute_step (result, (*path)[i], values, source, effective_target,
                          destination, i+1 == path->size (), temporary,
                          rendering_editor);
  if (const auto* file= std::get_if<url> (&result)) {
    url output= *file;
    if (!is_none (destination) && output != destination) {
      copy (output, destination);
      result= destination;
    }
    temporary.retained= std::get<url> (result);
  }
  return result;
}
