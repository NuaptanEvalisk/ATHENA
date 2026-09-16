/******************************************************************************
* MODULE     : QTMPluginUi.hpp
* DESCRIPTION: Native plugin preferences page and menu interfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
class QWidget;
class QMenu;
class QTMPluginManager;
QWidget* qtm_plugin_preferences (QTMPluginManager* manager, QWidget* parent = nullptr);
QMenu* qtm_plugins_menu (QWidget* parent);
QMenu* qtm_install_plugins_menu (QWidget* parent);
