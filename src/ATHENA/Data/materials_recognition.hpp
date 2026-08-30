/******************************************************************************
* MODULE     : materials_recognition.hpp
* DESCRIPTION: Local extraction and provider-assisted Material recognition
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_MATERIALS_RECOGNITION_HPP
#define ATHENA_MATERIALS_RECOGNITION_HPP

#include "ATHENA/Data/materials.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct MaterialProviderOptions {
  bool crossref= false;
  bool openalex= false;
  bool open_library= false;
  bool google_books= false;
  bool arxiv= false;
  bool pubmed= false;
  std::string open_library_endpoint= "https://openlibrary.org/api/books";
  std::string contact_email;
  int timeout_ms= 15000;
  std::function<bool ()> cancelled;
};

struct MaterialRecognitionOptions {
  std::string metadata_extractor= "exiftool";
  std::string pdf_text_extractor= "pdftotext";
  std::string pdf_page_renderer= "pdftoppm";
  std::string ocr_engine= "tesseract";
  std::string ocr_languages= "eng";
  int pdf_pages= 8;
  int maximum_text_bytes= 512 * 1024;
  MaterialProviderOptions providers;
  std::function<void (const std::string&)> progress;
  std::function<bool ()> cancelled;
};

struct MaterialRecognitionResult {
  MaterialRecord material;
  std::vector<MaterialIdentifier> identifiers;
  std::vector<std::string> diagnostics;
  std::string extracted_text;
  std::string metadata_source;
  double confidence= 0.0;
  bool external_metadata_used= false;
};

bool athena_material_recognize_file (
  const std::filesystem::path& source,
  const MaterialRecognitionOptions& options,
  MaterialRecognitionResult& result, std::string& error);

#endif // ATHENA_MATERIALS_RECOGNITION_HPP
