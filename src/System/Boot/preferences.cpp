
/******************************************************************************
* MODULE     : preferences.cpp
* DESCRIPTION: User preferences for TeXmacs
* COPYRIGHT  : (C) 2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "boot.hpp"
#include "basic.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "merge_sort.hpp"
#include "iterator.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

/******************************************************************************
* Changing the user preferences
******************************************************************************/

bool user_prefs_modified= false;
hashmap<string,string> user_prefs ("");
hashmap<string,string> user_prefs_default ("");
hashmap<string,bool> user_prefs_string_default (true);
hashmap<string,string> user_prefs_callback ("");
url user_prefs_file= "$ATHENA_HOME_PATH/system/preferences.json";
void notify_preference (string var);

enum builtin_default_kind {
  PREF_STATIC,
  PREF_PRINTING_COMMAND,
  PREF_PAPER_TYPE,
  PREF_GPG_EXECUTABLE
};

struct builtin_preference {
  const char* key;
  const char* def;
  bool string_def;
  const char* callback;
  builtin_default_kind kind;
};

static string
default_gpg_executable () {
  if (exists_in_path ("gpg")) return "gpg";
  if (exists_in_path ("gpg2")) return "gpg2";
  return "";
}

static string
default_paper_type () {
  string psize= get_env ("PAPERSIZE");
  if (psize != "") return psize;
  return "a4";
}

static string
builtin_default_value (const builtin_preference& pref) {
  switch (pref.kind) {
  case PREF_PRINTING_COMMAND:
    return get_printing_default ();
  case PREF_PAPER_TYPE:
    return default_paper_type ();
  case PREF_GPG_EXECUTABLE:
    return default_gpg_executable ();
  case PREF_STATIC:
  default:
    return pref.def;
  }
}

