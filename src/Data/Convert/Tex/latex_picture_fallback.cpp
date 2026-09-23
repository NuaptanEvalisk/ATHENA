
/******************************************************************************
* MODULE     : latex_picture_fallback.cpp
* DESCRIPTION: picture fallback for unsupported LaTeX imports
* COPYRIGHT  : (C) 2013  François Poulain, Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Tex/latex_picture_fallback.hpp"
#include "Ghostscript/gs_utilities.hpp"
#include "Tex/convert_tex.hpp"
#include "analyze.hpp"
#include "file.hpp"
#include "sys_utils.hpp"

static inline void
dbg (string s) {
  if (DEBUG_CONVERT) debug_convert << s << LF;
}

static string latex_command= "pdflatex";
static const string fallback_marker= "\\latex_picture_fallback";

void
set_latex_command (string cmd) {
  latex_command= cmd;
}

static bool
latex_present () {
  return exists_in_path (latex_command);
}

static array<string>
search_picture_fallbacks (tree t) {
  array<string> r;
  if (is_atomic (t));
  else if (is_tuple (t, "\\def") || is_tuple (t, "\\def*")
      || is_tuple (t, "\\def**") || is_tuple (t, "\\newenvironment**") ||
      is_tuple (t, "\\newenvironment") || is_tuple (t, "\\newenvironment*"));
  else if (is_tuple (t, fallback_marker, 2))
    r << as_string (t[1]);
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      r << search_picture_fallbacks (t[i]);
  }
  return r;
}

static tree
substitute_picture_fallbacks (tree t, array<tree> pictures, int& i) {
  if (N(pictures) <= i);
  else if (is_atomic (t));
  else if (is_tuple (t, fallback_marker, 2)) {
    t[0]= "\\picture-mixed";
    t[1]= pictures[i++];
  }
  else if (is_tuple (t, "\\def") || is_tuple (t, "\\def*")
      || is_tuple (t, "\\def**") || is_tuple (t, "\\newenvironment**") ||
      is_tuple (t, "\\newenvironment") || is_tuple (t, "\\newenvironment*"));
  else
    for (int j=0; j<N(t); j++)
      t[j]= substitute_picture_fallbacks (t[j], pictures, i);
  return t;
}

static int
count_unbalanced_picture_fallbacks (tree t) {
  if (!is_concat (t)) return 0;
  int count= 0;
  for (int i=0; i<N(t); i++) {
    tree v= t[i];
    if (is_tuple (v, fallback_marker, 2)
        && starts (as_string (v[1]), "begin-")) count++;
    if (is_tuple (v, fallback_marker, 2)
        && starts (as_string (v[1]), "end-")) count--;
    if (is_concat (v)) count += count_unbalanced_picture_fallbacks (v);
  }
  return count;
}

static tree
merge_environment_picture_fallbacks (tree t) {
  if (is_atomic (t)) return t;
  if (is_tuple (t, "\\def") || is_tuple (t, "\\def*")
      || is_tuple (t, "\\def**") || is_tuple (t, "\\newenvironment**") ||
      is_tuple (t, "\\newenvironment") || is_tuple (t, "\\newenvironment*"))
    return t;
  int n= N(t);
  tree r (L(t));
  string name;
  tree code;
  bool in_env= false;
  for (int i=0; i<n; i++) {
    tree v= t[i];
    if (!in_env && is_concat (t) && is_tuple (v, fallback_marker, 2)
        && starts (as_string (v[1]), "begin-")) {
      in_env= true;
      name= as_string (v[1]);
      code= v[2];
    }
    if (in_env && is_concat (t) && is_tuple (v, fallback_marker, 2)
        && starts (as_string (v[1]), "end-")) {
      in_env= false;
      code= concat (code, v[2]);
      r << tuple (fallback_marker, name, code);
      name= "";
    }
    else if (is_concat (t) && count_unbalanced_picture_fallbacks (v) != 0) {
      tree tmp (CONCAT);
      for (int j=0; j<N(v); j++) tmp << v[j];
      for (int j=i+1; j<n; j++) tmp << t[j];
      t= tmp;
      n= N(t);
      i= -1;
    }
    else if (!in_env)
      r << merge_environment_picture_fallbacks (v);
  }
  return r;
}

static void
clean_picture_fallback_directory (url u) {
  bool flag= false;
  array<string> content= read_directory (u, flag);
  for (int i=0; i<N(content); i++)
    if (content[i] != "." && content[i] != "..")
      remove (u * content[i]);
  rmdir (u);
}

static string
remove_latex_format_directive (string s) {
  int i= 0, start= 0, n= N(s);
  while (i<n) {
    if (test (s, 0, "%&") || (i > 0 && test (s, i, "\n%&"))) {
      start= i++;
      bool cut= false;
      while (i<n && s[i] != '\n') {
        if (test (s, i, "tex")) cut= true;
        i++;
      }
      if (cut) {
        s= s(0, start) * '\n' * s(i+1, n);
        n= N(s);
      }
    }
    else if (test (s, i, "\\usepackage") || test (s, i, "\\begin"))
      break;
    i++;
  }
  return s;
}

static void
install_picture_fallback_preamble (string s, tree t, url wdir, bool dvips) {
  s= remove_latex_format_directive (s);
  int i= 0;
  array<string> macros= search_picture_fallbacks (t);
  hashmap<string,bool> done (false);
  string preview= "%%%%%%%%%%%%%% ADDED BY TEXMACS %%%%%%%%%%%%%%%%%%\n";
  if (!dvips)
    preview  << "\\usepackage[active,tightpage,delayed]{preview}\n";
  else
    preview  << "\\usepackage[active,tightpage,delayed,psfixbb,dvips]{preview}\n";

  for (i=0; i<N(macros); i++) {
    if (macros[i] == "") continue;
    if (done[macros[i]]) continue;
    if (test (macros[i], 0, "end-")) continue;
    int arity= latex_arity (macros[i]);
    bool option= (arity<0);
    string arity_code;
    if (option) {
      arity_code << "[]";
      arity= -arity;
    }
    while (arity-- > 0) arity_code << "{}";
    string name= "\\" * macros[i];
    string cmd= "\\PreviewMacro";
    if (test (name, 0, "\\begin-")) {
      name= name(7, N(name));
      cmd= "\\PreviewEnvironment";
    }
    preview << cmd << "[{" * arity_code * "}]{" * name * "}\n";
    done(macros[i])= true;
  }
  preview << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";

  i= latex_search_forwards ("\\documentclass", 0, s);
  i= latex_search_forwards ("{", i, s);
  i= latex_search_forwards ("}", i, s);
  i= latex_search_forwards ("\n", i, s);
  i++;
  s= s(0, i) * preview * s(i, N(s));
  save_string (wdir * "temp.tex", s);
}

static tree
load_picture_fallback_image (url image) {
#if defined(USE_GS)
  string s;
  tree t (IMAGE, 5);
  load_string (image, s, false);
  if (s == "") {
    dbg ("Could not load " * as_string (image));
    return array<tree> ();
  }
  int width, height;
  gs_image_size (image, width, height);
  t[0]= tuple (tree (RAW_DATA, s), "eps");
  t[1]= as_string (width) * "pt";
  t[2]= as_string (height) * "pt";
  return (t);
#else
    dbg ("LaTeX picture fallback: Ghostscript is not available");
  return array<tree> ();
#endif
}

static array<tree>
load_picture_fallbacks (url wdir, bool dvips, int count) {
#if defined(USE_GS)
  string cmdln= "cd \"" * as_string (wdir) * "\"; ";
  if (dvips) {
    cmdln << "dvips temp.dvi && "
      << gs_prefix()* " -sDEVICE="*eps_device()*" -dSAFER -q -dNOPAUSE -dBATCH "
      << "-dLanguageLevel=3 -sOutputFile=temp%d.eps temp.ps";
  }
  else {
    if (!exists_in_path ("pdftops")) {
      dbg ("LaTeX picture fallback: pdftops not found");
      return array<tree> ();
    }
    cmdln= "";
    for (int i=1; i<=count; i++) {
      if (i > 1) cmdln << " && ";
      cmdln << "pdftops -eps -f " << as_string (i) << " -l " << as_string (i)
            << " " << sys_concretize (wdir * "temp.pdf")
            << " " << sys_concretize (wdir * ("temp" * as_string (i) * ".eps"));
    }
  }
  dbg ("Picture extraction command: " * cmdln);
  if (system (cmdln)) {
    dbg ("Could not extract pictures from LaTeX document");
    return array<tree> ();
  }
  unsigned int cnt= 1;
  bool stop= false;
  array<tree> r;
  while (!stop) {
    url u= wdir * ("temp" * as_string (cnt) * ".eps");
    if (exists (u)) {
      // gs can produce empty pictures, to be ignored
      tree tmp= load_picture_fallback_image (u);
      if (N(tmp) == 5 && (tmp[1] != "0pt" || tmp[2] != "0pt"))
        r << tmp;
      cnt++;
    }
    else
      stop= true;
  }
  return r;
#else
  dbg ("LaTeX picture fallback: Ghostscript is not available");
  return array<tree> ();
#endif
}

static array<tree>
render_picture_fallbacks (string s, tree t) {
  if (!latex_present () && !exists_in_path ("latex")) {
    dbg ("LaTeX picture fallback: " * latex_command * " not found");
    return array<tree>();
  }
  if (!exists_in_path ("gs")) {
    dbg ("LaTeX picture fallback: Ghostscript not found");
    return array<tree>();
  }
  // Keep the progress message before launching the external compiler.
  system_wait ("LaTeX: compiling document, ", "please wait");
  url wdir= url_temp ("_latex_picture_fallback");
  mkdir (wdir);
  bool dvips= false;
  install_picture_fallback_preamble (s, t, wdir, dvips);
  string document_root= as_string (head (get_file_focus ()));
  string cmdln= "cd " * document_root;
  cmdln << "; " << latex_command
        << " -interaction nonstopmode -halt-on-error -file-line-error "
        << " -output-directory \"" << as_string (wdir)
        << "\" \"" << as_string (wdir) << "/temp.tex\"";
  dbg ("LaTeX command: " * cmdln);
  if (system (cmdln)) {
    dbg ("Could not compile LaTeX document using " * latex_command);
    dbg ("Try to fallback on LaTeX");
    dvips= true;
    install_picture_fallback_preamble (s, t, wdir, dvips);
    cmdln= "cd " * document_root;
    cmdln << "; latex"
          << " -interaction nonstopmode -halt-on-error -file-line-error "
          << " -output-directory \"" << as_string (wdir)
          << "\" \"" << as_string (wdir) << "/temp.tex\"";
    dbg ("LaTeX command: " * cmdln);
    if (system (cmdln)) {
      dbg ("Could not compile LaTeX document");
      clean_picture_fallback_directory (wdir);
      return array<tree> ();
    }
  }
  int exp= N(search_picture_fallbacks (t));
  array<tree> r= load_picture_fallbacks (wdir, dvips, exp);
  if (N(r) != exp) {
    string msg;
    msg << "Warning: did not found the expected number of pictures:\n"
      << "         Got " << as_string (N(r)) << " whereas expected "
      << as_string (exp) << ".\n         LaTeX compilation or picture"
      << " importation might have failed";
    dbg (msg);
  }
  clean_picture_fallback_directory (wdir);
  return r;
}

tree
latex_fallback_on_pictures (string source, tree parsed) {
  if (N(search_picture_fallbacks (parsed)) == 0) return parsed;
  parsed= merge_environment_picture_fallbacks (parsed);
  array<tree> pictures= render_picture_fallbacks (source, parsed);
  int i= 0;
  return substitute_picture_fallbacks (parsed, pictures, i);
}
