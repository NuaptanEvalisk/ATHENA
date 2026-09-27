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
#include "node_metadata.hpp"
#include "unicode_text.hpp"
#include <QByteArray>
#include <QIODevice>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace athena::document {
codec_exception::codec_exception (codec_error c, const std::string& message,
                                  std::int64_t l, std::int64_t col,
                                  std::int64_t offset):
  std::runtime_error (message), code (c), line (l), column (col),
  character_offset (offset) {}

namespace {
bool legacy_version (const tree& value) {
  return is_compound (value, "TeXmacs", 1) && is_atomic (value[0]);
}
void check_document_envelope (const tree& value) {
  if (!is_func (value, DOCUMENT))
    throw codec_exception (codec_error::invalid_structure, "Expected a document tree");
  for (int i= 0; i < N (value); ++i)
    if (legacy_version (value[i]))
      throw codec_exception (codec_error::invalid_structure,
        "Legacy TeXmacs version metadata must be removed before XML storage");
}
const char* envelope (xml_kind kind) {
  return kind == xml_kind::document ? "athena-document" : "athena-tree";
}
std::string_view bytes (const string& s) { return {s.data (), std::size_t (N (s))}; }
QString qtext (std::string_view s) {
  return QString::fromUtf8 (s.data (), qsizetype (s.size ()));
}
string native (const QByteArray& s) { return string (s.constData (), int (s.size ())); }
std::string standard (const QByteArray& s) {
  return {s.constData (), std::size_t (s.size ())};
}

// V2 properties: <property name="..." type="...">value</property>.
// Types are string, boolean (true/false), int64 (canonical signed decimal),
// double (finite, shortest round-trip decimal), list, dictionary, reference and
// rich_tree. Types are explicit, never inferred from string contents.
// Lists contain <item type>
// values; dictionaries contain named <property> values. References have an id
// attribute and no content; rich_tree contains exactly one regular V2 node.
// Strings use the text codec's optional encoding="base64-utf8". Names use
// optional name-encoding="base64-utf8", just like compound tag attributes.
// Each property/item consumes one node and one depth level; rich_tree's node
// consumes another. IDs, keys and scalar payloads share the tree text budget.
// Properties/value wrappers are structural and consume no additional budget.
// Maps are emitted in key order; empty properties wrappers are omitted. Node
// IDs are unique throughout the envelope, including rich trees; references may
// repeat or point outside the envelope (fragments need not resolve references).
template<typename T> std::string number_text (T value) {
  char buffer[128];
  auto result= std::to_chars (buffer, buffer + sizeof (buffer), value);
  if (result.ec != std::errc ())
    throw codec_exception (codec_error::invalid_structure, "Invalid numeric property");
  return {buffer, result.ptr};
}

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
  bool v2;
  std::set<std::string> identities;
  QString checked_text (std::string_view s) {
    count.consume (s.size ());
    if (!text::valid_utf8 (s))
      throw codec_exception (codec_error::invalid_utf8, "XML text is not valid UTF-8");
    return qtext (s);
  }
  void payload (std::string_view s, bool binary) {
    if (binary) {
      count.consume (s.size ());
      xml.writeAttribute ("encoding", "base64");
      xml.writeCharacters (QString::fromLatin1 (encoded (s)));
    }
    else {
      auto decoded= checked_text (s);
      if (xml_text (decoded)) xml.writeCharacters (decoded);
      else {
        xml.writeAttribute ("encoding", "base64-utf8");
        xml.writeCharacters (QString::fromLatin1 (encoded (s)));
      }
    }
  }
  void name_attribute (const char* name, const char* encoding, std::string_view s) {
    auto value= checked_text (s);
    if (xml_text (value) && !value.contains ('\t') && !value.contains ('\n'))
      xml.writeAttribute (name, value);
    else {
      xml.writeAttribute (name, QString::fromLatin1 (encoded (s)));
      xml.writeAttribute (encoding, "base64-utf8");
    }
  }
  void reference_id (const std::string& id) {
    if (!athena::node::valid_id (id))
      throw codec_exception (codec_error::invalid_structure, "Invalid metadata identity");
    xml.writeAttribute ("id", checked_text (id));
  }
  void property (const athena::node::property& value, std::size_t depth,
                 const std::string* name= nullptr) {
    count.node (depth);
    xml.writeStartElement (name ? "property" : "item");
    if (name) name_attribute ("name", "name-encoding", *name);
    const auto& data= value.data;
    if (const auto* s= std::get_if<std::string> (&data)) {
      xml.writeAttribute ("type", "string");
      payload (*s, false);
    }
    else if (const auto* b= std::get_if<bool> (&data)) {
      xml.writeAttribute ("type", "boolean");
      xml.writeCharacters (checked_text (*b ? "true" : "false"));
    }
    else if (const auto* i= std::get_if<std::int64_t> (&data)) {
      xml.writeAttribute ("type", "int64");
      xml.writeCharacters (checked_text (number_text (*i)));
    }
    else if (const auto* d= std::get_if<double> (&data)) {
      if (!std::isfinite (*d))
        throw codec_exception (codec_error::invalid_structure, "Nonfinite numeric property");
      xml.writeAttribute ("type", "double");
      xml.writeCharacters (checked_text (number_text (*d)));
    }
    else if (const auto* list= std::get_if<athena::node::property::list> (&data)) {
      xml.writeAttribute ("type", "list");
      for (const auto& item: *list) property (item, depth + 1);
    }
    else if (const auto* dict= std::get_if<athena::node::property::dictionary> (&data)) {
      xml.writeAttribute ("type", "dictionary");
      for (const auto& entry: *dict) property (entry.second, depth + 1, &entry.first);
    }
    else if (const auto* ref= std::get_if<athena::node::reference> (&data)) {
      xml.writeAttribute ("type", "reference");
      reference_id (ref->id);
    }
    else if (const auto* rich= std::get_if<athena::node::rich_text> (&data)) {
      xml.writeAttribute ("type", "rich_tree");
      node (rich->content, depth + 1);
    }
    else throw codec_exception (codec_error::invalid_structure, "Invalid property variant");
    xml.writeEndElement ();
    if (sink.exhausted)
      throw codec_exception (codec_error::resource_limit, "XML output byte limit exceeded");
  }
  void metadata (const tree& value, std::size_t depth) {
    const auto* meta= athena::node::get (value);
    if (!meta || meta->empty ()) return;
    if (!meta->id.empty ()) {
      if (!identities.insert (meta->id).second)
        throw codec_exception (codec_error::invalid_structure, "Duplicate node identity");
      reference_id (meta->id);
    }
    if (!meta->properties.empty ()) {
      xml.writeStartElement ("properties");
      for (const auto& entry: meta->properties)
        property (entry.second, depth + 1, &entry.first);
      xml.writeEndElement ();
    }
  }
  void node (const tree& value, std::size_t depth, bool binary= false) {
    count.node (depth);
    const auto* meta= athena::node::get (value);
    if (!v2 && meta && !meta->empty ())
      throw codec_exception (codec_error::invalid_structure, "XML v1 cannot preserve node metadata");
    if (is_atomic (value)) {
      xml.writeStartElement (binary ? "bytes" : "text");
      if (v2) {
        metadata (value, depth);
        xml.writeStartElement ("value");
      }
      payload (bytes (value->label), binary);
      if (v2) xml.writeEndElement ();
      xml.writeEndElement ();
    }
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
      if (v2) metadata (value, depth);
      for (int i= 0; i < N (value); ++i) node (value[i], depth + 1, L (value) == RAW_DATA);
      xml.writeEndElement ();
    }
    else throw codec_exception (codec_error::opaque_value, "Opaque native value is not document content");
    if (sink.exhausted)
      throw codec_exception (codec_error::resource_limit, "XML output byte limit exceeded");
  }
