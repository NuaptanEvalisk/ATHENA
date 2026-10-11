/* Debug-console streams extracted from tm_ostream.cpp.
 * Copyright (C) 2009-2013 David MICHEL, Joris van der Hoeven.
 * GPL-3.0-or-later.
 */
#include "tm_ostream.hpp"
#include "analyze.hpp"
#include "tree.hpp"

/******************************************************************************
* Streams for debugging purposes
******************************************************************************/

class debug_ostream_rep: public tm_ostream_rep {
public:
  string channel;
  string pending;

public:
  debug_ostream_rep (string channel);
  ~debug_ostream_rep ();

  bool is_writable () const;
  void write (const char* s, size_t n);
  void write (tree t);
  void flush ();
  void clear ();
};

debug_ostream_rep::debug_ostream_rep (string channel2):
  channel (channel2), pending ("") {}
debug_ostream_rep::~debug_ostream_rep () { flush (); }

bool
debug_ostream_rep::is_writable () const {
  return true;
}

void
debug_ostream_rep::clear () {
  pending= "";
  clear_debug_messages (channel);
}

void
debug_ostream_rep::write (const char* s, size_t n) {
  pending << string (s, n);
  while (occurs ("\n", pending)) {
    int pos= search_forwards ("\n", 0, pending);
    debug_message (channel, pending (0, pos+1));
    pending= pending (pos+1, N(pending));
  }
}

void
debug_ostream_rep::write (tree t) {
  flush ();
  debug_formatted (channel, t);
}

void
debug_ostream_rep::flush () {
  if (pending == "") return;
  debug_message (channel, pending);
  pending= "";
}

tm_ostream
debug_ostream (string channel) {
  return (tm_ostream_rep*) tm_new<debug_ostream_rep> (channel);
}

tm_ostream std_error       = debug_ostream ("std-error");
tm_ostream failed_error    = debug_ostream ("failed-error");
tm_ostream boot_error      = debug_ostream ("boot-error");
tm_ostream qt_error        = debug_ostream ("qt-error");
tm_ostream widkit_error    = debug_ostream ("widkit-error");
tm_ostream aqua_error      = debug_ostream ("aqua-error");
tm_ostream font_error      = debug_ostream ("font-error");
tm_ostream convert_error   = debug_ostream ("convert-error");
tm_ostream bibtex_error    = debug_ostream ("bibtex-error");
tm_ostream io_error        = debug_ostream ("io-error");

tm_ostream std_warning     = debug_ostream ("std-warning");
tm_ostream convert_warning = debug_ostream ("convert-warning");
tm_ostream typeset_warning = debug_ostream ("typeset-warning");
tm_ostream io_warning      = debug_ostream ("io-warning");
tm_ostream widkit_warning  = debug_ostream ("widkit-warning");
tm_ostream bibtex_warning  = debug_ostream ("bibtex-warning");

tm_ostream io_emergency     = debug_ostream ("io-emergency");
tm_ostream io_alert         = debug_ostream ("io-alert");
tm_ostream io_critical      = debug_ostream ("io-critical");
tm_ostream io_notice        = debug_ostream ("io-notice");
tm_ostream io_info          = debug_ostream ("io-info");

tm_ostream debug_std       = debug_ostream ("debug-std");
tm_ostream debug_qt        = debug_ostream ("debug-qt");
tm_ostream debug_aqua      = debug_ostream ("debug-aqua");
tm_ostream debug_widgets   = debug_ostream ("debug-widgets");
tm_ostream debug_fonts     = debug_ostream ("debug-fonts");
tm_ostream debug_convert   = debug_ostream ("debug-convert");
tm_ostream debug_typeset   = debug_ostream ("debug-typeset");
tm_ostream debug_edit      = debug_ostream ("debug-edit");
tm_ostream debug_packrat   = debug_ostream ("debug-packrat");
tm_ostream debug_history   = debug_ostream ("debug-history");
tm_ostream debug_keyboard  = debug_ostream ("debug-keyboard");
tm_ostream debug_automatic = debug_ostream ("debug-automatic");
tm_ostream debug_boot      = debug_ostream ("debug-boot");
tm_ostream debug_events    = debug_ostream ("debug-events");
tm_ostream debug_shell     = debug_ostream ("debug-shell");
tm_ostream debug_io        = debug_ostream ("debug-io");
tm_ostream debug_spell     = debug_ostream ("debug-spell");
tm_ostream debug_updater   = debug_ostream ("debug-updater");

tm_ostream std_bench       = debug_ostream ("std-bench");
