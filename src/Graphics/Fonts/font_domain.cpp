/******************************************************************************
* MODULE     : font_domain.cpp
* DESCRIPTION: Explicit font ownership, binding, and ordered teardown
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "font_domain.hpp"
#include <atomic>
#include <mutex>

namespace {
thread_local font_domain* selected_domain= nullptr;
thread_local unsigned long binding_serial= 1;
std::mutex configuration_lock;
std::atomic<unsigned long> configuration_revision{1};
}

font_domain::font_domain ():
  configuration_revision_ (configuration_revision.load (std::memory_order_acquire)) {}

void
invalidate_font_configuration () {
  std::lock_guard<std::mutex> guard (configuration_lock);
  configuration_revision.store (
    configuration_revision.load (std::memory_order_relaxed) + 1,
    std::memory_order_release);
}

void
font_domain::synchronize_configuration () {
  unsigned long revision= configuration_revision.load (std::memory_order_acquire);
  if (configuration_revision_ == revision) return;
  check_owner ();
  // Existing boxes keep using the old resources until their owner shuts down.
  retired_slots_.push_back (std::move (slots_));
  slots_.clear ();
  configuration_revision_= revision;
  ++binding_serial;
}

font_domain&
current_font_domain () {
  if (selected_domain != nullptr) return *selected_domain;
  // Main-thread and standalone native clients have their own default owner.
  thread_local font_domain default_domain;
  return default_domain;
}

unsigned long
font_domain_binding_serial () { return binding_serial; }

font_domain_binding::font_domain_binding (font_domain& domain):
  previous_ (selected_domain) {
  domain.check_owner ();
  selected_domain= &domain;
  ++binding_serial;
}

font_domain_binding::~font_domain_binding () {
  selected_domain= previous_;
  ++binding_serial;
}

void
font_domain::check_owner () const {
  ASSERT (owner_ == std::this_thread::get_id (),
          "font resource accessed outside its owning domain thread");
}

font_domain::~font_domain () {
  check_owner ();
  font_domain_binding binding (*this);
  for (auto& head: resources_)
    while (head != nullptr) tm_delete (head);
  slots_.clear ();
  retired_slots_.clear ();
}

font_resource::font_resource (string name, int phase):
  owner_ (current_font_domain ()), phase_ (phase), res_name (name) {
  auto& head= owner_.resources_[phase_];
  next_= head;
  if (head != nullptr) head->previous_= this;
  head= this;
}

font_resource::~font_resource () {
  owner_.check_owner ();
  if (previous_ != nullptr) previous_->next_= next_;
  else owner_.resources_[phase_]= next_;
  if (next_ != nullptr) next_->previous_= previous_;
}
