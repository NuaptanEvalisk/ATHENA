/******************************************************************************
* MODULE     : pdf_text_string.cpp
* DESCRIPTION: Strict UTF-8 to PDF text strings through Qt's UTF-16BE encoder
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "pdf_text_string.hpp"
#include "unicode_text.hpp"
#include <QStringConverter>
#include <limits>
#include <stdexcept>

namespace athena::text {

std::string pdf_text_string (std::string_view utf8) {
  if (utf8.size () > static_cast<std::size_t> (
        std::numeric_limits<qsizetype>::max () / 4 - 16))
    throw std::length_error ("PDF text string is too large");
  require_utf8 (utf8);
  const auto text= QString::fromUtf8 (
    utf8.data (), static_cast<qsizetype> (utf8.size ()));
  QStringEncoder encode (QStringConverter::Utf16BE);
  const QByteArray bytes= encode (text);
  if (encode.hasError ())
    throw std::runtime_error ("Could not encode PDF text string");
  return "<FEFF" + bytes.toHex ().toStdString () + ">";
}

} // namespace athena::text
