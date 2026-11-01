#ifndef ATHENA_NATIVE_PROGRAM_EDIT_HPP
#define ATHENA_NATIVE_PROGRAM_EDIT_HPP

#include "editor.hpp"

void program_indent (bool backwards);
void program_indent_all (bool backwards);
void program_insert_tabstop ();
void program_remove_tabstop ();
void program_bracket (string left, string right, bool closing);
void program_select_enlarge ();
void program_cursor_moved (editor_rep *ed);
bool program_return (editor_rep *ed);
bool program_clipboard (editor_rep *ed, bool paste);

#endif
