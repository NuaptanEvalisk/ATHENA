
/******************************************************************************
* MODULE     : impl_language.hpp
* COPYRIGHT  : (C) 1999-2020  Joris van der Hoeven, Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef IMPL_LANGUAGE_H
#define IMPL_LANGUAGE_H

#include "language.hpp"
extern text_property_rep tp_normal_rep;
extern text_property_rep tp_hyph_rep;
extern text_property_rep tp_thin_space_rep;
extern text_property_rep tp_space_rep;
extern text_property_rep tp_space_before_rep;
extern text_property_rep tp_dspace_rep;
extern text_property_rep tp_nb_thin_space_rep;
extern text_property_rep tp_nb_space_rep;
extern text_property_rep tp_nb_dspace_rep;
extern text_property_rep tp_period_rep;
extern text_property_rep tp_cjk_normal_rep;
extern text_property_rep tp_cjk_no_break_rep;
extern text_property_rep tp_cjk_period_rep;
extern text_property_rep tp_cjk_wide_period_rep;
extern text_property_rep tp_cjk_no_break_period_rep;
extern text_property_rep tp_half_rep;
extern text_property_rep tp_operator_rep;
extern text_property_rep tp_short_apply_rep;
extern text_property_rep tp_apply_rep;

struct verb_language_rep: language_rep {
  verb_language_rep (string name);
  text_property advance (tree t, int& pos) override;
  array<int> get_hyphens (string s) override;
  void hyphenate (string s, int after, string& left, string& right) override;
  string get_color (tree t, int start, int end) override;
};

#endif // defined IMPL_LANGUAGE_H
