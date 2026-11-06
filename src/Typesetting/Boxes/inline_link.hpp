/******************************************************************************
* MODULE     : inline_link.hpp
* DESCRIPTION: Semantic links retained when transparent text boxes are shaped
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "Typesetting/boxes.hpp"
#include <memory>
#include <vector>

// Match results are committed by the owning document actor after a
// content-revision check. Live boxes read only immutable snapshots. Unlike
// inline_link_info, these ranges are not baked into line-breaking geometry.
struct live_radioactive_span {
  int child= 0, begin= 0, end= 0;
  string destination;
};
struct live_radioactive_snapshot {
  std::vector<live_radioactive_span> spans;
};
struct live_radioactive_decorations {
  std::shared_ptr<const live_radioactive_snapshot> current;
  std::shared_ptr<const live_radioactive_snapshot> read () const {
    return std::atomic_load_explicit (&current, std::memory_order_acquire);
  }
  void publish (std::shared_ptr<const live_radioactive_snapshot> next) {
    std::atomic_store_explicit (&current, std::move (next),
                                std::memory_order_release);
  }
};

struct inline_link_info {
  list<string> ids;
  string reference;
  string anchor;
  SI outline_pixel= 0;
  std::shared_ptr<live_radioactive_decorations> radioactive;
  int radioactive_child= -1;
  string radioactive_color;
};

// Only wrappers with unchanged geometry and transparent source navigation may
// opt in. Other modifiers remain boxes; a shaper must not infer their meaning.
class inline_link_source {
public:
  virtual ~inline_link_source () = default;
  virtual bool inline_link (box& body, inline_link_info& link) = 0;
};
