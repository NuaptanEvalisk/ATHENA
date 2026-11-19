// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "QTMVaultBadge.hpp"
#include "fuzzy_rank.hpp"
#include <QBitArray>
#include <unicode/utf8.h>
#include <unicode/utf16.h>

inline void vault_rank_highlight (QListWidgetItem* item, string query,
                                  string text, array<fuzzy_rank_field> fields) {
  fuzzy_rank_hits hits;
  fuzzy_rank (query, fields, &hits);
  if (N(hits.bytes) == 0) return;

  // The winning field is either the displayed path or its filename suffix.
  // Matcher offsets are UTF-8 bytes; Qt formats address UTF-16 code units.
  const int offset= N(text) - N(fields[hits.field].text);
  QBitArray marked (N(text));
  for (int i=0; i<N(hits.bytes); ++i) marked.setBit (offset + hits.bytes[i]);
  QList<QTextLayout::FormatRange> formats;
  QTextCharFormat bold;
  bold.setFontWeight (QFont::Bold);
  for (int byte=0, pos=0; byte<N(text);) {
    const int start= byte;
    UChar32 cp;
    U8_NEXT (text.data (), byte, N(text), cp);
    const int length= U16_LENGTH (cp);
    for (int i=start; i<byte; ++i)
      if (marked.testBit (i)) {
        if (!formats.isEmpty () && formats.last ().start + formats.last ().length == pos)
          formats.last ().length+= length;
        else formats.append ({pos, length, bold});
        break;
      }
    pos+= length;
  }
  item->setData (VaultHighlightRole, QVariant::fromValue (formats));
}
