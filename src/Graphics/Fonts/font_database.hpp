/******************************************************************************
* MODULE     : font_database.hpp
* DESCRIPTION: Shared Unicode-native view of ATHENA's single font database
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "font_source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace athena::text {

struct font_database_coverage_page {
  std::uint32_t first= 0;
  std::array<std::uint32_t, 8> bits {};
};

struct font_database_face {
  font_file_source file;
  std::vector<std::string> families;
  std::string style;
  int weight= 0;
  int width= 0;
  int slant= 0;
  int spacing= 0;
  bool scalable= true;
  bool color= false;
  std::vector<font_database_coverage_page> coverage;
};

struct font_database_page_shortlist {
  std::array<std::uint32_t, 257> offsets {};
  std::vector<std::uint32_t> faces;
};

struct font_database_view {
  std::uint64_t generation= 0;
  std::vector<font_database_face> faces;
  std::unordered_map<std::string, std::vector<std::size_t>> families;
  std::unordered_map<std::uint32_t, std::vector<std::size_t>> unicode_pages;
  // Only pages whose candidate set would exceed the hot-path bound get an
  // exact-scalar shortlist. Small pages are already bounded by unicode_pages.
  std::unordered_map<std::uint32_t, font_database_page_shortlist>
    unicode_shortlists;
  std::unordered_map<std::string, std::size_t> physical_faces;
  std::unordered_map<std::string, std::string> generic_families;
};

std::shared_ptr<const font_database_view> font_database_native_view ();
std::string font_database_family_key (std::string_view family);
std::string font_database_face_key (const font_file_source& source);
std::string font_database_physical_key (const font_file_source& source);
bool font_database_supports (const font_database_face& face, char32_t scalar);
std::optional<font_file_source> font_database_match_family (
  std::string_view family, int weight= 400, int slant= 0,
  int width= 100, int spacing= 0);
std::optional<font_file_source> font_database_match_style (
  std::string_view family, std::string_view style);

} // namespace athena::text
