/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "interop_document_source.hpp"

std::string interop_document_source_error (const tree& source) {
  if (!is_document (source)) return "Missing native document root";
  int bodies= 0;
  for (int i= 0; i < N (source); ++i) {
    if (is_compound (source[i], "body")) {
      if (N (source[i]) != 1) return "The document body field requires one child";
      ++bodies;
    }
  }
  if (bodies > 1) return "Duplicate document body fields";
  // Legacy empty documents may omit the body; newly created ones include it.
  return {};
}
