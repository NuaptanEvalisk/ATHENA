/******************************************************************************
* MODULE     : QTMCommutativeDiagramArrowPane.hpp
* DESCRIPTION: Native commutative-diagram arrow properties pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
*/

#ifndef QTMCOMMUTATIVEDIAGRAMARROWPANE_HPP
#define QTMCOMMUTATIVEDIAGRAMARROWPANE_HPP

#include "string.hpp"

class QTMWidget;

void commutative_diagram_arrow_pane_show (QTMWidget* canvas);
void commutative_diagram_arrow_pane_accept_state (
  QTMWidget* canvas, string encoded);

#endif // QTMCOMMUTATIVEDIAGRAMARROWPANE_HPP
