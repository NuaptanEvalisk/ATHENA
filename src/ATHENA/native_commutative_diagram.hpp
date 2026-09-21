/******************************************************************************
* MODULE     : native_commutative_diagram.hpp
* DESCRIPTION: Transport-level commands for native commutative diagrams
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
*/

#ifndef ATHENA_NATIVE_COMMUTATIVE_DIAGRAM_HPP
#define ATHENA_NATIVE_COMMUTATIVE_DIAGRAM_HPP

#include <cstdint>

enum class native_cd_action: std::uint8_t {
  set_selected_option= 0,
  reverse_selected_arrow,
  flip_selected_arrow,
  flip_selected_label,
  trim,
  enlarge_horizontal,
  enlarge_vertical,
  delete_selected,
  clear_selection,
  navigate_left,
  navigate_right,
  navigate_up,
  navigate_down,
  edit_selected_label,
  insert_diagram
};

#endif