static void
ensure_builtin_user_preferences () {
  static bool done= false;
  if (done) return;
  done= true;

  static const builtin_preference prefs[]= {
#define PREF(k, d, cb) {k, d, true, cb, PREF_STATIC}
#define PREF_OBJ(k, d, cb) {k, d, false, cb, PREF_STATIC}
#define PREF_KIND(k, d, cb, kind) {k, d, true, cb, kind}
    PREF ("profile", "beginner", ""),
    PREF ("look and feel", "default", "notify-look-and-feel"),
    PREF ("case sensitive shortcuts", "default", ""),
    PREF ("new toolbar", "on", "notify-restart"),
    PREF ("disable texmacs window positioning", "off", ""),
    PREF ("default cjk language", "chinese", ""),
    PREF ("render solution in smaller font", "on",
          "notify-enunciation-rendering"),
    PREF ("number solutions", "on", "notify-enunciation-rendering"),
    PREF ("text toolbar", "off", "notify-toolbar-presentation"),
    PREF ("page medium", "paper", ""),
    PREF ("show full context", "on", ""),
    PREF ("show table cells", "on", ""),
    PREF ("show focus", "on", ""),
    PREF ("show only semantic focus", "on", ""),
    PREF ("semantic editing", "off", ""),
    PREF ("semantic selections", "on", ""),
    PREF ("semantic correctness", "off", ""),
    PREF ("remove superfluous invisible", "off", ""),
    PREF ("insert missing invisible", "off", ""),
    PREF ("zealous invisible correct", "off", ""),
    PREF ("homoglyph correct", "off", ""),
    PREF ("manual remove superfluous invisible", "on", ""),
    PREF ("manual insert missing invisible", "on", ""),
    PREF ("manual zealous invisible correct", "off", ""),
    PREF ("manual homoglyph correct", "on", ""),
    PREF ("security", "prompt on scripts", "notify-security"),
    PREF ("latex command", "pdflatex", "notify-latex-command"),
    PREF ("scripting language", "none", "notify-scripting-language"),
    PREF ("database tool", "off", "notify-tool"),
    PREF ("debugging tool", "off", "notify-tool"),
    PREF ("developer tool", "off", "notify-tool"),
    PREF ("linking tool", "off", "notify-tool"),
    PREF ("presentation tool", "off", "notify-tool"),
    PREF ("inertial scrolling", "off", ""),
    PREF ("inertial scrolling friction", "0.95", ""),
    PREF ("inertial scrolling sensitivity", "2.0", ""),
    PREF ("source tool", "off", "notify-tool"),
    PREF ("experimental alpha", "on", "notify-tool"),
    PREF ("new style fonts", "on", "notify-new-fonts"),
    PREF ("bitmap effects", "on", "notify-tool"),
    PREF ("new style page breaking", "on", "notify-new-page-breaking"),
    PREF ("open console on errors", "on", ""),
    PREF ("open console on warnings", "on", ""),
    PREF ("debug scheme backtraces", "off", "notify-debug-backtrace"),
    PREF ("debug show memory in status bar", "off",
          "notify-debug-memory-footer"),
    PREF ("debug channel auto", "off", ""),
    PREF ("debug channel verbose", "off", ""),
    PREF ("debug channel events", "off", ""),
    PREF ("debug channel std", "off", ""),
    PREF ("debug channel io", "off", ""),
    PREF ("debug channel gnutls", "off", ""),
    PREF ("debug channel bench", "off", ""),
    PREF ("debug channel history", "off", ""),
    PREF ("debug channel qt", "off", ""),
    PREF ("debug channel qt-widgets", "off", ""),
    PREF ("debug channel keyboard", "off", ""),
    PREF ("debug channel packrat", "off", ""),
    PREF ("debug channel flatten", "off", ""),
    PREF ("debug channel parser", "off", ""),
    PREF ("debug channel correct", "off", ""),
    PREF ("debug channel convert", "off", ""),
    PREF ("debug channel live", "off", ""),
    PREF ("gui:line-input:autocommit", "on", ""),
    PREF ("show font substitution warning", "on", ""),
    PREF ("check for updates", "on", ""),
    PREF ("use native menubar", "off", ""),
    PREF ("use unified toolbar", "off", ""),
    PREF ("hide toolbars when not using them", "off",
          "notify-toolbar-presentation"),
    PREF ("remember ads panes layout", "on", ""),
    PREF ("middle click closes ads tab", "on", ""),

    PREF ("header", "on", "notify-header"),
    PREF ("main icon bar", "on", "notify-icon-bar"),
    PREF ("mode dependent icons", "on", "notify-icon-bar"),
    PREF ("focus dependent icons", "on", "notify-icon-bar"),
    PREF ("user provided icons", "off", "notify-icon-bar"),
    PREF ("status bar", "on", "notify-status-bar"),
    PREF ("zoom factor", "1", "notify-zoom-factor"),
    PREF ("snap to pages", "off", ""),
    PREF ("persistent fit width", "off", ""),
    PREF ("typewriter mode", "off", ""),
    PREF ("ir-up", "home", "notify-remote-control"),
    PREF ("ir-down", "end", "notify-remote-control"),
    PREF ("ir-left", "pageup", "notify-remote-control"),
    PREF ("ir-right", "pagedown", "notify-remote-control"),
    PREF ("ir-center", "S-return", "notify-remote-control"),
    PREF ("ir-play", "F5", "notify-remote-control"),
    PREF ("ir-pause", "escape", "notify-remote-control"),
    PREF ("ir-menu", ".", "notify-remote-control"),
    PREF ("draw cursor", "on", ""),
    PREF ("blinking cursor", "on", ""),
    PREF ("rendering performance monitor", "off", ""),

    PREF ("native pdf", "on", ""),
    PREF ("native postscript", "on", ""),
    PREF ("texmacs->pdf:data-art cover", "off", ""),
    PREF ("texmacs->pdf:expand slides", "off", ""),
    PREF ("texmacs->pdf:check", "off", ""),
    PREF ("preview command", "default", "notify-preview-command"),
    PREF_KIND ("printing command", "lpr", "notify-printing-command",
               PREF_PRINTING_COMMAND),
    PREF_KIND ("paper type", "a4", "notify-paper-type", PREF_PAPER_TYPE),
    PREF ("printer dpi", "1200", "notify-printer-dpi"),

    PREF ("autosave", "120", "notify-autosave"),
    PREF ("autosave default", "on", ""),
    PREF ("custom keyboard", "", ""),
    PREF ("keyboard tool", "off", "notify-keyboard-tool"),
    PREF ("cyrillic input method", "none", "notify-cyrillic-input-method"),
    PREF ("source tree style", "angular", ""),
    PREF ("source tree special rendering", "normal", ""),
    PREF ("source tree compactification", "normal", ""),
    PREF ("source tree closing style", "compact", ""),

    PREF ("vault fuzzy search limit", "3", ""),
    PREF ("vault transclusion color", "#f8f8f8", ""),
    PREF ("recent text colors", "()", ""),
    PREF ("saved text colors", "()", ""),
    PREF ("gui cursor color", "red", "notify-cursor-color"),
    PREF ("gui selection color", "red", "notify-selection-color"),
    PREF ("gui focus color", "#0ff", "notify-focus-color"),
    PREF ("gui focus border width", "1", "notify-focus-border-width"),
    PREF ("locus-color", "#404080", "notify-link-color"),
    PREF ("visited-color", "#702070", "notify-link-color"),
    PREF ("enable radioactive links", "on", "notify-link-color"),
    PREF ("radioactive-link-color", "#a04400", "notify-link-color"),
    PREF ("override white document background", "off",
          "notify-document-background-color"),
    PREF ("white document background override color", "#f7f3e8",
          "notify-document-background-color"),
    PREF ("vault welcome page", "on", ""),
    PREF ("vault take preferences with vault", "off",
          "notify-vault-preferences-mode"),
    PREF ("vault auto load last", "off", ""),
    PREF ("vault report missing last", "off", ""),
    PREF ("vault explorer show on startup", "on", ""),
    PREF ("vault explorer track current file", "off",
          "notify-vault-explorer-track"),
    PREF ("vault explorer use system trash", "off", ""),
    PREF ("vault namespace explorer leaf matches only", "off", ""),
    PREF ("vault namespace explorer from root namespace", "off", ""),
    PREF ("vault namespace explorer simplify hierarchy", "off", ""),
    PREF ("vault simplify hierarchy graphs", "off", ""),
    PREF ("interactive elastic graphs", "on", ""),
    PREF ("fold table of contents in reflow", "on",
          "notify-fold-table-of-contents"),
    PREF ("vault preferred initial neighborhood", "namespace", ""),
    PREF ("vault max full backups", "Unlimited", ""),
    PREF ("vault pre-save history preservation", "1 week", ""),
    PREF ("vault maintenance anchor reader processes", "Unlimited", ""),
    PREF ("vault maintenance update table of contents", "off", ""),
    PREF ("vault maintenance continuous rag", "off", ""),
    PREF ("vault maintenance remove redundant block wikilinks", "off", ""),
    PREF ("delegation server", "", ""),
    PREF ("vault maintenance rag delegation fallback", "continue", ""),
    PREF ("vault collect orphan assets", "off", ""),
    PREF ("vault generate maintenance summary page", "off", ""),
    PREF ("vault maintenance summaries to keep", "All", ""),
    PREF ("vault subproduct consume string aggressively", "on", ""),
    PREF ("vault preferred font", "", ""),
    PREF ("vault labels mode", "visible", "notify-labels-mode"),
    PREF ("enunciation color preset", "Solarized Light", ""),
    PREF ("vault theorem color", "none", "notify-enunciation-color"),
    PREF ("vault lemma color", "none", "notify-enunciation-color"),
    PREF ("vault corollary color", "none", "notify-enunciation-color"),
    PREF ("vault proposition color", "none", "notify-enunciation-color"),
    PREF ("vault axiom color", "none", "notify-enunciation-color"),
    PREF ("vault definition color", "none", "notify-enunciation-color"),
    PREF ("vault notation color", "none", "notify-enunciation-color"),
    PREF ("vault convention color", "none", "notify-enunciation-color"),
    PREF ("vault conjecture color", "none", "notify-enunciation-color"),
    PREF ("vault law color", "none", "notify-enunciation-color"),
    PREF ("vault remark color", "none", "notify-enunciation-color"),
    PREF ("vault note color", "none", "notify-enunciation-color"),
    PREF ("vault example color", "none", "notify-enunciation-color"),
    PREF ("vault warning color", "none", "notify-enunciation-color"),
    PREF ("vault disambiguation color", "none", "notify-enunciation-color"),
    PREF ("vault acknowledgments color", "none", "notify-enunciation-color"),
    PREF ("vault exercise color", "none", "notify-enunciation-color"),
    PREF ("vault problem color", "none", "notify-enunciation-color"),
    PREF ("vault question color", "none", "notify-enunciation-color"),
    PREF ("vault solution color", "none", "notify-enunciation-color"),
    PREF ("vault answer color", "none", "notify-enunciation-color"),
    PREF ("vault proof color", "none", "notify-enunciation-color"),
    PREF ("vault proof alternative color", "none", "notify-enunciation-color"),
    PREF ("vault proof standard color", "none", "notify-enunciation-color"),
    PREF ("vault auto copy images to vault", "off", ""),
    PREF ("vault normalize image filename when inserting", "off", ""),
    PREF ("pasted internet image handling", "link", ""),
    PREF ("vault auto anchor enunciations on save", "off", ""),
    PREF ("vault auto approve anchor changes", "off", ""),

    PREF ("bidirectional navigation", "off",
          "notify-bidirectional-navigation"),
    PREF ("external navigation", "on", "notify-external-navigation"),
    PREF ("link pages", "on", "notify-link-pages"),
    PREF ("document update times", "1", "notify-doc-update-times"),
    PREF ("live spell checking", "off", "spell-live-notify"),
    PREF ("custom dictionary import language", "english", ""),
    PREF ("toolbar spell", "on", ""),
    PREF ("toolbar search", "on", ""),
    PREF ("toolbar replace", "on", ""),
    PREF ("allow-blank-match", "on", ""),
    PREF ("allow-initial-match", "on", ""),
    PREF ("allow-partial-match", "on", ""),
    PREF ("allow-injective-match", "on", ""),
    PREF ("allow-cascaded-match", "on", ""),
    PREF ("case-insensitive-match", "off", ""),
    PREF ("vault wikilink inserter case insensitive search", "off", ""),
    PREF ("vault transclusion inserter case insensitive search", "off", ""),
    PREF ("vault wikilink inserter fuzzy search", "off", ""),
    PREF ("vault transclusion inserter fuzzy search", "off", ""),
    PREF ("vault global search case insensitive search", "off", ""),
    PREF ("vault global search fuzzy search", "off", ""),
    PREF ("vault wikilink display template file", "%f", ""),
    PREF ("vault wikilink display template heading", "%c", ""),
    PREF ("vault wikilink display template anchor", "%c", ""),
    PREF ("materials provider crossref", "off", ""),
    PREF ("materials provider openalex", "off", ""),
    PREF ("materials provider open library", "on", ""),
    PREF ("materials provider google books", "off", ""),
    PREF ("materials provider arxiv", "off", ""),
    PREF ("materials provider pubmed", "off", ""),
    PREF ("materials provider contact email", "", ""),
    PREF ("materials local metadata extractor", "exiftool", ""),
    PREF ("materials local text extractor", "pdftotext", ""),
    PREF ("materials import parallelism", "auto", ""),
    PREF ("materials csl style", "springer-mathphys", ""),
    PREF ("allow-blank-replace", "off", ""),
    PREF ("allow-initial-replace", "off", ""),
    PREF ("allow-partial-replace", "off", ""),
    PREF ("allow-injective-replace", "off", ""),
    PREF ("auto bib import", "on", ""),
    PREF ("console details", "normal", "refresh-console"),
    PREF ("console size", "100", "refresh-console"),
    PREF ("manual style", "tmmanual", ""),
    PREF_OBJ ("doc:collect-timestamp", "0", "notify-doc-collect-preference"),
    PREF_OBJ ("doc:collect-languages", "()", "notify-doc-collect-preference"),
    PREF_OBJ ("gui:help-window-geometry", "(400 -400 400 300)",
              "notify-help-win-preference"),
    PREF_OBJ ("gui:help-window-viewing", "(\"\" \"\")",
              "notify-help-win-preference"),
    PREF_OBJ ("gui:help-window-visible", "#f",
              "notify-help-win-preference"),

    PREF ("text spacebar", "default", ""),
    PREF ("math spacebar", "default", ""),
    PREF ("automatic quotes", "default", "notify-quoting-style"),
    PREF ("automatic brackets", "mathematics",
          "notify-auto-close-brackets"),
    PREF ("use large brackets", "on", ""),
    PREF ("prog:automatic brackets", "off",
          "notify-prog-auto-close-brackets"),
    PREF ("prog:highlight brackets", "off", "notify-highlight-brackets"),
    PREF ("prog:select brackets", "off", "notify-select-brackets"),
    PREF_OBJ ("editor:verbatim:tabstop", "4", ""),
    PREF ("syntax:fortran:none", "black", "notify-fortran-pref"),
    PREF ("syntax:fortran:comment", "dark grey", "notify-fortran-pref"),
    PREF ("syntax:fortran:keyword", "dark magenta", "notify-fortran-pref"),
    PREF ("syntax:fortran:keyword_conditional", "dark magenta",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:keyword_control", "dark magenta",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:error", "dark red", "notify-fortran-pref"),
    PREF ("syntax:fortran:operator", "dark red", "notify-fortran-pref"),
    PREF ("syntax:fortran:operator_special", "dark red",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:operator_openclose", "dark red",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:operator_field", "dark red",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:preprocessor", "dark green",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:preprocessor_directive", "dark brown",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:declare_type", "#4040c0", "notify-fortran-pref"),
    PREF ("syntax:fortran:declare_function", "#4040c0",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:variable_function", "#0000c0",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:variable_type", "dark red",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:constant", "#4040c0", "notify-fortran-pref"),
    PREF ("syntax:fortran:constant_function", "#0000c0",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:constant_type", "#4040c0",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:constant_number", "#4040c0",
          "notify-fortran-pref"),
    PREF ("syntax:fortran:constant_string", "dark red",
          "notify-fortran-pref"),
    PREF ("syntax:scheme:none", "red", "notify-scheme-syntax"),
    PREF ("syntax:scheme:comment", "brown", "notify-scheme-syntax"),
    PREF ("syntax:scheme:keyword", "#309090", "notify-scheme-syntax"),
    PREF ("syntax:scheme:error", "dark red", "notify-scheme-syntax"),
    PREF ("syntax:scheme:constant_number", "#4040c0",
          "notify-scheme-syntax"),
    PREF ("syntax:scheme:constant_string", "dark grey",
          "notify-scheme-syntax"),
    PREF ("syntax:scheme:constant_char", "#333333",
          "notify-scheme-syntax"),
    PREF ("syntax:scheme:variable_identifier", "#204080",
          "notify-scheme-syntax"),
    PREF ("syntax:scheme:declare_category", "#d030d0",
          "notify-scheme-syntax"),
    PREF ("syntax:python:none", "red", "notify-python-syntax"),
    PREF ("syntax:python:comment", "brown", "notify-python-syntax"),
    PREF ("syntax:python:error", "dark red", "notify-python-syntax"),
    PREF ("syntax:python:constant", "#4040c0", "notify-python-syntax"),
    PREF ("syntax:python:constant_number", "#4040c0",
          "notify-python-syntax"),
    PREF ("syntax:python:constant_string", "dark grey",
          "notify-python-syntax"),
    PREF ("syntax:python:constant_char", "#333333",
          "notify-python-syntax"),
    PREF ("syntax:python:declare_function", "#0000c0",
          "notify-python-syntax"),
    PREF ("syntax:python:declare_type", "#0000c0",
          "notify-python-syntax"),
    PREF ("syntax:python:operator", "#8b008b", "notify-python-syntax"),
    PREF ("syntax:python:operator_openclose", "#B02020",
          "notify-python-syntax"),
    PREF ("syntax:python:operator_field", "#88888",
          "notify-python-syntax"),
    PREF ("syntax:python:operator_special", "orange",
          "notify-python-syntax"),
    PREF ("syntax:python:keyword", "#309090", "notify-python-syntax"),
    PREF ("syntax:python:keyword_conditional", "#309090",
          "notify-python-syntax"),
    PREF ("syntax:python:keyword_control", "#309090",
          "notify-python-syntax"),
    PREF ("syntax:cpp:none", "black", "notify-cpp-pref"),
    PREF ("syntax:cpp:comment", "dark grey", "notify-cpp-pref"),
    PREF ("syntax:cpp:keyword", "dark magenta", "notify-cpp-pref"),
    PREF ("syntax:cpp:error", "dark red", "notify-cpp-pref"),
    PREF ("syntax:cpp:preprocessor", "dark brown", "notify-cpp-pref"),
    PREF ("syntax:cpp:preprocessor_directive", "dark green",
          "notify-cpp-pref"),
    PREF ("syntax:cpp:constant_type", "#4040c0", "notify-cpp-pref"),
    PREF ("syntax:cpp:constant_number", "#4040c0", "notify-cpp-pref"),
    PREF ("syntax:cpp:constant_string", "dark red", "notify-cpp-pref"),
    PREF ("syntax:julia:none", "red", "notify-julia-syntax"),
    PREF ("syntax:julia:comment", "brown", "notify-julia-syntax"),
    PREF ("syntax:julia:error", "dark red", "notify-julia-syntax"),
    PREF ("syntax:julia:constant", "#4040c0", "notify-julia-syntax"),
    PREF ("syntax:julia:constant_number", "#4040c0",
          "notify-julia-syntax"),
    PREF ("syntax:julia:constant_string", "dark grey",
          "notify-julia-syntax"),
    PREF ("syntax:julia:constant_char", "#333333", "notify-julia-syntax"),
    PREF ("syntax:julia:declare_function", "#0000c0",
          "notify-julia-syntax"),
    PREF ("syntax:julia:declare_module", "0000c0",
          "notify-julia-syntax"),
    PREF ("syntax:julia:declare_type", "0000c0", "notify-julia-syntax"),
    PREF ("syntax:julia:operator", "#8b008b", "notify-julia-syntax"),
    PREF ("syntax:julia:operator_openclose", "#B02020",
          "notify-julia-syntax"),
    PREF ("syntax:julia:operator_field", "#88888",
          "notify-julia-syntax"),
    PREF ("syntax:julia:operator_special", "orange",
          "notify-julia-syntax"),
    PREF ("syntax:julia:keyword", "#309090", "notify-julia-syntax"),
    PREF ("syntax:julia:keyword_conditional", "#309090",
          "notify-julia-syntax"),
    PREF ("syntax:julia:keyword_control", "#309090",
          "notify-julia-syntax"),

    PREF ("codex home", "", ""),
    PREF ("codex completion remember choices", "off", ""),
    PREF ("codex completion model", "", ""),
    PREF ("codex completion effort", "", ""),
    PREF ("codex completion fast", "off", ""),
    PREF ("codex completion web search", "off", ""),
    PREF ("codex completion destination", "document", ""),
    PREF ("google tasks cloud todo list id", "", ""),
    PREF ("rag mcp port", "8765", ""),
    PREF ("rag embedding model", "", ""),
    PREF ("rag embedding device", "auto", ""),
    PREF ("rag delegation enabled", "off", ""),
    PREF ("artifact definition span delegation enabled", "off", ""),
    PREF ("rag mcp bearer token", "", ""),

    PREF ("w increase", "0.05", ""),
    PREF ("h increase", "0.05", ""),
    PREF ("em increase", "0.1", ""),
    PREF ("ex increase", "0.1", ""),
    PREF ("spc increase", "0.2", ""),
    PREF ("fn increase", "0.5", ""),
    PREF ("mm increase", "0.5", ""),
    PREF ("cm increase", "0.1", ""),
    PREF ("inch increase", "0.05", ""),
    PREF ("pt increase", "10", ""),
    PREF ("msec increase", "50", ""),
    PREF ("sec increase", "1", ""),
    PREF ("min increase", "0.1", ""),
    PREF ("% increase", "5", ""),
    PREF ("default unit", "ex", ""),

    PREF ("texmacs->latex:transparent-tracking", "on", ""),
    PREF ("texmacs->latex:source-tracking", "off", "converter-set-option"),
    PREF ("texmacs->latex:conservative", "on", "converter-set-option"),
    PREF ("texmacs->latex:transparent-source-tracking", "on",
          "converter-set-option"),
    PREF ("texmacs->latex:attach-tracking-info", "on",
          "converter-set-option"),
    PREF ("texmacs->latex:replace-style", "on", "converter-set-option"),
    PREF ("texmacs->latex:expand-macros", "on", "converter-set-option"),
    PREF ("texmacs->latex:expand-user-macros", "off",
          "converter-set-option"),
    PREF ("texmacs->latex:use-macros", "on", "converter-set-option"),
    PREF ("texmacs->latex:encoding", "utf-8", "converter-set-option"),
    PREF ("latex->texmacs:matrix-recognition", "on", ""),
    PREF ("latex->texmacs:aligned-to-eqnarray", "on", ""),
    PREF ("latex->texmacs:align-to-aligned", "on", ""),
    PREF ("latex->texmacs:operator-d-is-differential", "on", ""),
    PREF ("latex->texmacs:roman-d-is-differential", "on", ""),
    PREF ("latex->texmacs:text-d-is-differential", "on", ""),
    PREF ("latex->texmacs:parse-bbbk", "on", ""),
    PREF ("latex->texmacs:parse-bbbi-as-mathi", "on", ""),
    PREF ("latex->texmacs:text-operators", "on", ""),
    PREF ("latex->texmacs:intelligent-formula-cleaner", "off", ""),
    PREF ("latex->texmacs:intelligent-formula-cleaner-model",
          "$ATHENA_PATH/tools/formula-cleaner/formula-cleaner.gguf", ""),
    PREF ("texmacs->verbatim:wrap", "off", "converter-set-option"),
    PREF ("texmacs->verbatim:encoding", "auto", "converter-set-option"),
    PREF ("verbatim->texmacs:wrap", "on", "converter-set-option"),
    PREF ("verbatim->texmacs:encoding", "auto", "converter-set-option"),
    PREF ("texmacs->image:raster-resolution", "300", ""),
    PREF ("texmacs->image:format", "pdf", ""),
    PREF ("image auto remove background", "off", ""),
    PREF ("image->texmacs:svg-prefer-inkscape", "off",
          "converter-set-option"),
    PREF ("texmacs->html:css", "on", "converter-set-option"),
    PREF ("texmacs->html:mathjax", "off", "converter-set-option"),
    PREF ("texmacs->html:mathml", "off", "converter-set-option"),
    PREF ("texmacs->html:images", "on", "converter-set-option"),
    PREF ("texmacs->html:css-stylesheet", "---", "converter-set-option"),
    PREF ("mathml->texmacs:latex-annotations", "on", "converter-set-option"),
    PREF ("latex->texmacs:fallback-on-pictures", "off",
          "converter-set-option"),
    PREF_KIND ("gpg executable", "", "notify-gpg-executable",
               PREF_GPG_EXECUTABLE),
    PREF ("experimental encryption", "off",
          "gpg-notify-experimental-encryption"),
    PREF ("gpg cipher algorithm", "AES256",
          "notify-gpg-cipher-algorithm"),
    PREF ("gpg wallet key fingerprint", "",
          "notify-gpg-wallet-key-fingerprint"),
    PREF ("gpg default key fingerprint", "",
          "notify-gpg-default-key-fingerprint"),
    PREF ("wallet persistent status", "off",
          "notify-wallet-persistent-status"),
    PREF ("wallet always remember", "off",
          "notify-wallet-always-remember")
#undef PREF_KIND
#undef PREF_OBJ
#undef PREF
  };

  for (const builtin_preference& pref: prefs) {
    if (!user_prefs_default->contains (pref.key)) {
      user_prefs_default (pref.key)= builtin_default_value (pref);
      user_prefs_string_default (pref.key)= pref.string_def;
    }
    if (pref.callback != nullptr && string (pref.callback) != "")
      user_prefs_callback (pref.key)= pref.callback;
  }
}

static QString
to_qstring (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

struct debug_preference {
  const char* key;
  const char* channel;
};

static const debug_preference debug_preferences[]= {
  {"debug channel auto", "auto"},
  {"debug channel verbose", "verbose"},
  {"debug channel events", "events"},
  {"debug channel std", "std"},
  {"debug channel io", "io"},
  {"debug channel gnutls", "gnutls"},
  {"debug channel bench", "bench"},
  {"debug channel history", "history"},
  {"debug channel qt", "qt"},
  {"debug channel qt-widgets", "qt-widgets"},
  {"debug channel keyboard", "keyboard"},
  {"debug channel packrat", "packrat"},
  {"debug channel flatten", "flatten"},
  {"debug channel parser", "parser"},
  {"debug channel correct", "correct"},
  {"debug channel convert", "convert"},
  {"debug channel live", "live"}
};

static bool
apply_debug_preference (string key, string value) {
  for (const debug_preference& pref: debug_preferences)
    if (key == pref.key) {
      debug_set (pref.channel, value == "on");
      return true;
    }
  return false;
}

static void
apply_debug_preferences () {
  for (const debug_preference& pref: debug_preferences)
    debug_set (pref.channel,
      get_user_preference (pref.key, "off") == "on");
}

static string
from_qstring (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
}

static bool
has_suffix (string s, string suf) {
  return N(s) >= N(suf) && s (N(s) - N(suf), N(s)) == suf;
}

static url
with_json_suffix (url u) {
  string s= as_string (u);
  if (has_suffix (s, ".json")) return u;
  if (has_suffix (s, ".scm")) return url (s (0, N(s) - 4) * ".json");
  return url (s * ".json");
}

bool
has_user_preference (string var) {
  ensure_builtin_user_preferences ();
  return user_prefs->contains (var);
}

void
register_user_preference (string var, string def, bool string_def) {
  ensure_builtin_user_preferences ();
  if (!user_prefs_default->contains (var)) {
    user_prefs_default (var)= def;
    user_prefs_string_default (var)= string_def;
  }
}

void
register_user_preference_callback (string var, string callback) {
  ensure_builtin_user_preferences ();
  if (callback == "") user_prefs_callback->reset (var);
  else user_prefs_callback (var)= callback;
}

bool
user_preference_default_is_string (string var) {
  ensure_builtin_user_preferences ();
  if (user_prefs_string_default->contains (var))
    return user_prefs_string_default[var];
  return true;
}

string
get_user_preference_callback (string var) {
  ensure_builtin_user_preferences ();
  if (user_prefs_callback->contains (var)) return user_prefs_callback[var];
  return "";
}

array<string>
get_user_preference_names () {
  ensure_builtin_user_preferences ();
  iterator<string> it= iterate (user_prefs_default);
  array<string> a;
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  return a;
}

bool
user_preference_is_sensitive (string var) {
  string key= locase_all (var);
  if (key == "rag mcp bearer token" ||
      key == "google oauth client id" ||
      key == "google oauth client secret")
    return true;

  static const char* markers[]= {
    "access key", "api key", "apikey", "authentication",
    "authorization", "bearer", "client id", "client secret", "cookie",
    "credential", "oauth", "password", "passwd", "private key",
    "refresh token", "secret", "session key", "token"
  };
  for (const char* marker: markers)
    if (search_forwards (string (marker), key) >= 0) return true;
  return false;
}

array<string>
get_user_preference_callback_names () {
  ensure_builtin_user_preferences ();
  iterator<string> it= iterate (user_prefs_callback);
  array<string> a;
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  return a;
}

void
set_user_preference (string var, string val) {
  ensure_builtin_user_preferences ();
  if (val == "default") user_prefs->reset (var);
  else user_prefs (var)= val;
  user_prefs_modified= true;
  apply_debug_preference (var, get_user_preference (var, "off"));
  notify_preference (var);
}

void
reset_user_preference (string var) {
  ensure_builtin_user_preferences ();
  user_prefs->reset (var);
  user_prefs_modified= true;
  apply_debug_preference (var, get_user_preference (var, "off"));
  notify_preference (var);
}

string
get_user_preference (string var, string val) {
  ensure_builtin_user_preferences ();
  if (user_prefs->contains (var)) return user_prefs[var];
  if (user_prefs_default->contains (var)) return user_prefs_default[var];
  else return val;
}

/******************************************************************************
* Loading and saving user preferences
******************************************************************************/

static hashmap<string,string>
read_scheme_user_preferences (url prefs_file) {
  hashmap<string,string> prefs ("");
  string s;
  tree p (TUPLE);
  if (!load_string (prefs_file, s, false))
    p= block_to_scheme_tree (s);
  while (is_func (p, TUPLE, 1)) p= p[0];
  for (int i=0; i<N(p); i++)
    if (is_func (p[i], TUPLE, 2) &&
        is_atomic (p[i][0]) && is_atomic (p[i][1]) &&
        is_quoted (p[i][0]->label) && is_quoted (p[i][1]->label)) {
      string var= scm_unquote (p[i][0]->label);
      string val= scm_unquote (p[i][1]->label);
      prefs (var)= val;
    }
  return prefs;
}

static hashmap<string,string>
read_json_user_preferences (url prefs_file, bool& ok) {
  ok= false;
  hashmap<string,string> prefs ("");
  string s;
  if (load_string (prefs_file, s, false)) return prefs;

  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (as_charp (s), N(s)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    std_error << "Invalid preferences JSON in " << prefs_file << LF;
    return prefs;
  }

  QJsonObject root= doc.object ();
  if (root.value ("format").toString () != "athena-preferences" ||
      root.value ("version").toInt () != 1 ||
      !root.value ("preferences").isObject ()) {
    std_error << "Unsupported preferences JSON in " << prefs_file << LF;
    return prefs;
  }

  QJsonObject obj= root.value ("preferences").toObject ();
  for (QJsonObject::const_iterator it= obj.constBegin ();
       it != obj.constEnd (); ++it) {
    if (!it.value ().isString ()) continue;
    prefs (from_qstring (it.key ()))= from_qstring (it.value ().toString ());
  }

  ok= true;
  return prefs;
}

static void
write_scheme_user_preferences (url prefs_file) {
  iterator<string> it= iterate (user_prefs);
  array<string> a;
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  string s;
  for (int i=0; i<N(a); i++)
    s << "(" << scm_quote (a[i])
      << " " << scm_quote (user_prefs[a[i]]) << ")\n";
  if (save_string (prefs_file, s))
    std_warning << "The user preferences could not be saved\n";
}

static void
write_json_user_preferences (url prefs_file) {
  iterator<string> it= iterate (user_prefs);
  QJsonObject prefs;
  while (it->busy ()) {
    string key= it->next ();
    prefs.insert (to_qstring (key), to_qstring (user_prefs[key]));
  }

  QJsonObject root;
  root.insert ("format", "athena-preferences");
  root.insert ("version", 1);
  root.insert ("preferences", prefs);

  QJsonDocument doc (root);
  QByteArray bytes= doc.toJson (QJsonDocument::Indented);
  if (save_string (prefs_file, string (bytes.constData ())))
    std_warning << "The user preferences could not be saved\n";
}

static hashmap<string,string>
read_user_preferences (url prefs_file, url& canonical_file) {
  bool json_ok= false;
  if (has_suffix (as_string (prefs_file), ".json")) {
    bool json_exists= exists (prefs_file);
    hashmap<string,string> prefs= read_json_user_preferences (prefs_file,
                                                              json_ok);
    canonical_file= prefs_file;
    if (json_ok) return prefs;

    url legacy_file= url (as_string (prefs_file) (0,
                         N(as_string (prefs_file)) - 5) * ".scm");
    if (exists (legacy_file)) {
      if (json_exists)
        std_warning << "preferences: falling back to legacy preferences file "
                    << legacy_file << LF;
      else
        cout << "preferences: importing legacy preferences file "
             << legacy_file << LF;
      prefs= read_scheme_user_preferences (legacy_file);
      if (!json_exists) {
        user_prefs= prefs;
        write_json_user_preferences (prefs_file);
      }
      return prefs;
    }
    return prefs;
  }

  canonical_file= with_json_suffix (prefs_file);
  if (exists (canonical_file)) {
    hashmap<string,string> prefs= read_json_user_preferences (canonical_file,
                                                             json_ok);
    if (json_ok) return prefs;
    if (exists (prefs_file))
      return read_scheme_user_preferences (prefs_file);
    return prefs;
  }

  hashmap<string,string> prefs= read_scheme_user_preferences (prefs_file);
  user_prefs= prefs;
  write_json_user_preferences (canonical_file);
  return prefs;
}

static void
write_user_preferences (url prefs_file) {
  if (has_suffix (as_string (prefs_file), ".scm"))
    write_scheme_user_preferences (prefs_file);
  else
    write_json_user_preferences (prefs_file);
}

void
load_user_preferences () {
  load_user_preferences ("$ATHENA_HOME_PATH/system/preferences.json");
}

void
load_user_preferences (url prefs_file) {
  ensure_builtin_user_preferences ();
  save_user_preferences ();
  user_prefs= read_user_preferences (prefs_file, user_prefs_file);
  apply_debug_preferences ();
  user_prefs_modified= false;
}

void
dump_user_preferences (url prefs_file) {
  ensure_builtin_user_preferences ();
  write_user_preferences (prefs_file);
}

void
save_user_preferences () {
  ensure_builtin_user_preferences ();
  if (!user_prefs_modified) return;
  write_user_preferences (user_prefs_file);
  user_prefs_modified= false;
}
