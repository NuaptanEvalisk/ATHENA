/******************************************************************************
* MODULE     : athena_ios.hpp
* DESCRIPTION: iPad application storage, scene and system menu integration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <string>
class QTMMainTabWindow;
class QWidget;
namespace ads { class CDockWidget; }

bool athena_ios_initialize_paths (std::string& error);
void athena_ios_install_application_bridge ();
void athena_ios_register_shell (QTMMainTabWindow* shell);
void athena_ios_menus_changed (QTMMainTabWindow* shell);
bool athena_ios_new_scene (QTMMainTabWindow* origin, ads::CDockWidget* pane= nullptr);
bool athena_ios_return_pane (QTMMainTabWindow* shell, QWidget* pane);
bool athena_ios_can_return_pane (QTMMainTabWindow* shell);
void athena_ios_close_scene (QTMMainTabWindow* shell);
