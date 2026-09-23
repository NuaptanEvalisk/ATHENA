/******************************************************************************
* MODULE     : totex_images.cpp
* DESCRIPTION: Native image and graphics support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "new_view.hpp"
#include "scheme.hpp"

#include <cmath>

namespace {

using namespace latex_export_internal;

struct GeneratedImageName {
  url file;
  string latex_name;
};

scheme_tree
source_apply (string head, scheme_tree args) {
  scheme_tree out= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << args[i];
  return out;
}

string
force_string (scheme_tree value) {
  return string_atom (value) ? atom_text (value) : string ("");
}

GeneratedImageName
generated_image_name () {
  int serial= latex_export_next_serial ();
  string suffix= ".pdf";
  string postfix= "-" * as_string (serial) * suffix;
  GeneratedImageName result;
  result.file= glue (latex_export_image_root_url (), postfix);
  result.latex_name= latex_export_image_root_string () * postfix;
  return result;
}

string
quote_include_name (string name) {
  return "\"" * name * "\"";
}

string
clean_include_name (string name) {
  string athena_path= as_string (url ("$ATHENA_PATH"));
  name= replace (name, "$ATHENA_PATH", athena_path);
  name= replace (name, "file://", "");
  return name;
}

url
resolve_image_url (string name) {
  url plain= url_unix (name);
  url target= relative (save_target_url (), plain);
  url source= relative (save_source_url (), plain);
  if (exists (target)) return target;
  if (exists (source)) return source;
  if (exists (plain)) return plain;
  return target;
}

string
numbered_file_name (string name, int nr) {
  if (nr == 0) return name;
  string ext= suffix (url_unix (name));
  string stem= name;
  if (ext != "" && N(name) > N(ext)) stem= name (0, N(name) - N(ext) - 1);
  return stem * "-" * as_string (nr) * (ext == "" ? "" : "." * ext);
}

url
portable_image_dir () {
  return head (save_target_url ());
}

bool
portable_image_conflict (url source, string file_name) {
  url dest= expand (portable_image_dir ()) * url_system (file_name);
  return exists (dest) && as_system_string (source) != as_system_string (dest);
}

string
portable_image_fresh_name (url source, string tail_name) {
  for (int nr=0; ; ++nr) {
    string candidate= numbered_file_name (tail_name, nr);
    if (!latex_export_portable_image_used (candidate) &&
        !portable_image_conflict (source, candidate))
      return candidate;
  }
}

string
portable_image_copy (url source) {
  string key= as_system_string (source);
  object old= latex_export_portable_image_copy_get (key);
  if (is_string (old)) return as_string (old);

  string tail_name= as_system_string (tail (source));
  string file_name= portable_image_fresh_name (source, tail_name);
  url dir= expand (portable_image_dir ());
  url dest= dir * url_system (file_name);
  mkdir (dir);
  if (as_system_string (source) != as_system_string (dest)) copy (source, dest);
  latex_export_portable_image_record (key, file_name);
  return file_name;
}

scheme_tree
include_image_file (string original_name, url source) {
  latex_export_add_latex_extra_package ("graphicx");
  string include_name;
  if (latex_export_portable ()) include_name= portable_image_copy (source);
  else {
    url target_relative= relative (save_target_url (), url_unix (original_name));
    include_name= exists (target_relative) ? original_name : as_system_string (source);
    include_name= clean_include_name (include_name);
  }
  scheme_tree out= stree_apply ("includegraphics");
  out << stree_string (quote_include_name (include_name));
  return out;
}

scheme_tree
image_as_eps (string name) {
  url source= resolve_image_url (name);
  if (exists (source)) return include_image_file (name, source);

  GeneratedImageName generated= generated_image_name ();
  if (starts (name, "..")) source= relative (save_source_url (), url_unix (name));
  string from= as_string (call ("format-from-suffix", object (suffix (source)))) * "-file";
  string to= suffix (generated.file) == "pdf" ? "pdf-file" : "postscript-file";
  array<object> argv;
  argv << object (source) << object (from) << object (to) << object (generated.file);
  (void) call ("convert-to-file", argv);
  latex_export_add_latex_extra_package ("graphicx");
  scheme_tree out= stree_apply ("includegraphics");
  out << stree_string (quote_include_name (generated.latex_name));
  return out;
}

string
image_length (scheme_tree value) {
  string s= force_string (value);
  if (s == "" || ends (s, "%")) return "!";
  double number= 0.0;
  string unit;
  parse_length (s, number, unit);
  if (unit == "w" || unit == "h") return "!";
  return latex_tmtex_decode_length (object (s));
}

bool
image_magnification (scheme_tree value, double& result) {
  string s= force_string (value);
  if (s == "") { result= 0.0; return true; }
  if (ends (s, "%")) {
    string number= s (0, N(s)-1);
    result= is_double (number) ? as_double (number) / 100.0 : 0.0;
    return true;
  }
  double number= 0.0;
  string unit;
  parse_length (s, number, unit);
  if (unit == "w" || unit == "h") { result= number; return true; }
  return false;
}

scheme_tree
image_size_records (scheme_tree width, scheme_tree height) {
  string w= force_string (width), h= force_string (height);
  scheme_tree records (TUPLE);
  if (!latex_tmtex_px_length (object (w)) && !latex_tmtex_px_length (object (h)))
    return records;
  scheme_tree values (TUPLE);
  values << stree_string ("img_size") << stree_string ("width") << stree_string (w)
         << stree_string ("height") << stree_string (h);
  records << stree_string (latex_export_athena_data_record ("aux", values));
  return records;
}

scheme_tree render_image_native (scheme_tree source);

scheme_tree
image_output (scheme_tree args) {
  scheme_tree source= source_apply ("image", args);
  if (!stree_list (args) || N(args) < 3) return latex_export_athena_data_wrap (source, stree_string (""));

  if (!string_atom (args[0]))
    return latex_export_athena_data_wrap (source, render_image_native (source));

  latex_export_add_latex_extra_package ("graphicx");
  scheme_tree width= args[1], height= args[2];
  scheme_tree fig= image_as_eps (atom_text (args[0]));
  string horizontal= image_length (width), vertical= image_length (height);
  double mh= 0.0, mv= 0.0;
  bool has_mh= image_magnification (width, mh);
  bool has_mv= image_magnification (height, mv);

  scheme_tree body;
  if (!has_mh || !has_mv) {
    body= stree_apply ("resizebox");
    body << stree_string (horizontal) << stree_string (vertical) << fig;
  }
  else if ((mh == 0.0 && mv == 0.0) || mh == 1.0 || mv == 1.0) body= fig;
  else {
    body= stree_apply ("scalebox");
    body << stree_string (scheme_inexact_string (mh == 0.0 ? mv : mh)) << fig;
  }

  return latex_export_athena_data_wrap_records (
    source, image_size_records (width, height), body);
}

scheme_tree
render_image_native (scheme_tree source) {
  if (math_mode ()) {
    scheme_tree wrapped= stree_apply ("with");
    wrapped << stree_string ("mode") << stree_string ("math") << source;
    source= wrapped;
  }

  latex_export_add_latex_extra_package ("graphicx");
  GeneratedImageName generated= generated_image_name ();
  array<SI> extents= get_current_editor ()->print_snippet (
    generated.file, scheme_tree_to_tree (source), true);
  if (N(extents) < 10 || extents[9] == 0)
    return stree_string ("");

  double unit= (1.0 / 60984.0) * (600.0 / (double) extents[9]);
  double x3= unit * (double) extents[0];
  double y3= unit * (double) extents[1];
  double x4= unit * (double) extents[2];
  double y4= unit * (double) extents[3];
  double x1= unit * (double) extents[4];
  double x2= unit * (double) extents[6];

  string lm= scheme_inexact_string (x3 - x1) * "cm";
  string rm= scheme_inexact_string (x2 - x4) * "cm";
  string ww= scheme_inexact_string (x4 - x3) * "cm";
  string hh= scheme_inexact_string (y4 - y3) * "cm";

  scheme_tree option= stree_apply ("!option");
  option << stree_string ("width=" * ww * ",height=" * hh);
  scheme_tree include= stree_apply ("includegraphics");
  include << option << stree_string (quote_include_name (generated.latex_name));

  double rat= y3 / (y4 - y3);
  scheme_tree dy= stree_apply ("!concat");
  dy << stree_string (scheme_inexact_string (rat)) << stree_apply ("height");
  scheme_tree raised= stree_apply ("raisebox");
  raised << dy << include;

  if (std::fabs (x3 - x1) < 0.01 && std::fabs (x2 - x4) < 0.01)
    return raised;

  scheme_tree left= stree_apply ("hspace");
  left << stree_string (lm);
  scheme_tree right= stree_apply ("hspace");
  right << stree_string (rm);
  scheme_tree parts (TUPLE);
  parts << left << raised << right;
  return latex_tex_concat (parts);
}

} // namespace

scheme_tree
latex_export_image (scheme_tree args) {
  return image_output (args);
}

scheme_tree
latex_export_render_image (scheme_tree source) {
  return render_image_native (source);
}
