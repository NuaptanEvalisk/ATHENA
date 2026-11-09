// Native slide motion metadata. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"
#include "path.hpp"
#include "node_metadata.hpp"
#include <string>
#include <vector>

namespace athena::slides::motion {
inline constexpr auto transition_key= "athena:slide-transition";
inline constexpr auto frames_key= "athena:animation-frames";
inline constexpr auto effects_key= "athena:animations";

struct transition {
  std::string type= "none", direction= "left";
  int duration= 500;
};
struct event_frame {
  std::string id, name, trigger= "manual";
};
struct effect {
  std::string frame, category= "entrance", type= "appear", direction= "left";
  int duration= 500;
};
struct object {
  std::vector<int> source_path;
  std::vector<effect> effects;
};
struct page {
  transition change;
  std::vector<event_frame> frames;
  std::vector<object> objects;
};
page describe (const tree& slide, path source_path);

transition read_transition (const tree&);
std::vector<event_frame> read_frames (const tree&);
std::vector<effect> read_effects (const tree&);
void write_transition (node::metadata&, const transition&);
void write_frames (node::metadata&, const std::vector<event_frame>&);
void write_effects (node::metadata&, const std::vector<effect>&);
// Throws for malformed motion metadata. References are local to their slide.
void validate (const tree&);
void validate_page (const tree&);
void remove_frame (tree& detached_page, const std::string& id);
}
