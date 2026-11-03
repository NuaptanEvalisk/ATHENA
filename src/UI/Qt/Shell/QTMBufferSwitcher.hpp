/******************************************************************************
* MODULE     : QTMBufferSwitcher.hpp
* DESCRIPTION: Visual Studio style document buffer switcher
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMBUFFERSWITCHER_HPP
#define QTMBUFFERSWITCHER_HPP

#include "array.hpp"
#include "string.hpp"

class QWidget;

void buffer_switcher_note_widget (QWidget* widget);
void visual_buffer_switcher_show ();
string visual_buffer_switcher_choose (array<string> entries);

#endif // QTMBUFFERSWITCHER_HPP
