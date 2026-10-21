// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Semantic display markers are produced only during an editor layout pass,
// never by HTML expansion or another consumer of enunciation macros.
class prooftrack_screen_scope {
  inline static thread_local bool enabled_= false;
  bool previous_;
public:
  explicit prooftrack_screen_scope (bool enabled): previous_ (enabled_) { enabled_= enabled; }
  ~prooftrack_screen_scope () { enabled_= previous_; }
  static bool active () { return enabled_; }
};
