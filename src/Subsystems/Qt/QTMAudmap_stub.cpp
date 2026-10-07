/******************************************************************************
* MODULE     : QTMAudmap_stub.cpp
* DESCRIPTION: AUDMAP desktop-listener stubs for targets without a listener
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMAudmap.hpp"

void qt_audmap_start () {}
void qt_audmap_stop () {}
QString qt_audmap_discovery_file () { return {}; }
QTMPluginManager* qtm_plugin_manager () { return nullptr; }
