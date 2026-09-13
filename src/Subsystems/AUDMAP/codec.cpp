/******************************************************************************
* MODULE     : codec.cpp
* DESCRIPTION: Bounded MessagePack encoding and validation for AUDMAP frames
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "codec.hpp"
#include <msgpack.hpp>
#include <cmath>
#include <stdexcept>

namespace athena::interop {
namespace {
struct buffer {
  std::string bytes;
  void write (const char* data, std::size_t count) {
    if (count > wire_size_limit - bytes.size ()) throw std::length_error ("Message too large");
    bytes.append (data, count);
  }
};

void pack (msgpack::packer<buffer>& p, const value& v, unsigned depth) {
  if (depth > 64) throw std::invalid_argument ("Message nesting exceeds 64");
  if (v.is_null ()) p.pack_nil ();
  else if (v.is_boolean ()) v.get<bool> () ? p.pack_true () : p.pack_false ();
  else if (v.is_number_unsigned ()) p.pack_uint64 (v.get<std::uint64_t> ());
  else if (v.is_number_integer ()) p.pack_int64 (v.get<std::int64_t> ());
  else if (v.is_number_float ()) {
    auto n = v.get<double> ();
    if (!std::isfinite (n)) throw std::invalid_argument ("Non-finite number");
    p.pack_double (n);
  }
  else if (v.is_string ()) {
    const auto& s = v.get_ref<const std::string&> ();
    if (s.size () > wire_size_limit) throw std::length_error ("String too large");
    p.pack_str (s.size ()); p.pack_str_body (s.data (), s.size ());
  }
  else if (v.is_binary ()) {
    const auto& b = v.get_binary ();
    if (b.size () > wire_size_limit) throw std::length_error ("Binary value too large");
    p.pack_bin (b.size ()); p.pack_bin_body (reinterpret_cast<const char*> (b.data ()), b.size ());
  }
  else if (v.is_array ()) {
    if (v.size () > 65536) throw std::length_error ("Array too large");
    p.pack_array (v.size ());
    for (const auto& item: v) pack (p, item, depth + 1);
  }
  else if (v.is_object ()) {
    if (v.size () > 65536) throw std::length_error ("Map too large");
    p.pack_map (v.size ());
    for (auto it = v.begin (); it != v.end (); ++it) {
      pack (p, it.key (), depth + 1); pack (p, it.value (), depth + 1);
    }
  }
  else throw std::invalid_argument ("Unsupported MessagePack value");
}

value unpack (const msgpack::object& o) {
  using namespace msgpack::type;
  switch (o.type) {
  case NIL: return nullptr;
  case BOOLEAN: return o.via.boolean;
  case POSITIVE_INTEGER: return o.via.u64;
  case NEGATIVE_INTEGER: return o.via.i64;
  case FLOAT32: case FLOAT64:
    if (!std::isfinite (o.via.f64)) throw std::invalid_argument ("Non-finite number");
    return o.via.f64;
  case STR: {
    std::string s (o.via.str.ptr, o.via.str.size);
    // dump validates UTF-8; binary strings must use MessagePack BIN instead.
    value v = std::move (s); v.dump (); return v;
  }
  case BIN: {
    const auto* first = reinterpret_cast<const std::uint8_t*> (o.via.bin.ptr);
    return value::binary (std::vector<std::uint8_t> (first, first + o.via.bin.size));
  }
  case ARRAY: {
    value out = value::array ();
    for (unsigned i = 0; i < o.via.array.size; ++i) out.push_back (unpack (o.via.array.ptr[i]));
    return out;
  }
  case MAP: {
    value out = value::object ();
    for (unsigned i = 0; i < o.via.map.size; ++i) {
      const auto& item = o.via.map.ptr[i];
      if (item.key.type != STR) throw std::invalid_argument ("Semantic map keys must be strings");
      const auto key = unpack (item.key).get<std::string> ();
      if (out.contains (key)) throw std::invalid_argument ("Duplicate map key");
      out[key] = unpack (item.val);
    }
    return out;
  }
  default: throw std::invalid_argument ("MessagePack extensions are not allowed");
  }
}
} // namespace

std::string encode_message (const value& message) {
  buffer b;
  msgpack::packer<buffer> p (b);
  pack (p, message, 0);
  return std::move (b.bytes);
}

value decode_message (std::string_view bytes) {
  if (bytes.empty () || bytes.size () > wire_size_limit)
    throw std::length_error ("Invalid message size");
  std::size_t consumed = 0;
  const msgpack::unpack_limit limit (65536, 65536, wire_size_limit, wire_size_limit, 0, 64);
  auto decoded = msgpack::unpack (bytes.data (), bytes.size (), consumed,
                                  nullptr, nullptr, limit);
  if (consumed != bytes.size ()) throw std::invalid_argument ("Trailing MessagePack data");
  value out = unpack (decoded.get ());
  if (!out.is_array () || out.empty () || !out[0].is_number_unsigned ())
    throw std::invalid_argument ("Message must be a positional array with numeric opcode");
  if (out[0].get<std::uint64_t> () > 255)
    throw std::invalid_argument ("Opcode exceeds protocol range");
  return out;
}
} // namespace athena::interop
