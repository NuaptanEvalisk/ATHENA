/******************************************************************************
* MODULE     : fossil_delta.cpp
* DESCRIPTION: Fossil-compatible delta encoding/decoding for document history
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "fossil_delta.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace athena::history {
namespace {

constexpr char fossil_digits[]=
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~";

void
append_uint (std::string& out, std::uint32_t value) {
  char buffer[8];
  int count= 0;
  if (value == 0) {
    out.push_back ('0');
    return;
  }
  while (value != 0) {
    buffer[count++]= fossil_digits[value & 0x3fU];
    value >>= 6;
  }
  while (count != 0) out.push_back (buffer[--count]);
}

int
digit_value (unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  if (c == '_') return 36;
  if (c >= 'a' && c <= 'z') return c - 'a' + 37;
  if (c == '~') return 63;
  return -1;
}

bool
read_uint (std::string_view bytes, std::size_t& cursor, std::uint32_t& value) {
  if (cursor >= bytes.size ()) return false;
  std::uint64_t result= 0;
  std::size_t start= cursor;
  while (cursor < bytes.size ()) {
    int digit= digit_value (static_cast<unsigned char> (bytes[cursor]));
    if (digit < 0) break;
    result= (result << 6) + static_cast<unsigned> (digit);
    if (result > std::numeric_limits<std::uint32_t>::max ()) return false;
    ++cursor;
  }
  if (cursor == start) return false;
  value= static_cast<std::uint32_t> (result);
  return true;
}

std::uint32_t
checksum (std::string_view bytes) {
  std::uint32_t sum= 0;
  std::size_t i= 0;
  while (i + 4 <= bytes.size ()) {
    const auto* p= reinterpret_cast<const unsigned char*> (bytes.data () + i);
    sum += (static_cast<std::uint32_t> (p[0]) << 24) |
           (static_cast<std::uint32_t> (p[1]) << 16) |
           (static_cast<std::uint32_t> (p[2]) << 8) |
           static_cast<std::uint32_t> (p[3]);
    i += 4;
  }
  if (i < bytes.size ()) {
    std::uint32_t tail= 0;
    int shift= 24;
    while (i < bytes.size ()) {
      tail |= static_cast<std::uint32_t> (
        static_cast<unsigned char> (bytes[i++])) << shift;
      shift -= 8;
    }
    sum += tail;
  }
  return sum;
}

std::size_t
encoded_uint_size (std::size_t value) {
  std::size_t count= 1;
  while (value >= 64) {
    value >>= 6;
    ++count;
  }
  return count;
}

void
append_copy (std::string& out, std::size_t count, std::size_t offset) {
  append_uint (out, static_cast<std::uint32_t> (count));
  out.push_back ('@');
  append_uint (out, static_cast<std::uint32_t> (offset));
  out.push_back (',');
}

void
append_literal (std::string& out, std::string_view literal) {
  if (literal.empty ()) return;
  append_uint (out, static_cast<std::uint32_t> (literal.size ()));
  out.push_back (':');
  out.append (literal.data (), literal.size ());
}

bool
copy_is_smaller (std::size_t count, std::size_t offset) {
  return count > encoded_uint_size (count) + encoded_uint_size (offset) + 2;
}

} // namespace

std::string
fossil_delta_create (std::string_view source, std::string_view target) {
  if (source.size () > std::numeric_limits<std::uint32_t>::max () ||
      target.size () > std::numeric_limits<std::uint32_t>::max ())
    return {};

  std::size_t prefix= 0;
  while (prefix < source.size () && prefix < target.size () &&
         source[prefix] == target[prefix])
    ++prefix;

  std::size_t suffix= 0;
  while (suffix < source.size () - std::min (prefix, source.size ()) &&
         suffix < target.size () - std::min (prefix, target.size ()) &&
         source[source.size () - 1 - suffix] ==
           target[target.size () - 1 - suffix])
    ++suffix;

  bool use_prefix= prefix != 0 && copy_is_smaller (prefix, 0);
  bool use_suffix= suffix != 0 &&
    copy_is_smaller (suffix, source.size () - suffix);

  std::size_t literal_begin= use_prefix ? prefix : 0;
  std::size_t literal_end= use_suffix ? target.size () - suffix : target.size ();
  if (literal_end < literal_begin) {
    use_suffix= false;
    literal_end= target.size ();
  }

  std::string out;
  out.reserve (target.size () + 48);
  append_uint (out, static_cast<std::uint32_t> (target.size ()));
  out.push_back ('\n');
  if (use_prefix) append_copy (out, prefix, 0);
  append_literal (out, target.substr (literal_begin, literal_end - literal_begin));
  if (use_suffix)
    append_copy (out, suffix, source.size () - suffix);
  append_uint (out, checksum (target));
  out.push_back (';');
  return out;
}

std::optional<std::size_t>
fossil_delta_output_size (std::string_view delta) {
  std::size_t cursor= 0;
  std::uint32_t size= 0;
  if (!read_uint (delta, cursor, size) || cursor >= delta.size () ||
      delta[cursor] != '\n')
    return std::nullopt;
  return static_cast<std::size_t> (size);
}

std::optional<std::string>
fossil_delta_apply (std::string_view source, std::string_view delta) {
  std::size_t cursor= 0;
  std::uint32_t limit= 0;
  if (!read_uint (delta, cursor, limit) || cursor >= delta.size () ||
      delta[cursor++] != '\n')
    return std::nullopt;

  std::string out;
  out.reserve (limit);
  while (cursor < delta.size ()) {
    std::uint32_t count= 0;
    if (!read_uint (delta, cursor, count) || cursor >= delta.size ())
      return std::nullopt;
    char op= delta[cursor++];
    if (op == ';') {
      if (out.size () != limit || checksum (out) != count ||
          cursor != delta.size ())
        return std::nullopt;
      return out;
    }
    if (op == ':') {
      if (count > delta.size () - cursor || out.size () + count > limit)
        return std::nullopt;
      out.append (delta.data () + cursor, count);
      cursor += count;
      continue;
    }
    if (op == '@') {
      std::uint32_t offset= 0;
      if (!read_uint (delta, cursor, offset) || cursor >= delta.size () ||
          delta[cursor++] != ',')
        return std::nullopt;
      if (offset > source.size ()) return std::nullopt;
      std::size_t actual= count == 0 ? source.size () - offset : count;
      if (actual > source.size () - offset || out.size () + actual > limit)
        return std::nullopt;
      out.append (source.data () + offset, actual);
      continue;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

} // namespace athena::history
