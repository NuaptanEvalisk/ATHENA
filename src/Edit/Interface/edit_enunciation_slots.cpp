/******************************************************************************
* MODULE     : edit_enunciation_slots.cpp
* DESCRIPTION: Focus-local structured title slots; source remains in metadata
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* GNU general public license version 3 or later; see LICENSE.
******************************************************************************/
#include "edit_interface.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/enunciation_slots.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "Data/NodeContracts/native_fields.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "buffer_state.hpp"
#include "buffer_actor.hpp"
#include <nlohmann/json.hpp>

namespace {
namespace en= athena::enunciation;
bool slot_bounds (box b, const string& id, SI x, SI y, rectangle& result, path& route) {
  const tree ids= b->get_info ("locus-ids");
  if (is_func (ids, TUPLE))
    for (int i=0; i<N(ids); ++i)
      // A locus also wraps zero-width formatting markers. Only the visible
      // slot body supplies the editable rectangle.
      if (ids[i] == id && b->w () > 0 && b->h () > 0) {
        result= rectangle (x+b->x1, y+b->y1, x+b->x2, y+b->y2);
        route= path ();
        return true;
      }
  for (int i=0; i<b->subnr (); ++i) {
    path child;
    if (slot_bounds (b->subbox (i), id, x+b->sx (i), y+b->sy (i), result, child)) {
      route= path (i, child);
      return true;
    }
  }
  return false;
}

bool cached_slot_bounds (box b, path route, const string& id, rectangle& result) {
  SI x= 0, y= 0;
  for (; !is_nil (route); route= route->next) {
    const int i= route->item;
    if (i < 0 || i >= b->subnr ()) return false;
    x+= b->sx (i); y+= b->sy (i); b= b->subbox (i);
  }
  const tree ids= b->get_info ("locus-ids");
  if (is_func (ids, TUPLE) && b->w () > 0 && b->h () > 0)
    for (int i=0; i<N(ids); ++i)
      if (ids[i] == id) {
        result= rectangle (x+b->x1, y+b->y1, x+b->x2, y+b->y2);
        return true;
      }
  return false;
}

nlohmann::json slot_geometry (rectangle rect, double magf) {
  return {{"x", int (rect->x1 * magf / PIXEL)},
          {"y", int (-rect->y2 * magf / PIXEL)},
          {"width", int ((rect->x2-rect->x1) * magf / PIXEL)},
          {"height", int ((rect->y2-rect->y1) * magf / PIXEL)}};
}
}

void edit_interface_rep::refresh_enunciation_slots () {
  if (!(env_change & (THE_TREE | THE_CURSOR | THE_ENVIRONMENT))) return;
  path selected;
  if (buf && !buf->read_only)
    for (path p= path_up (tp); rp <= p; p= path_up (p)) {
      if (has_subtree (et, p) && en::editable_title (subtree (et, p))) {
        selected= p; break;
      }
      if (p == rp) break;
    }
  const auto id= is_nil (selected) ? std::string () :
    athena::node::id (subtree (et, selected));
  const bool changed= set_enunciation_editing_id (id);
  if (enunciation_slots_path_ == selected && !changed) return;
  const path previous= enunciation_slots_path_;
  enunciation_slots_path_= selected;
  if (!is_nil (previous) && has_subtree (et, previous))
    typeset_invalidate_presentation (previous);
  if (!is_nil (selected)) typeset_invalidate_presentation (selected);
  ++enunciation_slot_request_;
  enunciation_slot_field_= "";
  (void) publish_ui_text (actor_command_kind::ui_enunciation_slot, "");
}

