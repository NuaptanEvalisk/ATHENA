/******************************************************************************
* MODULE     : pdf_merge.hpp
* DESCRIPTION: Merge native PDF exports while preserving link annotations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace athena::pdf {
struct destination {
  std::string label;
  unsigned long page; // Zero-based physical page in the member PDF.
  double x, y;       // PDF points, as written in an XYZ destination.
};
struct merge_member {
  std::string filename;
  std::vector<destination> destinations;
};

// Resolver returns the unchanged URI, #manifest-label, or empty (omit link).
// Inputs are native ATHENA exports. Throws on failure; caller publishes output.
void merge_documents (
  const std::vector<merge_member>& members, const std::string& output,
  const std::function<std::string(const std::string&)>& resolve_link,
  const std::function<bool()>& cancelled= {});
} // namespace athena::pdf
