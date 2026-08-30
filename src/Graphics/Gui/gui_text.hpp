
/******************************************************************************
* MODULE     : gui_text.hpp
* DESCRIPTION: Direct English text formatting for the user interface
* COPYRIGHT  : (C) 2026  The ATHENA developers
*******************************************************************************
*/

#ifndef GUI_TEXT_H
#define GUI_TEXT_H

#include "tree.hpp"

string ui_text (const string& s);
string ui_text (const char* s);
string ui_text (const tree& t);

#endif // defined GUI_TEXT_H
