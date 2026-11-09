// Native slide motion metadata. GPL-3.0-or-later.
#include "Document/Model/slide_motion.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace athena::slides::motion {
namespace {
using property= node::property;
using dict= property::dictionary;
const property* find (const tree& t, const char* key) {
  const auto* m= node::get (t);
  if (!m) return nullptr;
  auto it= m->properties.find (key);
  return it == m->properties.end () ? nullptr : &it->second;
}
template<class T> const T& as (const property& p) {
  const auto* v= std::get_if<T> (&p.data);
  if (!v) throw std::invalid_argument ("Invalid slide motion property type");
  return *v;
}
std::string text (const dict& d, const char* key) {
  auto it= d.find (key);
  if (it == d.end ()) throw std::invalid_argument (std::string ("Missing slide motion field: ") + key);
  return as<std::string> (it->second);
}
int duration (const dict& d) {
  auto it= d.find ("duration");
  if (it == d.end ()) throw std::invalid_argument ("Missing animation duration");
  auto ms= as<std::int64_t> (it->second);
  if (ms < 1 || ms > 600000) throw std::invalid_argument ("Animation duration must be 1..600000 ms");
  return int (ms);
}
void choice (const std::string& value, std::initializer_list<const char*> options) {
  if (std::none_of (options.begin (), options.end (), [&] (auto s) { return value == s; }))
    throw std::invalid_argument ("Unknown slide motion value: " + value);
}
void direction (const std::string& s) { choice (s, {"left", "right", "up", "down"}); }
void clean (tree& t, const std::string& id) {
  auto effects= read_effects (t);
  auto end= std::remove_if (effects.begin (), effects.end (), [&] (const auto& e) { return e.frame == id; });
  if (end != effects.end ()) {
    effects.erase (end, effects.end ());
    auto m= *node::get (t);
    write_effects (m, effects);
    node::set (t, m);
  }
  for (int i=0; i<N(t); ++i) clean (t[i], id);
}
}

transition read_transition (const tree& t) {
  auto* p= find (t, transition_key);
  if (!p) return {};
  const auto& d= as<dict> (*p);
  transition result {text (d, "type"), text (d, "direction"), duration (d)};
  choice (result.type, {"none", "push", "fade", "cube"});
  direction (result.direction);
  return result;
}
std::vector<event_frame> read_frames (const tree& t) {
  auto* p= find (t, frames_key);
  if (!p) return {};
  std::vector<event_frame> result;
  std::set<std::string> ids;
  for (const auto& v: as<property::list> (*p)) {
    const auto& d= as<dict> (v);
    event_frame f {text (d, "id"), text (d, "name"), text (d, "trigger")};
    if (!node::valid_id (f.id) || !ids.insert (f.id).second)
      throw std::invalid_argument ("Invalid or duplicate animation frame ID");
    choice (f.trigger, {"manual", "after", "with"});
    result.push_back (std::move (f));
  }
  return result;
}
std::vector<effect> read_effects (const tree& t) {
  auto* p= find (t, effects_key);
  if (!p) return {};
  std::vector<effect> result;
  std::set<std::string> ids;
  for (const auto& v: as<property::list> (*p)) {
    const auto& d= as<dict> (v);
    effect e {text (d, "frame"), text (d, "category"), text (d, "type"),
              text (d, "direction"), duration (d)};
    if (!node::valid_id (e.frame) || !ids.insert (e.frame).second)
      throw std::invalid_argument ("Each object may have only one effect per animation frame");
    choice (e.category, {"entrance", "emphasis", "exit"});
    if (e.category == "emphasis") choice (e.type, {"grow"});
    else choice (e.type, {"appear", "fade", "fly"});
    direction (e.direction);
    result.push_back (std::move (e));
  }
  return result;
}
void write_transition (node::metadata& m, const transition& t) {
  m.properties[transition_key]= property (dict {
    {"type", property (t.type)}, {"direction", property (t.direction)},
    {"duration", property (std::int64_t (t.duration))}});
}
void write_frames (node::metadata& m, const std::vector<event_frame>& frames) {
  property::list list;
  for (const auto& f: frames) list.emplace_back (dict {
    {"id", property (f.id)}, {"name", property (f.name)}, {"trigger", property (f.trigger)}});
  m.properties[frames_key]= property (std::move (list));
}
void write_effects (node::metadata& m, const std::vector<effect>& effects) {
  if (effects.empty ()) { m.properties.erase (effects_key); return; }
  property::list list;
  for (const auto& e: effects) list.emplace_back (dict {
    {"frame", property (e.frame)}, {"category", property (e.category)},
    {"type", property (e.type)}, {"direction", property (e.direction)},
    {"duration", property (std::int64_t (e.duration))}});
  m.properties[effects_key]= property (std::move (list));
}
void validate (const tree& t) {
  if (find (t, transition_key) || find (t, frames_key)) {
    if (L(t) != SLIDE_PAGE) throw std::invalid_argument ("Transitions and event frames belong to a slide page");
    (void) read_transition (t);
    (void) read_frames (t);
  }
  (void) read_effects (t);
}
void validate_page (const tree& page) {
  auto frames= read_frames (page);
  std::set<std::string> ids;
  for (const auto& f: frames) ids.insert (f.id);
  const auto visit= [&] (auto&& self, const tree& t) -> void {
    validate (t);
    for (const auto& e: read_effects (t))
      if (!ids.count (e.frame)) throw std::invalid_argument ("Animation refers to a deleted or foreign slide frame");
    for (int i=0; i<N(t); ++i) self (self, t[i]);
  };
  visit (visit, page);
}
void remove_frame (tree& page, const std::string& id) {
  auto frames= read_frames (page);
  frames.erase (std::remove_if (frames.begin (), frames.end (),
    [&] (const auto& f) { return f.id == id; }), frames.end ());
  auto m= *node::get (page);
  write_frames (m, frames);
  node::set (page, m);
  clean (page, id);
}
page describe (const tree& slide, path source_path) {
  validate_page (slide);
  page result {read_transition (slide), read_frames (slide), {}};
  std::vector<int> prefix;
  for (path p= source_path; !is_nil (p); p= p->next) prefix.push_back (p->item);
  const auto visit= [&] (auto&& self, const tree& t) -> void {
    auto effects= read_effects (t);
    if (!effects.empty ()) result.objects.push_back ({prefix, std::move (effects)});
    for (int i=0; i<N(t); ++i) {
      prefix.push_back (i); self (self, t[i]); prefix.pop_back ();
    }
  };
  visit (visit, slide);
  return result;
}
}
