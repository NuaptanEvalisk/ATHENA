/******************************************************************************
* MODULE     : node_metadata.cpp
* DESCRIPTION: Validated owner-local node metadata and explicit identity copies
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "node_metadata.hpp"
#include "unicode_text.hpp"
#include <QUuid>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace athena::node {
namespace {
template<class... F> struct visitor: F... { using F::operator()...; };
template<class... F> visitor(F...) -> visitor<F...>;

struct validation {
  std::size_t nodes= 0, bytes= 0;
  std::set<const tree_rep*> active;
  void enter (std::size_t depth) {
    if (depth > 256 || ++nodes > 1000000)
      throw std::length_error ("Node metadata exceeds its structural budget");
  }
  void text (const std::string& s) {
    if (s.size () > 64 * 1024 * 1024 - bytes)
      throw std::length_error ("Node metadata exceeds its text budget");
    bytes+= s.size ();
    if (!athena::text::valid_utf8 (s))
      throw std::invalid_argument ("Node metadata text must be UTF-8");
  }
  void properties (const property::dictionary& a, std::size_t depth) {
    for (const auto& item: a) {
      if (item.first.empty ())
        throw std::invalid_argument ("Node property name must not be empty");
      text (item.first);
      value (item.second, depth);
    }
  }
  void attributes (const metadata& m, std::size_t depth) {
    if (!m.id.empty () && !valid_id (m.id))
      throw std::invalid_argument ("Node identity must be a canonical UUID");
    text (m.id);
    properties (m.properties, depth);
  }
  void document (const tree& t, std::size_t depth) {
    enter (depth);
    if (is_generic (t) || L(t) == UNINIT || L(t) == RAW_DATA)
      throw std::invalid_argument ("Structured metadata must be text, not opaque data");
    if (!active.insert (inside (t)).second)
      throw std::invalid_argument ("Cyclic structured node metadata");
    if (const auto* m= get (t)) attributes (*m, depth + 1);
    if (is_atomic (t)) text (std::string (t->label.data (), N(t->label)));
    else {
      const string tag= as_string (L(t));
      text (std::string (tag.data (), N(tag)));
      for (int i=0; i<N(t); ++i) document (t[i], depth + 1);
    }
    active.erase (inside (t));
  }
  void value (const property& p, std::size_t depth) {
    enter (depth);
    std::visit (visitor {
      [&] (const std::string& s) { text (s); },
      [] (bool) {}, [] (std::int64_t) {},
      [] (double n) {
        if (!std::isfinite (n))
          throw std::invalid_argument ("Node numeric property must be finite");
      },
      [&] (const property::list& a) {
        for (const auto& item: a) value (item, depth + 1);
      },
      [&] (const property::dictionary& a) { properties (a, depth + 1); },
      [&] (const reference& r) {
        if (!valid_id (r.id))
          throw std::invalid_argument ("Node reference must contain a canonical UUID");
        text (r.id);
      },
      [&] (const rich_text& r) { document (r.content, depth + 1); }
    }, p.data);
  }
};

metadata clone (const metadata& source) {
  metadata result;
  result.id= source.id;
  for (const auto& item: source.properties)
    result.properties.emplace (item.first, copy_property (item.second));
  return result;
}

bool equal_property_content (const property& a, const property& b);
bool equal_properties (const property::dictionary& a,
                       const property::dictionary& b, bool content= false) {
  if (a.size () != b.size ()) return false;
  auto j= b.begin ();
  for (auto i= a.begin (); i != a.end (); ++i, ++j) {
    if (i->first != j->first) return false;
    if (!(content ? equal_property_content (i->second, j->second) :
                    equal (i->second, j->second))) return false;
  }
  return true;
}

bool equal_property_content (const property& a, const property& b) {
  if (a.data.index () != b.data.index ()) return false;
  return std::visit ([&] (const auto& value) {
    using T= std::decay_t<decltype (value)>;
    const auto& other= std::get<T> (b.data);
    if constexpr (std::is_same_v<T, rich_text>)
      return content_equal (value.content, other.content);
    else if constexpr (std::is_same_v<T, property::dictionary>)
      return equal_properties (value, other, true);
    else if constexpr (std::is_same_v<T, property::list>) {
      if (value.size () != other.size ()) return false;
      for (std::size_t i=0; i<value.size (); ++i)
        if (!equal_property_content (value[i], other[i])) return false;
      return true;
    }
    else return equal (a, b);
  }, a.data);
}

std::uint32_t mix (std::uint32_t a, std::uint32_t b) {
  return (a ^ b) * 16777619U;
}
std::uint32_t hash_text (const std::string& s) {
  std::uint32_t h= 2166136261U;
  for (unsigned char c: s) h= mix (h, c);
  return h;
}
std::uint32_t hash_property (const property& p) {
  auto value= std::visit (visitor {
    [] (const std::string& s) { return hash_text (s); },
    [] (bool b) { return std::uint32_t (b); },
    [] (std::int64_t n) {
      auto bits= static_cast<std::uint64_t> (n);
      return mix (std::uint32_t (bits), std::uint32_t (bits >> 32));
    },
    [] (double n) { return std::uint32_t (std::hash<double> {} (n)); },
    [] (const property::list& a) {
      std::uint32_t h= 0;
      for (const auto& item: a) h= mix (h, hash_property (item));
      return h;
    },
    [] (const property::dictionary& a) {
      std::uint32_t h= 0;
      for (const auto& item: a)
        h= mix (mix (h, hash_text (item.first)), hash_property (item.second));
      return h;
    },
    [] (const reference& r) { return hash_text (r.id); },
    [] (const rich_text& r) { return std::uint32_t (::hash (r.content)); }
  }, p.data);
  return mix (std::uint32_t (p.data.index ()), value);
}

template<class F> void walk (tree& t, F& fn);
template<class F> void walk_property (property& p, F& fn) {
  if (auto* a= std::get_if<property::list> (&p.data))
    for (auto& item: *a) walk_property (item, fn);
  else if (auto* a= std::get_if<property::dictionary> (&p.data))
    for (auto& item: *a) walk_property (item.second, fn);
  else if (auto* r= std::get_if<rich_text> (&p.data)) walk (r->content, fn);
}
template<class F> void walk (tree& t, F& fn) {
  fn (t);
  if (auto* m= inside (t)->attributes)
    for (auto& item: m->properties) walk_property (item.second, fn);
  if (is_compound (t))
    for (int i=0; i<N(t); ++i) walk (t[i], fn);
}
void remap_property (property& p, const identity_map& replacements) {
  if (auto* r= std::get_if<reference> (&p.data)) {
    auto found= replacements.find (r->id);
    if (found != replacements.end ()) r->id= found->second;
  }
  else if (auto* a= std::get_if<property::list> (&p.data))
    for (auto& item: *a) remap_property (item, replacements);
  else if (auto* a= std::get_if<property::dictionary> (&p.data))
    for (auto& item: *a) remap_property (item.second, replacements);
}
} // namespace

bool valid_id (const std::string& value) {
  if (value.size () != 36) return false;
  bool nonzero= false;
  for (std::size_t i=0; i<value.size (); ++i) {
    const char c= value[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') return false;
    }
    else {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
      nonzero|= c != '0';
    }
  }
  return nonzero;
}

std::string new_id () {
  return QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ();
}
const metadata* get (const tree& node) { return inside (node)->attributes; }
std::string id (const tree& node) {
  const metadata* m= get (node);
  return m == nullptr ? std::string () : m->id;
}
void set (tree& node, const metadata& value) {
  if (is_generic (node) || L(node) == UNINIT)
    throw std::invalid_argument ("Cannot annotate an opaque or uninitialized tree");
  validation checked;
  checked.attributes (value, 0);
  auto replacement= value.empty () ? std::unique_ptr<metadata> () :
    std::make_unique<metadata> (clone (value));
  delete inside (node)->attributes;
  inside (node)->attributes= replacement.release ();
}
void clear (tree& node) {
  delete inside (node)->attributes;
  inside (node)->attributes= nullptr;
}
void copy_metadata (const tree& source, tree& target) {
  if (strong_equal (source, target)) return;
  const auto* m= get (source);
  auto replacement= m == nullptr ? std::unique_ptr<metadata> () :
    std::make_unique<metadata> (clone (*m));
  delete inside (target)->attributes;
  inside (target)->attributes= replacement.release ();
}
property copy_property (const property& p) {
  return std::visit (visitor {
    [] (const property::list& a) {
      property::list out;
      out.reserve (a.size ());
      for (const auto& item: a) out.push_back (copy_property (item));
      return property (std::move (out));
    },
    [] (const property::dictionary& a) {
      property::dictionary out;
      for (const auto& item: a) out.emplace (item.first, copy_property (item.second));
      return property (std::move (out));
    },
    [] (const rich_text& r) { return property (rich_text {copy (r.content)}); },
    [] (const auto& value) { return property (value); }
  }, p.data);
}
bool equal (const property& a, const property& b) {
  if (a.data.index () != b.data.index ()) return false;
  return std::visit ([&] (const auto& value) {
    using T= std::decay_t<decltype (value)>;
    const auto& other= std::get<T> (b.data);
    if constexpr (std::is_same_v<T, reference>) return value.id == other.id;
    else if constexpr (std::is_same_v<T, rich_text>) return value.content == other.content;
    else if constexpr (std::is_same_v<T, property::dictionary>)
      return equal_properties (value, other);
    else if constexpr (std::is_same_v<T, property::list>) {
      if (value.size () != other.size ()) return false;
      for (std::size_t i=0; i<value.size (); ++i)
        if (!equal (value[i], other[i])) return false;
      return true;
    }
    else return value == other;
  }, a.data);
}
bool equal_metadata (const tree& a, const tree& b) {
  const auto* x= get (a);
  const auto* y= get (b);
  if (x == y) return true;
  if (x == nullptr || y == nullptr) return false;
  return x->id == y->id && equal_properties (x->properties, y->properties);
}
int hash_metadata (const tree& node) {
  const auto* m= get (node);
  if (m == nullptr) return 0;
  std::uint32_t h= hash_text (m->id);
  for (const auto& item: m->properties)
    h= mix (mix (h, hash_text (item.first)), hash_property (item.second));
  return static_cast<int> (h);
}
bool content_equal (const tree& a, const tree& b) {
  if (strong_equal (a, b)) return true;
  if (L(a) != L(b)) return false;
  const auto* x= get (a);
  const auto* y= get (b);
  static const property::dictionary empty;
  if (!equal_properties (x ? x->properties : empty, y ? y->properties : empty, true))
    return false;
  if (is_atomic (a)) return a->label == b->label;
  if (N(a) != N(b)) return false;
  for (int i=0; i<N(a); ++i) if (!content_equal (a[i], b[i])) return false;
  return true;
}
bool contains_metadata (const tree& source) {
  if (get (source)) return true;
  if (is_compound (source))
    for (int i=0; i<N(source); ++i)
      if (contains_metadata (source[i])) return true;
  return false;
}

tree duplicate (const tree& source, identity_map* result) {
  tree target= copy (source);
  identity_map replacements;
  auto allocate= [&] (tree& t) {
    auto* m= inside (t)->attributes;
    if (m == nullptr || m->id.empty ()) return;
    const std::string fresh= new_id ();
    if (!replacements.emplace (m->id, fresh).second)
      throw std::invalid_argument ("Cannot duplicate a source with conflicting identities");
    m->id= fresh;
  };
  walk (target, allocate);
  auto remap= [&] (tree& t) {
    if (auto* m= inside (t)->attributes)
      for (auto& item: m->properties) remap_property (item.second, replacements);
  };
  walk (target, remap);
  if (result != nullptr) *result= std::move (replacements);
  return target;
}
tree content_projection (const tree& source) {
  tree result= copy (source);
  auto strip= [] (tree& t) {
    if (auto* m= inside (t)->attributes) {
      m->id.clear ();
      if (m->empty ()) clear (t);
    }
  };
  walk (result, strip);
  return result;
}
} // namespace athena::node
