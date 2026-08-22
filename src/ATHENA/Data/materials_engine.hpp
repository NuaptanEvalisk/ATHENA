/******************************************************************************
* MODULE     : materials_engine.hpp
* DESCRIPTION: Hayagriva bridge for Materials import and CSL rendering
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_MATERIALS_ENGINE_HPP
#define ATHENA_MATERIALS_ENGINE_HPP

#include "ATHENA/Data/materials.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct MaterialCitationItem {
  std::string uuid;
  std::string locator_type;
  std::string locator_value;
  bool hidden= false;
};

struct MaterialCitationCluster {
  std::vector<MaterialCitationItem> items;
};

struct MaterialRenderedBibliographyItem {
  std::string uuid;
  std::string html;
};

struct MaterialRenderedDocument {
  std::vector<std::string> citation_html;
  std::vector<MaterialRenderedBibliographyItem> bibliography;
};

struct MaterialCslStyle {
  std::string name;
  std::string title;
};

bool athena_materials_list_csl_styles (
  std::vector<MaterialCslStyle>& styles, std::string& error);

bool athena_materials_import_bibtex (
  const std::filesystem::path& path, std::vector<MaterialRecord>& records,
  std::string& error);

bool athena_materials_render (
  const std::vector<MaterialRecord>& records,
  const std::vector<MaterialCitationCluster>& citations,
  const std::vector<std::string>& bibliography_only,
  const std::string& csl_style, MaterialRenderedDocument& rendered,
  std::string& error);

std::filesystem::path athena_materials_engine_path ();

#endif // ATHENA_MATERIALS_ENGINE_HPP
