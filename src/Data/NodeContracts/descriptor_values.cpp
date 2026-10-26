// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "descriptor_values.hpp"
#include <utility>

namespace athena::contracts {
namespace {
constexpr std::pair<std::string_view, int> types[] {
  {"regular", TYPE_REGULAR}, {"adhoc", TYPE_ADHOC}, {"raw", TYPE_RAW},
  {"variable", TYPE_VARIABLE}, {"argument", TYPE_ARGUMENT}, {"binding", TYPE_BINDING},
  {"boolean", TYPE_BOOLEAN}, {"integer", TYPE_INTEGER}, {"string", TYPE_STRING},
  {"length", TYPE_LENGTH}, {"numeric", TYPE_NUMERIC}, {"code", TYPE_CODE},
  {"identifier", TYPE_IDENTIFIER}, {"url", TYPE_URL}, {"color", TYPE_COLOR},
  {"graphical", TYPE_GRAPHICAL}, {"point", TYPE_POINT}, {"constraint", TYPE_CONSTRAINT},
  {"graphical-id", TYPE_GRAPHICAL_ID}, {"effect", TYPE_EFFECT}, {"duration", TYPE_DURATION},
  {"font-size", TYPE_FONT_SIZE}, {"obsolete", TYPE_OBSOLETE},
  {"unknown", TYPE_UNKNOWN}, {"error", TYPE_ERROR}
};
}
int type_code (std::string_view name) {
  for (const auto& entry: types) if (entry.first == name) return entry.second;
  return TYPE_INVALID;
}
std::string_view type_name (int code) {
  for (const auto& entry: types) if (entry.second == code) return entry.first;
  return "unknown";
}
}
