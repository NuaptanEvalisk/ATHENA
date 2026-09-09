/******************************************************************************
* MODULE     : buffer_name_catalog.hpp
* DESCRIPTION: Thread-safe publication of UI-owned buffer names and metadata
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef BUFFER_NAME_CATALOG_HPP
#define BUFFER_NAME_CATALOG_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The UI publishes on insert/remove/rename. Readers acquire one immutable
// generation, not aliases to TeXmacs' non-atomic reference-counted values.
class buffer_name_catalog {
public:
  using names= std::vector<std::string>;
  using snapshot= std::shared_ptr<const names>;
  struct metadata {
    std::string title;
    double last_visit= 0;
    bool modified= false;
  };
  using records= std::unordered_map<std::string, metadata>;

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

  // GUI-owned metadata is copied only on membership/title changes. Visits
  // update one scalar; no document or native reference-counted value is shared.
  void publish_metadata (records value) {
    std::lock_guard<std::mutex> guard (metadata_lock_);
    metadata_= std::move (value);
  }

  void visit (const std::string& name, double time) {
    std::lock_guard<std::mutex> guard (metadata_lock_);
    auto found= metadata_.find (name);
    if (found != metadata_.end ()) found->second.last_visit= time;
  }

  void set_modified (const std::string& name, bool modified) {
    std::lock_guard<std::mutex> guard (metadata_lock_);
    auto found= metadata_.find (name);
    if (found != metadata_.end ()) found->second.modified= modified;
  }

  bool lookup (const std::string& name, metadata& result) const {
    std::lock_guard<std::mutex> guard (metadata_lock_);
    auto found= metadata_.find (name);
    if (found == metadata_.end ()) return false;
    result= found->second;
    return true;
  }

private:
  snapshot names_;
  mutable std::mutex metadata_lock_;
  records metadata_;
};

#endif // BUFFER_NAME_CATALOG_HPP
