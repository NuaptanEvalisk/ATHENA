/******************************************************************************
* MODULE     : handwriting_recognizer.hpp
* DESCRIPTION: Hand TeX single-symbol handwriting recognition
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_HANDWRITING_RECOGNIZER_HPP
#define ATHENA_HANDWRITING_RECOGNIZER_HPP

#include <memory>
#include <string>
#include <vector>

struct athena_handwriting_point {
  float x;
  float y;
};

using athena_handwriting_stroke= std::vector<athena_handwriting_point>;

struct athena_handwriting_prediction {
  std::string key;
  std::string command;
  float confidence;
};

class athena_handwriting_recognizer {
public:
  explicit athena_handwriting_recognizer (const std::string& asset_directory);
  ~athena_handwriting_recognizer ();

  athena_handwriting_recognizer (const athena_handwriting_recognizer&)= delete;
  athena_handwriting_recognizer& operator= (
    const athena_handwriting_recognizer&)= delete;

  bool available (std::string& error);
  std::vector<athena_handwriting_prediction> recognize (
    const std::vector<athena_handwriting_stroke>& strokes,
    float viewport_width, float viewport_height, int maximum_results,
    std::string& error);

  static std::vector<float> preprocess (
    const std::vector<athena_handwriting_stroke>& strokes,
    float viewport_width, float viewport_height);

private:
  struct implementation;
  std::unique_ptr<implementation> impl;
};

#endif // ATHENA_HANDWRITING_RECOGNIZER_HPP
