#pragma once
#include "tree.hpp"
#include "Document/Model/slide_page.hpp"
#include "Domains/Knowledge/enunciation_model.hpp"
#include <string>
#include <functional>

class editor_rep;
namespace athena::enunciation {
struct slot_style { const char* label; const char* foreground; const char* background; };
inline slot_style title_slot_style (const std::string& field) {
  if (field == "title") return {"Title", "#8733a8", "#f4e9fa"};
  if (field == "subtitle") return {"Subtitle", "#136f65", "#e4f3ee"};
  if (field == "author") return {"Author", "#285e9c", "#e7effa"};
  if (field == "date") return {"Date", "#136f65", "#e4f3ee"};
  if (field == "name") return {"Name", "#8733a8", "#f4e9fa"};
  if (field == "year") return {"Year", "#136f65", "#e4f3ee"};
  return {"Attribution", "#285e9c", "#e7effa"};
}
inline bool editable_fields (const tree& t) {
  return editable_title (t) || !athena::slides::kind (t).empty ();
}
inline std::vector<std::string> field_order (const tree& t) {
  return {"name", "year", "attribution"};
}
inline string title_slot_prefix (const std::string& node) {
  return "athena-title-slot:" * string (node.c_str ()) * ":";
}
inline string title_slot_id (const std::string& node, const std::string& field, int index) {
  return title_slot_prefix (node) * string (field.c_str ()) * ":" * as_string (index);
}
// Called on the input BufferActor, not on the GUI thread.
bool title_input_key (editor_rep&, const string& key);
bool title_input_active (editor_rep&);
void title_input_layout (editor_rep&, const std::function<int (tree)>& measure);
void title_input_changed (editor_rep&);
}
