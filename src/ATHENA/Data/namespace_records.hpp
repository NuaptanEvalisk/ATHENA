/******************************************************************************
* MODULE     : namespace_records.hpp
* DESCRIPTION: Immutable namespace payloads with reader-local record indices
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_NAMESPACE_RECORDS_HPP
#define ATHENA_NAMESPACE_RECORDS_HPP

#include <algorithm>
#include <memory>
#include <numeric>
#include <vector>

// Payloads are immutable. Only record indices are copied or reordered by readers.
template<typename T> class namespace_records {
  std::shared_ptr<const std::vector<T>> storage;
  std::vector<size_t> indices;
public:
  namespace_records () = default;
  explicit namespace_records (std::vector<T> values):
    storage (std::make_shared<const std::vector<T>> (std::move (values))),
    indices (storage->size ()) {
    std::iota (indices.begin (), indices.end (), 0);
  }
  explicit namespace_records (std::shared_ptr<const std::vector<T>> values):
    storage (std::move (values)), indices (storage->size ()) {
    std::iota (indices.begin (), indices.end (), 0);
  }
  namespace_records (std::shared_ptr<const std::vector<T>> values,
                     std::vector<size_t> selected):
    storage (std::move (values)), indices (std::move (selected)) {}
  size_t size () const { return indices.size (); }
  bool empty () const { return indices.empty (); }
  const T& operator[] (size_t i) const { return (*storage)[indices[i]]; }
  std::shared_ptr<const T> retain (size_t i) const {
    return std::shared_ptr<const T> (storage, &(*this)[i]);
  }
  template<typename Compare> void stable_sort (Compare compare) {
    std::stable_sort (indices.begin (), indices.end (),
      [&] (size_t a, size_t b) { return compare ((*storage)[a], (*storage)[b]); });
  }
  void reorder (const std::vector<size_t>& order) {
    std::vector<size_t> next;
    next.reserve (order.size ());
    for (size_t i: order) next.push_back (indices.at (i));
    indices= std::move (next);
  }
  class iterator {
    const namespace_records* records;
    size_t index;
  public:
    iterator (const namespace_records* owner, size_t i): records (owner), index (i) {}
    const T& operator* () const { return (*records)[index]; }
    iterator& operator++ () { ++index; return *this; }
    bool operator!= (const iterator& other) const { return index != other.index; }
  };
  iterator begin () const { return iterator (this, 0); }
  iterator end () const { return iterator (this, size ()); }
};

#endif