bool edit_interface_rep::open_enunciation_slot (string field, int index,
                                               uint64_t expected_request) {
  if (expected_request && expected_request != enunciation_slot_request_) return false;
  if (field == "") {
    ++enunciation_slot_request_;
    enunciation_slot_field_= "";
    (void) publish_ui_text (actor_command_kind::ui_enunciation_slot, "");
    (void) publish_ui (actor_command_kind::ui_focus_view);
    return true;
  }
  apply_changes ();
  if (expected_request && expected_request != enunciation_slot_request_) return false;
  if (is_nil (enunciation_slots_path_) || !has_subtree (et, enunciation_slots_path_) ||
      !buf || buf->read_only) return false;
  const tree& source= subtree (et, enunciation_slots_path_);
  if (!en::editable_title (source)) return false;
  const std::string key (field.data (), N(field));
  const auto* fields= athena::contracts::native_fields (L(source));
  const auto* property= fields ? fields->property (key) : nullptr;
  if (!property || property->editor != athena::contracts::property_editor::title_slot)
    return false;
  auto values= en::title_fields (source, key);
  if (values.empty ()) values.emplace_back ("");
  if (index < 0 || index >= int (values.size ())) return false;
  rectangle rect;
  if (is_nil (eb) || !slot_bounds (eb,
      en::title_slot_id (athena::node::id (source), key, index), 0, 0, rect,
      enunciation_slot_box_path_)) return false;
  enunciation_slot_field_= field;
  enunciation_slot_index_= index;
  namespace xml= athena::document;
  tree carrier (TUPLE);
  athena::node::copy_metadata (source, carrier);
  std::vector<int> location;
  for (path p= enunciation_slots_path_ / rp; !is_nil (p); p= p->next)
    location.push_back (p->item);
  const string font= get_env_string (FONT);
  const string font_size= get_env_string (FONT_BASE_SIZE);
  nlohmann::json request {
    {"generation", ++enunciation_slot_request_},
    {"field", key}, {"index", index}, {"node", athena::node::id (source)},
    {"path", location},
    {"header", xml::write_xml_v3 (carrier, xml::xml_kind::fragment)},
    {"style", xml::write_xml_v3 (get_style (), xml::xml_kind::fragment)},
    {"font", std::string (font.data (), N(font))},
    {"font-size", std::string (font_size.data (), N(font_size))},
    {"zoom", zoomf}
  };
  const auto geometry= slot_geometry (rect, magf);
  request.update (geometry);
  enunciation_slot_geometry_= string (geometry.dump ().c_str ());
  const auto serialized= request.dump ();
  return publish_ui_text (actor_command_kind::ui_enunciation_slot,
    string (serialized.data (), serialized.size ()));
}

void edit_interface_rep::refresh_enunciation_slot_geometry () {
  if (enunciation_slot_field_ == "" || is_nil (eb) ||
      !(env_change & (THE_TREE | THE_ENVIRONMENT | THE_EXTENTS)) ||
      is_nil (enunciation_slots_path_) || !has_subtree (et, enunciation_slots_path_)) return;
  const auto id= athena::node::id (subtree (et, enunciation_slots_path_));
  const std::string field (enunciation_slot_field_.data (), N(enunciation_slot_field_));
  const string locus= en::title_slot_id (id, field, enunciation_slot_index_);
  rectangle rect;
  // A keystroke normally keeps the same box route. Search again only when
  // wrapping or another layout change moves the slot to a different branch.
  if (!cached_slot_bounds (eb, enunciation_slot_box_path_, locus, rect) &&
      !slot_bounds (eb, locus, 0, 0, rect, enunciation_slot_box_path_)) return;
  auto request= slot_geometry (rect, magf);
  const string geometry (request.dump ().c_str ());
  if (geometry == enunciation_slot_geometry_) return;
  enunciation_slot_geometry_= geometry;
  request["geometry"]= true;
  request["node"]= id;
  request["field"]= field;
  request["index"]= enunciation_slot_index_;
  const auto bytes= request.dump ();
  publish_ui_text (actor_command_kind::ui_enunciation_slot, string (bytes.data (), bytes.size ()));
}

bool edit_interface_rep::mouse_enunciation_slot (string kind, SI x, SI y) {
  if (kind == "release-left" && enunciation_slot_mouse_) {
    enunciation_slot_mouse_= false; return true;
  }
  if (kind != "press-left" || is_nil (enunciation_slots_path_) || is_nil (eb)) return false;
  list<string> ids;
  rectangles rectangles;
  eb->loci (x, y, 0, ids, rectangles);
  const string prefix= en::title_slot_prefix (
    athena::node::id (subtree (et, enunciation_slots_path_)));
  for (; !is_nil (ids); ids= ids->next) {
    const string id= ids->item;
    if (!starts (id, prefix)) continue;
    const auto tail= std::string (id.data ()+N(prefix), N(id)-N(prefix));
    const auto separator= tail.find (':');
    if (separator == std::string::npos) continue;
    try {
      const auto key= tail.substr (0, separator);
      if (open_enunciation_slot (string (key.c_str ()), std::stoi (tail.substr (separator+1)))) {
        enunciation_slot_mouse_= true; return true;
      }
    }
    catch (const std::exception&) {}
  }
  open_enunciation_slot ("", 0);
  return false;
}
