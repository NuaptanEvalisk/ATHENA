/******************************************************************************
* MODULE     : native_shape_recognizer.hpp
* DESCRIPTION: Detached geometric stroke recognition for native drawing
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_NATIVE_SHAPE_RECOGNIZER_HPP
#define ATHENA_NATIVE_SHAPE_RECOGNIZER_HPP

#include "ATHENA/native_ink.hpp"

#include <cstddef>

native_shape_recognition_result recognize_native_shape (
  const native_ink_sample* samples, std::size_t count) noexcept;

#endif // defined ATHENA_NATIVE_SHAPE_RECOGNIZER_HPP
