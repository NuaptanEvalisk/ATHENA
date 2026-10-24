/******************************************************************************
* Native mixed-tree concatenations. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#include "conversion_concat.hpp"
#include <vector>
#include <stdexcept>

namespace {
void append (object value, std::vector<object>& output) {
  string text;
  bool atomic= is_string (value);
  if (atomic) text= as_string (value);
  else if (is_tree (value)) {
    tree native= as_tree (value);
    if (is_atomic (native)) { atomic= true; text= native->label; }
    else if (is_concat (native)) {
      for (int i=0; i<N(native); ++i) append (object (native[i]), output);
      return;
    }
  }
  else if (is_list (value) && !is_null (value) && is_symbol (car (value)) &&
           as_symbol (car (value)) == "concat") {
    for (object tail= cdr (value); !is_null (tail); tail= cdr (tail))
      append (car (tail), output);
    return;
  }
  if (atomic) {
    if (text == "") return;
    if (!output.empty () && is_string (output.back ()))
      output.back ()= object (as_string (output.back ()) * text);
    else output.emplace_back (text);
  }
  else output.push_back (value);
}
}

object conversion_concat_raw (object items) {
  if (!is_list (items)) throw std::runtime_error ("Concatenation requires a list");
  if (is_null (items)) return object (string (""));
  if (is_null (cdr (items))) return car (items);
  return cons (symbol_object ("concat"), items);
}

object conversion_concat_simplify (object items) {
  if (!is_list (items)) throw std::runtime_error ("Concatenation requires a list");
  std::vector<object> output;
  for (; !is_null (items); items= cdr (items)) append (car (items), output);
  object result= null_object ();
  for (auto i= output.rbegin (); i != output.rend (); ++i) result= cons (*i, result);
  return result;
}

object conversion_concat (object items) {
  return conversion_concat_raw (conversion_concat_simplify (items));
}
