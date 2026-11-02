// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "application_tools.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "native_interfaces.hpp"
#include "Data/Convert/Generic/conversion_executor.hpp"
#include "Data/String/utf8_edit.hpp"
#include "picture.hpp"
#include "font.hpp"
#include "file.hpp"
#include "boot.hpp"
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <optional>

namespace {
std::optional<string> statistics_text (tree document) {
  object result= conversion_dispatch (
    object (document), "texmacs-tree", "verbatim-snippet", conversion_options {});
  if (!is_string (result)) return std::nullopt;
  return as_string (result);
}
}

int tools_count_characters (tree document) {
  const auto result= statistics_text (document);
  if (!result) return 0;
  const string& text= *result;
  return utf8_byte_to_character (text, N(text));
}

int tools_count_words (tree document) {
  const auto result= statistics_text (document);
  if (!result) return 0;
  const string& text= *result;
  int count= 0;
  bool inside= false;
  for (int i=0; i<N(text); ++i) {
    bool space= text[i] == ' ' || text[i] == '\n' || text[i] == '\t';
    if (!space && !inside) ++count;
    inside= !space;
  }
  // The existing tokenizer returns one field for an empty string.
  return max (1, count);
}

int tools_count_lines (tree document) {
  const auto result= statistics_text (document);
  if (!result) return 0;
  const string& text= *result;
  int count= 1;
  for (int i=0; i<N(text); ++i) if (text[i] == '\n') ++count;
  return count;
}

tree tools_selection_or_document () {
  editor ed= get_current_editor ();
  return ed->selection_active_any ()? ed->selection_get (): ed->the_buffer ();
}

void tools_show_character_count () {
  athena_native_info_dialog ("Character count: " *
    as_string (tools_count_characters (tools_selection_or_document ())), "Statistics");
}
void tools_show_word_count () {
  athena_native_info_dialog ("Word count: " *
    as_string (tools_count_words (tools_selection_or_document ())), "Statistics");
}
void tools_show_line_count () {
  athena_native_info_dialog ("Line count: " *
    as_string (tools_count_lines (tools_selection_or_document ())), "Statistics");
}

void tools_toggle_save_aux () {
  editor ed= get_current_editor ();
  ed->init_env ("save-aux", ed->get_env_string ("save-aux") == "true"? "false": "true");
}
void tools_toggle_show_keyboard () { set_show_kbd (!get_show_kbd ()); }

void tools_clear_font_cache () {
  const char* files[]= {"font_cache.scm", "font_path_cache_v2.scm",
                       "font_file_index_v2.scm", "font-characteristics.json"};
  for (const char* file: files) remove (url ("$ATHENA_HOME_PATH/system/cache") * file);
}

void tools_scan_fonts () {
  system_wait ("Refreshing the system font catalog", "please wait...");
  font_database_build_local ();
}

void tools_clean_cache () {
  if (headless_mode) return;
  if (QMessageBox::question (QApplication::activeWindow (), "Clean system cache",
      "Are you sure you want to clean the system cache?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
  string directory= concretize (url ("$ATHENA_HOME_PATH/system/cache"));
  QDir cache (QString::fromUtf8 (directory.data (), N(directory)));
  if (cache.exists () && !cache.removeRecursively ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Clean system cache",
                         "Could not remove the system cache.");
    return;
  }
  athena_restart_message ();
}

void tools_picture_gc () {
  picture_cache_reset ();
  get_server ()->typeset_update_all ();
}
