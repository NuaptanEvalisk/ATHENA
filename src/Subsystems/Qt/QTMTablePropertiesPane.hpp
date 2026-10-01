/******************************************************************************
* MODULE     : QTMTablePropertiesPane.hpp
* DESCRIPTION: Native Qt table and cell property panes
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
*/

#ifndef QTMTABLEPROPERTIESPANE_HPP
#define QTMTABLEPROPERTIESPANE_HPP

#include "url.hpp"

void cell_properties_pane_show ();
void table_properties_pane_show ();
void cell_properties_pane_show_for (url target);
void table_properties_pane_show_for (url target);

#endif // QTMTABLEPROPERTIESPANE_HPP
