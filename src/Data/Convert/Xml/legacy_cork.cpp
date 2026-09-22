/******************************************************************************
* MODULE     : legacy_cork.cpp
* DESCRIPTION: Explicit legacy character decoding without approximate fallback
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "legacy_cork.hpp"
#include "unicode_text.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <unicode/utf8.h>
#include <algorithm>
#include <cstdint>

namespace athena::document {
legacy_text_error::legacy_text_error (std::size_t position, const std::string& message):
  std::runtime_error (message), byte (position) {}

namespace {
int hex (char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

std::string scalar (std::uint32_t cp) {
  if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
    throw std::invalid_argument ("Invalid Unicode scalar in legacy encoding");
  char buffer[4];
  int32_t length= 0;
  U8_APPEND_UNSAFE (buffer, length, cp);
  return {buffer, static_cast<std::size_t> (length)};
}

std::string escapes (std::string_view source, bool unicode) {
  std::string out;
  for (std::size_t i= 0; i < source.size ();) {
    if (source[i] != '#') { out += source[i++]; continue; }
    ++i;
    auto start= i;
    std::uint32_t cp= 0;
    while (i < source.size () && hex (source[i]) >= 0) {
      if (cp > 0x10ffff / 16) throw std::invalid_argument ("Oversized encoding escape");
      cp= cp * 16 + hex (source[i++]);
    }
    if (start == i) throw std::invalid_argument ("Empty encoding escape");
    if (unicode) out += scalar (cp);
    else {
      if (cp > 255) throw std::invalid_argument ("Non-byte Cork encoding key");
      out += static_cast<char> (cp);
    }
  }
  return out;
}

template<class F> void dictionary (const QDir& dir, const char* name, F accept) {
  QFile file (dir.filePath (QString::fromLatin1 (name)));
  if (!file.open (QIODevice::ReadOnly) || file.size () > 4 * 1024 * 1024)
    throw std::runtime_error (std::string ("Cannot read legacy encoding dictionary: ") + name);
  QJsonParseError error;
  auto doc= QJsonDocument::fromJson (file.readAll (), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject () ||
      doc.object ().value ("version").toInt () != 1 ||
      !doc.object ().value ("mappings").isArray ())
    throw std::runtime_error (std::string ("Invalid legacy encoding dictionary: ") + name);
  for (const auto& entry: doc.object ().value ("mappings").toArray ()) {
    if (!entry.isArray ()) throw std::runtime_error ("Invalid encoding entry");
    auto pair= entry.toArray ();
    if (pair.size () != 2 || !pair[0].isString () || !pair[1].isString ())
      throw std::runtime_error ("Invalid encoding pair");
    accept (pair[0].toString ().toStdString (), pair[1].toString ().toStdString ());
  }
}
} // namespace

legacy_cork_table::legacy_cork_table (const std::string& directory) {
  text::require_utf8 (directory);
  QDir dir (QString::fromUtf8 (directory.data (), directory.size ()));
  dictionary (dir, "corktounicode.json", [&] (const auto& k, const auto& v) {
    auto key= escapes (k, false), value= escapes (v, true);
    text::require_utf8 (value);
    if (key.empty () || value.empty ()) throw std::runtime_error ("Empty Cork mapping");
    if (key.size () == 1) bytes_[static_cast<unsigned char> (key[0])]= value;
    else sequences_[key]= value;
  });
  dictionary (dir, "tmuniversaltounicode.json", [&] (const auto& key, const auto& v) {
    // Bare byte aliases and ASCII typographic substitutions in this file are
    // export conveniences, not authoritative character identity mappings.
    if (key.size () < 3 || key.front () != '<' || key.back () != '>') return;
    auto value= escapes (v, true);
    text::require_utf8 (value);
    if (value.empty ()) throw std::runtime_error ("Empty symbol mapping");
    symbols_[key]= value;
  });
}

std::vector<legacy_text_piece> legacy_cork_table::decode (
  std::string_view source, legacy_text_role role, std::size_t limit,
  std::size_t piece_limit) const {
  std::vector<legacy_text_piece> result;
  std::size_t written= 0;
  auto append_piece= [&] (legacy_piece_kind kind, std::string value, std::size_t begin,
                  std::size_t end) {
    if (value.size () > limit - written)
      throw legacy_text_error (begin, "Legacy text exceeds output budget");
    const bool identity= kind == legacy_piece_kind::text &&
      value == source.substr (begin, end - begin) &&
      std::all_of (value.begin (), value.end (), [] (unsigned char c) { return c < 128; });
    written += value.size ();
    if (identity && !result.empty () && result.back ().byte_identity && result.back ().end == begin) {
      result.back ().value += value;
      result.back ().end= end;
      return;
    }
    if (result.size () >= piece_limit)
      throw legacy_text_error (begin, "Legacy text exceeds position budget");
    result.push_back ({kind, std::move (value), begin, end, identity});
  };
  for (std::size_t i= 0; i < source.size ();) {
    const auto begin= i;
    if (role != legacy_text_role::identifier && source[i] == '<') {
      const auto end= source.find ('>', i + 1);
      if (end == std::string_view::npos)
        throw legacy_text_error (i, "Unterminated legacy character token");
      std::string token (source.substr (i, end - i + 1));
      if (token.size () <= 2 || token.find ('<', 1) != std::string::npos)
        throw legacy_text_error (i, "Invalid legacy character token");
      if (token[1] == '#') {
        try {
          const auto digits= std::string_view (token).substr (2, token.size () - 3);
          if (digits.empty () || !std::all_of (digits.begin (), digits.end (),
                                             [] (char c) { return hex (c) >= 0; }))
            throw std::invalid_argument ("Invalid hex character token");
          append_piece (legacy_piece_kind::text, escapes ("#" + std::string (digits), true), i, end + 1);
        }
        catch (const std::invalid_argument& error) { throw legacy_text_error (i, error.what ()); }
      }
      else if (auto found= symbols_.find (token); found != symbols_.end ())
        append_piece (legacy_piece_kind::text, found->second, i, end + 1);
      else if (role == legacy_text_role::content) {
        const auto name= token.substr (1, token.size () - 2);
        // The name itself is identity, not another stream of symbol tokens.
        std::string identity= "texmacs:";
        for (const auto& piece: decode (name, legacy_text_role::identifier, limit, piece_limit))
          identity += piece.value;
        append_piece (legacy_piece_kind::named_symbol, std::move (identity), i, end + 1);
      }
      else throw legacy_text_error (i, "Named glyph has no scalar character equivalent");
      i= end + 1;
      continue;
    }
    std::size_t matched= 0;
    const std::string* replacement= nullptr;
    if (role == legacy_text_role::content)
      for (const auto& [key, value]: sequences_)
        if (key.size () > matched && source.substr (i, key.size ()) == key) {
          matched= key.size (); replacement= &value;
        }
    if (replacement) { append_piece (legacy_piece_kind::text, *replacement, i, i + matched); i += matched; continue; }
    const auto byte= static_cast<unsigned char> (source[i++]);
    if (role != legacy_text_role::content && byte < 128)
      append_piece (legacy_piece_kind::text, std::string (1, byte), begin, i);
    else if (!bytes_[byte].empty ()) append_piece (legacy_piece_kind::text, bytes_[byte], begin, i);
    else if (role == legacy_text_role::content) {
      const char* digits= "0123456789ABCDEF";
      std::string id= "cork:";
      id += digits[byte >> 4]; id += digits[byte & 15];
      append_piece (legacy_piece_kind::named_symbol, std::move (id), begin, i);
    }
    else throw legacy_text_error (begin, "Cork glyph cannot be represented in a scalar field");
  }
  return result;
}
} // namespace athena::document