public:
  explicit writer (codec_limits limits, bool version2= false):
    count {limits}, sink (limits.output_bytes), xml (&sink), v2 (version2) {}
  std::string write (const tree& value, xml_kind kind) {
    if (kind == xml_kind::document) check_document_envelope (value);
    xml.writeStartDocument ("1.0");
    xml.writeStartElement (envelope (kind));
    xml.writeAttribute ("version", v2 ? "2" : "1");
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
  bool v2= false;
  std::set<std::string> identities;
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
  QByteArray scalar (bool binary= false) {
    auto encoding= xml.attributes ().value ("encoding");
    bool base64= binary || encoding == QLatin1StringView ("base64-utf8");
    if ((binary && encoding != QLatin1StringView ("base64")) ||
        (!binary && ((!encoding.isEmpty () && !base64) ||
         (v2 && xml.attributes ().hasAttribute ("encoding") && encoding.isEmpty ()))))
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
    return decode (value, base64, binary);
  }
  tree atom (bool binary) {
    attributes ({"encoding"});
    return tree (native (scalar (binary)));
  }
  std::string reference_id () {
    auto id= standard (decode (xml.attributes ().value ("id").toString (), false, false));
    if (!athena::node::valid_id (id))
      fail (codec_error::invalid_structure, "Invalid metadata identity");
    return id;
  }
  std::string property_name () {
    if (!xml.attributes ().hasAttribute ("name"))
      fail (codec_error::invalid_structure, "Missing property name");
    auto encoding= xml.attributes ().value ("name-encoding");
    bool base64= xml.attributes ().hasAttribute ("name-encoding");
    if (base64 && encoding != QLatin1StringView ("base64-utf8"))
      fail (codec_error::invalid_structure, "Unexpected property name encoding");
    auto name= standard (decode (xml.attributes ().value ("name").toString (), base64, false));
    if (name.empty ()) fail (codec_error::invalid_structure, "Empty property name");
    return name;
  }
  athena::node::property property (std::size_t depth, bool named) {
    using property_type= athena::node::property;
    count.node (depth);
    if (!xml.isStartElement () ||
        xml.name () != QLatin1StringView (named ? "property" : "item"))
      fail (codec_error::invalid_structure, "Unexpected property element");
    // Copy attribute views before advancing the stream.
    auto type= xml.attributes ().value ("type").toString ();
    if (type == QLatin1StringView ("string")) {
      if (named) attributes ({"name", "name-encoding", "type", "encoding"});
      else attributes ({"type", "encoding"});
      return property_type (standard (scalar ()));
    }
    if (type == QLatin1StringView ("reference")) {
      if (named) attributes ({"name", "name-encoding", "type", "id"});
      else attributes ({"type", "id"});
      auto id= reference_id ();
      whitespace ();
      if (!xml.isEndElement ())
        fail (codec_error::invalid_structure, "Reference must not contain a value");
      return property_type (athena::node::reference {std::move (id)});
    }
    if (named) attributes ({"name", "name-encoding", "type"});
    else attributes ({"type"});
    if (type == QLatin1StringView ("list")) {
      property_type::list items;
      whitespace ();
      while (xml.isStartElement ()) {
        items.push_back (property (depth + 1, false));
        whitespace ();
      }
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Missing list end");
      return property_type (std::move (items));
    }
    if (type == QLatin1StringView ("dictionary"))
      return property_type (dictionary (depth + 1));
    if (type == QLatin1StringView ("rich_tree")) {
      whitespace ();
      if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing rich tree");
      tree content= node (depth + 1);
      whitespace ();
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Multiple rich trees");
      return property_type (athena::node::rich_text {content});
    }
    if (type != QLatin1StringView ("boolean") && type != QLatin1StringView ("int64") &&
        type != QLatin1StringView ("double"))
      fail (codec_error::invalid_structure, "Unknown property type");
    auto value= standard (scalar ());
    if (type == QLatin1StringView ("boolean")) {
      if (value != "true" && value != "false")
        fail (codec_error::invalid_structure, "Invalid boolean property");
      return property_type (value == "true");
    }
    if (type == QLatin1StringView ("int64")) {
      std::int64_t integer;
      auto result= std::from_chars (value.data (), value.data () + value.size (), integer);
      if (result.ec != std::errc () || result.ptr != value.data () + value.size () ||
          number_text (integer) != value)
        fail (codec_error::invalid_structure, "Invalid int64 property");
      return property_type (integer);
    }
    double real;
    auto result= std::from_chars (value.data (), value.data () + value.size (), real);
    if (result.ec != std::errc () || result.ptr != value.data () + value.size () ||
        !std::isfinite (real))
      fail (codec_error::invalid_structure, "Invalid double property");
    return property_type (real);
  }
  athena::node::property::dictionary dictionary (std::size_t depth) {
    athena::node::property::dictionary result;
    whitespace ();
    while (xml.isStartElement ()) {
      if (xml.name () != QLatin1StringView ("property"))
        fail (codec_error::invalid_structure, "Expected named property");
      auto name= property_name ();
      if (result.find (name) != result.end ())
        fail (codec_error::invalid_structure, "Duplicate property name");
      auto value= property (depth, true);
      result.emplace (std::move (name), std::move (value));
      whitespace ();
    }
    if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Missing properties end");
    return result;
  }
  athena::node::metadata metadata (std::size_t depth) {
    athena::node::metadata result;
    if (xml.attributes ().hasAttribute ("id")) {
      result.id= reference_id ();
      if (!identities.insert (result.id).second)
        fail (codec_error::invalid_structure, "Duplicate node identity");
    }
    whitespace ();
    if (xml.isStartElement () && xml.name () == QLatin1StringView ("properties")) {
      attributes ({});
      result.properties= dictionary (depth + 1);
      whitespace ();
    }
    return result;
  }
  void attach (tree& value, const athena::node::metadata& meta) {
    if (meta.empty ()) return;
    try { athena::node::set (value, meta); }
    catch (const std::invalid_argument& error) {
      fail (codec_error::invalid_structure, error.what ());
    }
    catch (const std::length_error& error) {
      fail (codec_error::resource_limit, error.what ());
    }
  }
  tree node (std::size_t depth, bool binary= false) {
    count.node (depth);
    if (xml.name () == QLatin1StringView (binary ? "bytes" : "text")) {
      if (!v2) return atom (binary);
      attributes ({"id"});
      auto meta= metadata (depth);
      if (!xml.isStartElement () || xml.name () != QLatin1StringView ("value"))
        fail (codec_error::invalid_structure, "Missing atomic value");
      tree result= atom (binary);
      whitespace ();
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Unexpected atomic content");
      attach (result, meta);
      return result;
    }
    if (binary || xml.name () != QLatin1StringView ("node"))
      fail (codec_error::invalid_structure, "Expected a node or text element");
    if (v2) attributes ({"tag", "tag-encoding", "id"});
    else attributes ({"tag", "tag-encoding"});
    if (!xml.attributes ().hasAttribute ("tag"))
      fail (codec_error::invalid_structure, "Missing document tag");
    auto encoding= xml.attributes ().value ("tag-encoding");
    if ((!encoding.isEmpty () || (v2 && xml.attributes ().hasAttribute ("tag-encoding"))) &&
        encoding != QLatin1StringView ("base64-utf8"))
      fail (codec_error::invalid_structure, "Unexpected tag encoding");
    string tag= native (decode (xml.attributes ().value ("tag").toString (),
                                !encoding.isEmpty (), false));
    if (N (tag) == 0) fail (codec_error::invalid_structure, "Empty document tag");
    tree_label label= make_tree_label (tag);
    if (label <= TMSTRING) fail (codec_error::invalid_structure, "Invalid compound tag");
    array<tree> children;
    athena::node::metadata meta;
    if (v2) meta= metadata (depth);
    else whitespace ();
    while (true) {
      if (xml.isEndElement ()) break;
      if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing node end");
      if (label == RAW_DATA && N (children) != 0)
        fail (codec_error::invalid_structure, "RAW_DATA requires one byte payload");
      children << node (depth + 1, label == RAW_DATA);
      whitespace ();
    }
    if (label == RAW_DATA && N (children) != 1)
      fail (codec_error::invalid_structure, "RAW_DATA requires one byte payload");
    tree result (label, children);
    attach (result, meta);
    return result;
  }
public:
  reader (std::string_view input, codec_limits limits): count {limits} {
    if (input.size () > limits.input_bytes)
      fail (codec_error::resource_limit, "XML input byte limit exceeded");
    if (!text::valid_utf8 (input)) fail (codec_error::invalid_utf8, "XML input is not UTF-8");
    xml.addData (QByteArray (input.data (), qsizetype (input.size ())));
  }
  tree read (xml_kind kind, bool allow_v2= false) {
    whitespace ();
    if (!xml.isStartElement () || xml.name () != QLatin1StringView (envelope (kind)))
      fail (codec_error::invalid_structure, "Unexpected XML document envelope");
    attributes ({"version", "text-model"});
    auto version= xml.attributes ().value ("version");
    v2= version == QLatin1StringView ("2");
    if ((v2 && !allow_v2) || (!v2 && version != QLatin1StringView ("1")) ||
        xml.attributes ().value ("text-model") != QLatin1StringView ("utf-8"))
      fail (codec_error::unsupported_version, "Unsupported ATHENA XML or text-model version");
    whitespace ();
    if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing document tree");
    tree result= node (0);
    if (kind == xml_kind::document) {
      try { check_document_envelope (result); }
      catch (const codec_exception& error) { fail (error.code, error.what ()); }
    }
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
tree read_xml_v2 (std::string_view input, xml_kind kind, codec_limits limits) {
  return reader (input, limits).read (kind, true);
}
std::string write_xml (const tree& input, xml_kind kind, codec_limits limits) {
  return writer (limits).write (input, kind);
}
std::string write_xml_v2 (const tree& input, xml_kind kind, codec_limits limits) {
  return writer (limits, true).write (input, kind);
}

tree strip_legacy_document_version (const tree& input, std::vector<int>* child_map) {
  if (!is_func (input, DOCUMENT))
    throw codec_exception (codec_error::invalid_structure, "Expected a document tree");
  tree result (DOCUMENT);
  athena::node::copy_metadata (input, result);
  std::vector<int> mapping;
  if (child_map) mapping.reserve (N (input));
  for (int i= 0; i < N (input); ++i) {
    const bool removed= legacy_version (input[i]);
    if (child_map) mapping.push_back (removed ? -1 : N (result));
    if (!removed) result << input[i];
  }
  if (child_map) *child_map= std::move (mapping);
  return result;
}
} // namespace athena::document
