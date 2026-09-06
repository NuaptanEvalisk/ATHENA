/******************************************************************************
* MODULE     : buffer_name_catalog.hpp
* DESCRIPTION: Immutable, byte-only publication of UI-owned buffer names
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef BUFFER_NAME_CATALOG_HPP
#define BUFFER_NAME_CATALOG_HPP

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The UI publishes on insert/remove/rename. Readers acquire one immutable
// generation, not aliases to TeXmacs' non-atomic reference-counted values.
class buffer_name_catalog {
public:
  using names= std::vector<std::string>;
  using snapshot= std::shared_ptr<const names>;

  buffer_name_catalog (): names_ (std::make_shared<const names> ()) {}
  buffer_name_catalog (const buffer_name_catalog&)= delete;
  buffer_name_catalog& operator = (const buffer_name_catalog&)= delete;

  void publish (names value) {
    snapshot next= std::make_shared<const names> (std::move (value));
    std::atomic_store_explicit (&names_, std::move (next),
                                std::memory_order_release);
  }

  snapshot read () const {
    return std::atomic_load_explicit (&names_, std::memory_order_acquire);
  }

private:
  snapshot names_;
};

#endif // BUFFER_NAME_CATALOG_HPP
