/******************************************************************************
* MODULE     : athena_document_xml.cpp
* DESCRIPTION: Bounded Qt XML codec preserving native UTF-8 tree structure
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "athena_document_xml.hpp"
#include "unicode_text.hpp"
#include <QByteArray>
#include <QIODevice>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <limits>

namespace athena::document {
codec_exception::codec_exception (codec_error c, const std::string& message,
                                  std::int64_t l, std::int64_t col,
                                  std::int64_t offset):
  std::runtime_error (message), code (c), line (l), column (col),
  character_offset (offset) {}

namespace {
const char* envelope (xml_kind kind) {
  return kind == xml_kind::document ? "athena-document" : "athena-tree";
}
std::string_view bytes (const string& s) { return {s.data (), std::size_t (N (s))}; }
QString qtext (std::string_view s) {
  return QString::fromUtf8 (s.data (), qsizetype (s.size ()));
}
string native (const QByteArray& s) { return string (s.constData (), int (s.size ())); }

struct budget {
  codec_limits limits;
  std::size_t nodes= 0, text= 0;
  void node (std::size_t depth) {
    if (depth > limits.depth || nodes >= limits.nodes)
      throw codec_exception (codec_error::resource_limit, "XML tree node/depth limit exceeded");
    ++nodes;
  }
  void consume (std::size_t size) {
    if (size > limits.text_bytes - text ||
        size > std::size_t (std::numeric_limits<int>::max ()))
      throw codec_exception (codec_error::resource_limit, "XML tree text limit exceeded");
    text+= size;
  }
};

bool xml_text (const QString& s) {
  // CR is legal XML, but literal CR undergoes XML end-of-line normalization.
  for (qsizetype i= 0; i < s.size (); ++i) {
    auto ch= s[i].unicode ();
    if ((ch < 0x20 && ch != 9 && ch != 10) || ch == 0xfffe || ch == 0xffff)
      return false;
  }
  return true;
}
QByteArray encoded (std::string_view s) {
  return QByteArray (s.data (), qsizetype (s.size ())).toBase64 ();
}

class output: public QIODevice {
public:
  QByteArray data;
  std::size_t limit;
  bool exhausted= false;
  explicit output (std::size_t size): limit (size) { open (QIODevice::WriteOnly); }
  qint64 readData (char*, qint64) override { return -1; }
  qint64 writeData (const char* source, qint64 size) override {
    if (size < 0 || std::uint64_t (size) > limit - std::size_t (data.size ())) {
      exhausted= true;
      return -1;
    }
    data.append (source, qsizetype (size));
    return size;
  }
};

class writer {
  budget count;
  output sink;
  QXmlStreamWriter xml;
  QString checked_text (std::string_view s) {
    count.consume (s.size ());
    if (!text::valid_utf8 (s))
      throw codec_exception (codec_error::invalid_utf8, "XML text is not valid UTF-8");
    return qtext (s);
  }
  void atom (const string& value, bool binary) {
    auto s= bytes (value);
    if (binary) {
      count.consume (s.size ());
      xml.writeStartElement ("bytes");
      xml.writeAttribute ("encoding", "base64");
      xml.writeCharacters (QString::fromLatin1 (encoded (s)));
    }
    else {
      auto decoded= checked_text (s);
      xml.writeStartElement ("text");
      if (xml_text (decoded)) xml.writeCharacters (decoded);
      else {
        xml.writeAttribute ("encoding", "base64-utf8");
        xml.writeCharacters (QString::fromLatin1 (encoded (s)));
      }
    }
    xml.writeEndElement ();
  }
  void node (const tree& value, std::size_t depth, bool binary= false) {
    count.node (depth);
    if (is_atomic (value)) atom (value->label, binary);
    else if (is_compound (value)) {
      if (binary || (L (value) == RAW_DATA &&
          (N (value) != 1 || !is_atomic (value[0]))))
        throw codec_exception (codec_error::invalid_structure, "RAW_DATA requires one byte payload");
      string tag= as_string (L (value));
      // Do not serialize an unregistered native label as the placeholder '?'.
      if (!existing_tree_label (tag) || as_tree_label (tag) != L (value) || N (tag) == 0)
        throw codec_exception (codec_error::invalid_structure, "Unregistered document tag");
      auto name= checked_text (bytes (tag));
      xml.writeStartElement ("node");
      if (xml_text (name) && !name.contains ('\t') && !name.contains ('\n'))
        xml.writeAttribute ("tag", name);
      else {
        xml.writeAttribute ("tag", QString::fromLatin1 (encoded (bytes (tag))));
        xml.writeAttribute ("tag-encoding", "base64-utf8");
      }
      for (int i= 0; i < N (value); ++i) node (value[i], depth + 1, L (value) == RAW_DATA);
      xml.writeEndElement ();
    }
    else throw codec_exception (codec_error::opaque_value, "Opaque native value is not document content");
    if (sink.exhausted)
      throw codec_exception (codec_error::resource_limit, "XML output byte limit exceeded");
  }
public:
  explicit writer (codec_limits limits): count {limits}, sink (limits.output_bytes), xml (&sink) {}
  std::string write (const tree& value, xml_kind kind) {
    if (kind == xml_kind::document && !is_func (value, DOCUMENT))
      throw codec_exception (codec_error::invalid_structure, "Expected a document tree");
    xml.writeStartDocument ("1.0");
    xml.writeStartElement (envelope (kind));
    xml.writeAttribute ("version", "1");
    xml.writeAttribute ("text-model", "utf-8");
    node (value, 0);
    xml.writeEndElement ();
    xml.writeEndDocument ();
    if (sink.exhausted)
      throw codec_exception (codec_error::resource_limit, "XML output byte limit exceeded");
    if (xml.hasError ())
      throw codec_exception (codec_error::invalid_structure, "XML writer rejected document content");
    return {sink.data.constData (), std::size_t (sink.data.size ())};
  }
};

class reader {
  budget count;
  QXmlStreamReader xml;
  [[noreturn]] void fail (codec_error code, const char* message) {
    throw codec_exception (code, message, xml.lineNumber (), xml.columnNumber (),
                           xml.characterOffset ());
  }
  void next () {
    xml.readNext ();
    if (xml.hasError ()) {
      auto message= xml.errorString ().toUtf8 ();
      fail (codec_error::malformed_xml, message.constData ());
    }
    if (xml.isDTD ()) fail (codec_error::forbidden_dtd, "DTD is not permitted in ATHENA XML");
    if (xml.isEntityReference ())
      fail (codec_error::invalid_structure, "Unresolved XML entity");
    if (xml.isStartDocument () && !xml.documentEncoding ().isEmpty () &&
        xml.documentEncoding ().compare (QLatin1StringView ("UTF-8"), Qt::CaseInsensitive) != 0)
      fail (codec_error::invalid_utf8, "ATHENA XML requires UTF-8 encoding");
  }
  void attributes (std::initializer_list<const char*> allowed) {
    if (!xml.namespaceUri ().isEmpty () || !xml.prefix ().isEmpty () ||
        !xml.namespaceDeclarations ().isEmpty ())
      fail (codec_error::invalid_structure, "Unexpected XML namespace");
    for (const auto& attribute: xml.attributes ()) {
      bool found= false;
      for (auto name: allowed)
        if (attribute.qualifiedName () == QLatin1StringView (name)) found= true;
      if (!found) fail (codec_error::invalid_structure, "Unexpected XML attribute");
    }
  }
  void whitespace () {
    while (true) {
      next ();
      if (xml.isStartElement () || xml.isEndElement () || xml.isEndDocument ()) return;
      if (xml.isCharacters () && !xml.isWhitespace ())
        fail (codec_error::invalid_structure, "Text outside a text node");
    }
  }
  QByteArray decode (const QString& value, bool base64, bool binary) {
    QByteArray result;
    if (base64) {
      QByteArray ascii= value.toLatin1 ();
      auto decoded= QByteArray::fromBase64Encoding (ascii, QByteArray::AbortOnBase64DecodingErrors);
      if (!decoded || QString::fromLatin1 (ascii) != value ||
          decoded.decoded.toBase64 () != ascii)
        fail (codec_error::invalid_base64, "Invalid or noncanonical Base64 payload");
      result= std::move (decoded.decoded);
    }
    else result= value.toUtf8 ();
    count.consume (std::size_t (result.size ()));
    if (!binary && !text::valid_utf8 ({result.constData (), std::size_t (result.size ())}))
      fail (codec_error::invalid_utf8, "Decoded text is not valid UTF-8");
    return result;
  }
  tree atom (bool binary) {
    attributes ({"encoding"});
    auto encoding= xml.attributes ().value ("encoding");
    bool base64= binary || encoding == QLatin1StringView ("base64-utf8");
    if ((binary && encoding != QLatin1StringView ("base64")) ||
        (!binary && !encoding.isEmpty () && !base64))
      fail (codec_error::invalid_structure, "Unexpected text encoding");
    QString value;
    while (true) {
      next ();
      if (xml.isEndElement ()) break;
      if (!xml.isCharacters ())
        fail (codec_error::invalid_structure, "Text node contains non-text content");
      if (std::uint64_t (value.size ()) + std::uint64_t (xml.text ().size ()) >
          count.limits.input_bytes)
        fail (codec_error::resource_limit, "XML text token limit exceeded");
      value+= xml.text ();
    }
    return tree (native (decode (value, base64, binary)));
  }
  tree node (std::size_t depth, bool binary= false) {
    count.node (depth);
    if (xml.name () == QLatin1StringView (binary ? "bytes" : "text")) return atom (binary);
    if (binary || xml.name () != QLatin1StringView ("node"))
      fail (codec_error::invalid_structure, "Expected a node or text element");
    attributes ({"tag", "tag-encoding"});
    if (!xml.attributes ().hasAttribute ("tag"))
      fail (codec_error::invalid_structure, "Missing document tag");
    auto encoding= xml.attributes ().value ("tag-encoding");
    if (!encoding.isEmpty () && encoding != QLatin1StringView ("base64-utf8"))
      fail (codec_error::invalid_structure, "Unexpected tag encoding");
    string tag= native (decode (xml.attributes ().value ("tag").toString (),
                                !encoding.isEmpty (), false));
    if (N (tag) == 0) fail (codec_error::invalid_structure, "Empty document tag");
    tree_label label= make_tree_label (tag);
    if (label <= TMSTRING) fail (codec_error::invalid_structure, "Invalid compound tag");
    array<tree> children;
    while (true) {
      whitespace ();
      if (xml.isEndElement ()) break;
      if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing node end");
      if (label == RAW_DATA && N (children) != 0)
        fail (codec_error::invalid_structure, "RAW_DATA requires one byte payload");
      children << node (depth + 1, label == RAW_DATA);
    }
    if (label == RAW_DATA && N (children) != 1)
      fail (codec_error::invalid_structure, "RAW_DATA requires one byte payload");
    return tree (label, children);
  }
public:
  reader (std::string_view input, codec_limits limits): count {limits} {
    if (input.size () > limits.input_bytes)
      fail (codec_error::resource_limit, "XML input byte limit exceeded");
    if (!text::valid_utf8 (input)) fail (codec_error::invalid_utf8, "XML input is not UTF-8");
    xml.addData (QByteArray (input.data (), qsizetype (input.size ())));
  }
  tree read (xml_kind kind) {
    whitespace ();
    if (!xml.isStartElement () || xml.name () != QLatin1StringView (envelope (kind)))
      fail (codec_error::invalid_structure, "Unexpected XML document envelope");
    attributes ({"version", "text-model"});
    if (xml.attributes ().value ("version") != QLatin1StringView ("1") ||
        xml.attributes ().value ("text-model") != QLatin1StringView ("utf-8"))
      fail (codec_error::unsupported_version, "Unsupported ATHENA XML or text-model version");
    whitespace ();
    if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing document tree");
    tree result= node (0);
    if (kind == xml_kind::document && !is_func (result, DOCUMENT))
      fail (codec_error::invalid_structure, "Expected a document tree");
    whitespace ();
    if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Multiple document trees");
    whitespace ();
    if (!xml.isEndDocument ()) fail (codec_error::invalid_structure, "Content after document envelope");
    return result;
  }
};
} // namespace

tree read_xml (std::string_view input, xml_kind kind, codec_limits limits) {
  return reader (input, limits).read (kind);
}
std::string write_xml (const tree& input, xml_kind kind, codec_limits limits) {
  return writer (limits).write (input, kind);
}
} // namespace athena::document
