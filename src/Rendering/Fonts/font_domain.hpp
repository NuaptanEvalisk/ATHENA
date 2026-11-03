/******************************************************************************
* MODULE     : font_domain.hpp
* DESCRIPTION: Owner-local font resources and auxiliary caches
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef FONT_DOMAIN_HPP
#define FONT_DOMAIN_HPP

#include <array>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "resource.hpp"

class font_resource;

class font_domain {
  struct slot {
    virtual ~slot () = default;
  };
  template<class T> struct value_slot final: slot {
    T value;
    template<class... Args> explicit value_slot (Args&&... args):
      value (std::forward<Args> (args)...) {}
  };
  using slot_map= std::unordered_map<const void*, std::unique_ptr<slot>>;
  slot_map slots_;
  std::vector<slot_map> retired_slots_;
  unsigned long configuration_revision_;
  std::array<font_resource*, 4> resources_{};
  std::thread::id owner_= std::this_thread::get_id ();
  friend class font_resource;
  template<class T, class Tag> static const void* slot_key () {
    static const char key= 0;
    return &key;
  }

public:
  font_domain ();
  ~font_domain ();
  font_domain (const font_domain&) = delete;
  font_domain& operator= (const font_domain&) = delete;

  void check_owner () const;
  void synchronize_configuration ();
  template<class T, class Tag, class... Args> T& local (Args&&... args) {
    const void* key= slot_key<T, Tag> ();
    auto found= slots_.find (key);
    if (found == slots_.end ())
      found= slots_.emplace (key, std::make_unique<value_slot<T>> (
        std::forward<Args> (args)...)).first;
    return static_cast<value_slot<T>*> (found->second.get ())->value;
  }
};

font_domain& current_font_domain ();
unsigned long font_domain_binding_serial ();
void invalidate_font_configuration ();

class font_domain_binding {
  font_domain* previous_;
public:
  explicit font_domain_binding (font_domain& domain);
  ~font_domain_binding ();
  font_domain_binding (const font_domain_binding&) = delete;
  font_domain_binding& operator= (const font_domain_binding&) = delete;
};

// Cache the slot address, not the value. Switching domains invalidates these
// non-owning TLS hints, including when an old domain address is reused.
template<class T, class Tag= T, class... Args>
T& font_domain_local (Args&&... args) {
  static thread_local unsigned long serial= 0;
  static thread_local T* value= nullptr;
  font_domain& domain= current_font_domain ();
  if (serial != font_domain_binding_serial ()) {
    value= &domain.local<T, Tag> (std::forward<Args> (args)...);
    serial= font_domain_binding_serial ();
  }
  return *value;
}

// Phases destroy fonts/maps before metrics/glyphs, then TeX metrics, then faces.
// Auxiliary slots (including the FreeType library) outlive every resource.
class font_resource {
  font_domain& owner_;
  font_resource* previous_= nullptr;
  font_resource* next_= nullptr;
  int phase_;
  bool published_= false;
  friend class font_domain;
protected:
  font_resource (string name, int phase);
public:
  string res_name;
  virtual ~font_resource ();
  font_resource (const font_resource&) = delete;
  font_resource& operator= (const font_resource&) = delete;
  void check_owner () const { owner_.check_owner (); }
  bool publish_once () {
    check_owner ();
    if (published_) return false;
    published_= true;
    return true;
  }
};

template<class T> struct font_resource_registry {
  struct construction_tag {};
  hashmap<string,pointer>& map () const {
    return font_domain_local<hashmap<string,pointer>, T> (nullptr);
  }
  hashmap_rep<string,pointer>* operator-> () const { return map ().operator-> (); }
  pointer operator[] (string name) const { return map ()[name]; }
  pointer& operator() (string name) const { return map ()(name); }
  hashmap<string,pointer>& constructing () const {
    return font_domain_local<hashmap<string,pointer>, construction_tag> (nullptr);
  }
};

template<class R> inline R* font_resource_access (R* value) {
#ifndef NDEBUG
  if (value != nullptr) value->check_owner ();
#endif
  return value;
}

// Keep the legacy non-owning handle API, but do not publish from rep<T>'s
// constructor. The pointer constructor below runs after derived construction.
#define FONT_RESOURCE(PTR, PHASE) \
struct PTR##_rep; \
struct PTR: resource_ptr<PTR##_rep> { \
  static const font_resource_registry<PTR> instances; \
  PTR (PTR##_rep* value= nullptr); \
  PTR (string name) { rep= static_cast<PTR##_rep*> (instances[name]); } \
  PTR##_rep* operator-> () const { return font_resource_access (rep); } \
}; \
template<> struct rep<PTR>: font_resource { \
  explicit rep (string name): font_resource (name, PHASE) { \
    ASSERT (!PTR::instances.constructing ()->contains (name), \
            "recursive construction of the same font resource"); \
    PTR::instances.constructing ()(name)= this; \
  } \
  ~rep () override { \
    if (PTR::instances.constructing ()[res_name] == static_cast<pointer> (this)) \
      PTR::instances.constructing ()->reset (res_name); \
    if (PTR::instances[res_name] == static_cast<pointer> (this)) \
      PTR::instances->reset (res_name); \
  } \
}

#define FONT_RESOURCE_CODE(PTR) \
const font_resource_registry<PTR> PTR::instances{}; \
PTR::PTR (PTR##_rep* value) { \
  rep= value; \
  if (value != nullptr && value->publish_once ()) { \
    instances.constructing ()->reset (value->res_name); \
    instances (value->res_name)= static_cast<pointer> (value); \
  } \
}

#endif
