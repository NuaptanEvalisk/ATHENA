/******************************************************************************
* MODULE     : athena_document_xml_v3.cpp
* DESCRIPTION: Strict compact XML v3 codec for canonical native trees
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "athena_document_xml.hpp"
#include "node_metadata.hpp"
#include "math_tree_v3.hpp"
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
namespace {
void check_document_envelope (const tree& value) {
  if (!is_func (value, DOCUMENT))
    throw codec_exception (codec_error::invalid_structure, "Expected a document tree");
}
std::string_view bytes (const string& s) { return {s.data (), std::size_t (N (s))}; }
QString qtext (std::string_view s) {
  return QString::fromUtf8 (s.data (), qsizetype (s.size ()));
}
string native (const QByteArray& s) { return string (s.constData (), int (s.size ())); }
std::string standard (const QByteArray& s) {
  return {s.constData (), std::size_t (s.size ())};
}

bool v3_core_math_label (tree_label label) {
  // Native v3 is a closed vocabulary for core mathematical primitives.
  // Outside the mathematical core, labels are governed by the registered DRD
  // extension declarations. No old-XML alias recognizer is involved.
  if (label < AROUND || label > MATH_SYMBOL) return true;
  switch (label) {
  case FRAC: case SQRT: case NEG: case TREE: case SYNTAX:
  case DELIMITED: case MATH_MIDDLE: case BIG_OPERATOR: case MATH_SCRIPTS:
  case MATH_ACCENT: case MATH_ANNOTATION: case MATH_LONG_ARROW:
  case MATH_GRID: case MATH_BLOCK: case MATH_VARIANT: case MATH_SYMBOL:
    return true;
  default: return false;
  }
}

constexpr const char* grid_kind_property= "math:grid-kind";
constexpr const char* block_numbered_property= "math:block-numbered";

bool grid_kind_allowed (std::string_view value) {
  return value == "matrix" || value == "eqnarray" ||
         value == "align" || value == "cases" ||
         value == "stack" || value == "generic";
}

// XML v3 intentionally shares neither its grammar nor its reader with v1/v2.
// <a v="3" k="d|t"> wraps <n>/<t>/<b>/<d>/<o>/<m>. Attributes and
// property types are short, explicit and canonical; <t> always contains <v>.
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
  std::set<std::string> identities;
  struct node_frame {
    const tree* value;
    int child;
  };
  std::vector<node_frame> node_stack;
  bool error_located= false;

  std::string location () const {
    std::string indices, ancestry, identity;
    for (const auto& frame: node_stack) {
      if (frame.child >= 0) indices+= "/" + std::to_string (frame.child);
      else if (frame.child == -2) indices+= "/@rich-text";
      const tree& value= *frame.value;
      if (!ancestry.empty ()) ancestry+= " > ";
      ancestry+= is_atomic (value) ? "text" : std::string (bytes (as_string (L (value))));
      if (is_func (value, ASSOCIATE, 2) && is_atomic (value[0]))
        ancestry+= "[" + std::string (bytes (value[0]->label).substr (0, 80)) + "]";
      const auto id= athena::node::id (value);
      if (!id.empty ()) identity= id;
    }
    return " at tree path " + (indices.empty () ? "/" : indices) +
      " (" + ancestry + (identity.empty () ? "" : "; nearest UUID " + identity) + ")";
  }
  QString checked_text (std::string_view s) {
    count.consume (s.size ());
    if (!text::valid_utf8 (s))
      throw codec_exception (codec_error::invalid_utf8, "XML text is not valid UTF-8");
    return qtext (s);
  }
  void payload (std::string_view s, bool binary) {
    if (binary) {
      count.consume (s.size ());
      xml.writeAttribute ("e", "b");
      xml.writeCharacters (QString::fromLatin1 (encoded (s)));
    }
    else {
      auto decoded= checked_text (s);
      if (xml_text (decoded)) xml.writeCharacters (decoded);
      else {
        xml.writeAttribute ("e", "b");
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
      xml.writeAttribute (encoding, "b");
    }
  }
  void reference_id (const std::string& id) {
    if (!athena::node::valid_id (id))
      throw codec_exception (codec_error::invalid_structure, "Invalid metadata identity");
    xml.writeAttribute ("i", checked_text (id));
  }
  void property (const athena::node::property& value, std::size_t depth,
                 const std::string* name= nullptr) {
    count.node (depth);
    xml.writeStartElement (name ? "p" : "i");
    if (name) name_attribute ("n", "ne", *name);
    const auto& data= value.data;
    if (const auto* s= std::get_if<std::string> (&data)) {
      xml.writeAttribute ("t", "s");
      payload (*s, false);
    }
    else if (const auto* b= std::get_if<bool> (&data)) {
      xml.writeAttribute ("t", "b");
      xml.writeCharacters (checked_text (*b ? "true" : "false"));
    }
    else if (const auto* i= std::get_if<std::int64_t> (&data)) {
      xml.writeAttribute ("t", "i");
      xml.writeCharacters (checked_text (number_text (*i)));
    }
    else if (const auto* d= std::get_if<double> (&data)) {
      if (!std::isfinite (*d))
        throw codec_exception (codec_error::invalid_structure, "Nonfinite numeric property");
      xml.writeAttribute ("t", "f");
      xml.writeCharacters (checked_text (number_text (*d)));
    }
    else if (const auto* list= std::get_if<athena::node::property::list> (&data)) {
      xml.writeAttribute ("t", "l");
      for (const auto& item: *list) property (item, depth + 1);
    }
    else if (const auto* dict= std::get_if<athena::node::property::dictionary> (&data)) {
      xml.writeAttribute ("t", "d");
      for (const auto& entry: *dict) property (entry.second, depth + 1, &entry.first);
    }
    else if (const auto* ref= std::get_if<athena::node::reference> (&data)) {
      xml.writeAttribute ("t", "r");
      reference_id (ref->id);
    }
    else if (const auto* rich= std::get_if<athena::node::rich_text> (&data)) {
      xml.writeAttribute ("t", "h");
      node (rich->content, depth + 1, false, -2);
    }
    else throw codec_exception (codec_error::invalid_structure, "Invalid property variant");
    xml.writeEndElement ();
    if (sink.exhausted)
      throw codec_exception (codec_error::resource_limit, "XML output byte limit exceeded");
  }
  void metadata (const tree& value, std::size_t depth,
                 const char* structural_property= nullptr) {
    const auto* meta= athena::node::get (value);
    if (!meta || meta->empty ()) return;
    if (!meta->id.empty ()) {
      if (!identities.insert (meta->id).second)
        throw identity_conflict (meta->id);
      reference_id (meta->id);
    }
    std::size_t count_properties= meta->properties.size ();
    if (structural_property && meta->properties.count (structural_property))
      --count_properties;
    if (count_properties != 0) {
      xml.writeStartElement ("x");
      for (const auto& entry: meta->properties)
        if (!structural_property || entry.first != structural_property)
        property (entry.second, depth + 1, &entry.first);
      xml.writeEndElement ();
    }
  }
  void node (const tree& value, std::size_t depth, bool binary= false,
             int child= -1) {
    node_stack.push_back ({&value, child});
    struct pop_frame {
      std::vector<node_frame>& stack;
      ~pop_frame () { stack.pop_back (); }
    } pop {node_stack};
    try { node_impl (value, depth, binary); }
    catch (const identity_conflict&) { throw; }
    catch (const codec_exception& error) {
      if (error_located) throw;
      error_located= true;
      throw codec_exception (error.code, std::string (error.what ()) + location (),
                             error.line, error.column, error.character_offset);
    }
  }
  void node_impl (const tree& value, std::size_t depth, bool binary) {
    count.node (depth);
    if (is_atomic (value)) {
      xml.writeStartElement (binary ? "b" : "t");
      metadata (value, depth);
      xml.writeStartElement ("v");
      payload (bytes (value->label), binary);
      xml.writeEndElement ();
      xml.writeEndElement ();
    }
    else if (is_compound (value)) {
      if (binary || (L (value) == RAW_DATA &&
          (N (value) != 1 || !is_atomic (value[0]))))
        throw codec_exception (codec_error::invalid_structure, "RAW_DATA requires one byte payload");
      if (L (value) == DELIMITED) {
        if (!math_v3::valid_delimited (value))
          throw codec_exception (codec_error::invalid_structure, "Invalid delimited expression");
        xml.writeStartElement ("d");
        const auto descriptor= [&] (int index) {
          const auto text= checked_text (bytes (value[index]->label));
          if (!xml_text (text))
            throw codec_exception (codec_error::invalid_structure, "Invalid delimiter attribute");
          return text;
        };
        xml.writeAttribute ("l", descriptor (0));
        xml.writeAttribute ("r", descriptor (1));
        xml.writeAttribute ("s", descriptor (2));
        if (value[2] == "m") {
          xml.writeAttribute ("ls", descriptor (3));
          xml.writeAttribute ("rs", descriptor (4));
        }
        metadata (value, depth);
        node (value[5], depth + 1, false, 5);
        xml.writeEndElement ();
        return;
      }
      if (L (value) == BIG_OPERATOR) {
        if (!math_v3::valid_big_operator (value))
          throw codec_exception (codec_error::invalid_structure, "Invalid big operator");
        xml.writeStartElement ("o");
        const auto k= checked_text (bytes (value[0]->label));
        if (!xml_text (k))
          throw codec_exception (codec_error::invalid_structure, "Invalid operator glyph");
        xml.writeAttribute ("k", k);
        xml.writeAttribute ("u", checked_text (bytes (value[1]->label)));
        metadata (value, depth);
        xml.writeEndElement ();
        return;
      }
      if (L (value) == MATH_MIDDLE) {
        // <m> stores its glyph as an attribute. Delegate to the native
        // validator so an identified/attributed glyph child cannot be
        // accepted and silently stripped during serialization.
        if (!math_v3::valid_native_math (value))
          throw codec_exception (codec_error::invalid_structure, "Invalid middle delimiter");
        xml.writeStartElement ("m");
        const auto k= checked_text (bytes (value[0]->label));
        if (!xml_text (k))
          throw codec_exception (codec_error::invalid_structure, "Invalid middle glyph");
        xml.writeAttribute ("k", k);
        metadata (value, depth);
        xml.writeEndElement ();
        return;
      }
      if (L (value) == MATH_SCRIPTS && !math_v3::valid_scripts (value)) {
        std::string reason;
        const auto* meta= athena::node::get (value);
        if (N (value) != 5)
          reason= "expected 5 children, got " + std::to_string (N (value));
        else if (!meta || !meta->properties.count (math_v3::scripts_policy_key))
          reason= "missing math:scripts-policy";
        else {
          const auto& policy= meta->properties.at (math_v3::scripts_policy_key);
          reason= std::holds_alternative<std::string> (policy.data) ?
            "math:scripts-policy must be one of a, d, o, r" :
            "math:scripts-policy must be a string";
        }
        throw codec_exception (codec_error::invalid_structure, "Invalid MathScripts: " + reason);
      }
      if (!v3_core_math_label (L (value)))
        throw codec_exception (codec_error::invalid_structure,
                               "Noncanonical v3 mathematical node: " +
                               std::string (as_string (L (value)).data (),
                                            std::size_t (N (as_string (L (value))))));
      if (!math_v3::valid_native_math (value))
        throw codec_exception (codec_error::invalid_structure,
                               "Malformed native mathematical node: " +
                               std::string (as_string (L (value)).data (),
                                            std::size_t (N (as_string (L (value))))));
      string tag= as_string (L (value));
      // Do not serialize an unregistered native label as the placeholder '?'.
      if (!existing_tree_label (tag) || as_tree_label (tag) != L (value) || N (tag) == 0)
        throw codec_exception (codec_error::invalid_structure, "Unregistered document tag");
      auto name= checked_text (bytes (tag));
      xml.writeStartElement ("n");
      if (xml_text (name) && !name.contains ('\t') && !name.contains ('\n'))
        xml.writeAttribute ("t", name);
      else {
        xml.writeAttribute ("t", QString::fromLatin1 (encoded (bytes (tag))));
        xml.writeAttribute ("te", "b");
      }
      if (L (value) == MATH_SCRIPTS) {
        const auto* meta= athena::node::get (value);
        const auto& policy= std::get<std::string> (
          meta->properties.at (math_v3::scripts_policy_key).data);
        xml.writeAttribute ("p", checked_text (policy));
      }
      if (L (value) == MATH_GRID) {
        const auto* meta= athena::node::get (value);
        const auto found= meta ? meta->properties.find (grid_kind_property) :
                                 athena::node::property::dictionary::const_iterator ();
        if (!meta || found == meta->properties.end ())
          throw codec_exception (codec_error::invalid_structure, "MathGrid lacks kind");
        const auto* kind= std::get_if<std::string> (&found->second.data);
        if (!kind || !grid_kind_allowed (*kind))
          throw codec_exception (codec_error::invalid_structure, "Invalid MathGrid kind");
        xml.writeAttribute ("k", checked_text (*kind));
      }
      if (L (value) == MATH_BLOCK) {
        if (N (value) != 1)
          throw codec_exception (codec_error::invalid_structure, "MathBlock needs one body");
        const auto* meta= athena::node::get (value);
        const auto found= meta ? meta->properties.find (block_numbered_property) :
                                 athena::node::property::dictionary::const_iterator ();
        if (!meta || found == meta->properties.end ())
          throw codec_exception (codec_error::invalid_structure, "MathBlock lacks numbering flag");
        const auto* numbered= std::get_if<bool> (&found->second.data);
        if (!numbered)
          throw codec_exception (codec_error::invalid_structure, "Invalid MathBlock numbering flag");
        xml.writeAttribute ("b", *numbered ? "1" : "0");
      }
      // Grid ROW numbering and rich tags stay in generic typed metadata. Their
      // schema is checked by the MATH_GRID parent, never imposed on text ROWs.
      metadata (value, depth,
        L (value) == MATH_SCRIPTS ? math_v3::scripts_policy_key :
        L (value) == MATH_GRID ? grid_kind_property :
        L (value) == MATH_BLOCK ? block_numbered_property : nullptr);
      for (int i= 0; i < N (value); ++i)
        node (value[i], depth + 1, L (value) == RAW_DATA, i);
      xml.writeEndElement ();
    }
    else throw codec_exception (codec_error::opaque_value, "Opaque native value is not document content");
    if (sink.exhausted)
      throw codec_exception (codec_error::resource_limit, "XML output byte limit exceeded");
  }
public:
  explicit writer (codec_limits limits):
    count {limits}, sink (limits.output_bytes), xml (&sink) {}
  std::string write (const tree& value, xml_kind kind) {
    if (kind == xml_kind::document) check_document_envelope (value);
    xml.writeStartDocument ("1.0");
    xml.writeStartElement ("a");
    xml.writeAttribute ("v", "3");
    xml.writeAttribute ("k", kind == xml_kind::document ? "d" : "t");
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
    auto encoding= xml.attributes ().value ("e");
    const bool base64= xml.attributes ().hasAttribute ("e");
    if ((binary && encoding != QLatin1StringView ("b")) ||
        (!binary && base64 && encoding != QLatin1StringView ("b")))
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
    attributes ({"e"});
    return tree (native (scalar (binary)));
  }
  std::string reference_id () {
    if (!xml.attributes ().hasAttribute ("i"))
      fail (codec_error::invalid_structure, "Missing reference identity");
    auto id= standard (decode (xml.attributes ().value ("i").toString (), false, false));
    if (!athena::node::valid_id (id))
      fail (codec_error::invalid_structure, "Invalid metadata identity");
    return id;
  }
  std::string property_name () {
    if (!xml.attributes ().hasAttribute ("n"))
      fail (codec_error::invalid_structure, "Missing property name");
    auto encoding= xml.attributes ().value ("ne");
    bool base64= xml.attributes ().hasAttribute ("ne");
    if (base64 && encoding != QLatin1StringView ("b"))
      fail (codec_error::invalid_structure, "Unexpected property name encoding");
    auto name= standard (decode (xml.attributes ().value ("n").toString (), base64, false));
    if (name.empty ()) fail (codec_error::invalid_structure, "Empty property name");
    return name;
  }
  athena::node::property property (std::size_t depth, bool named) {
    using property_type= athena::node::property;
    count.node (depth);
    if (!xml.isStartElement () ||
        xml.name () != QLatin1StringView (named ? "p" : "i"))
      fail (codec_error::invalid_structure, "Unexpected property element");
    // Copy attribute views before advancing the stream.
    auto type= xml.attributes ().value ("t").toString ();
    if (type == QLatin1StringView ("s")) {
      if (named) attributes ({"n", "ne", "t", "e"});
      else attributes ({"t", "e"});
      return property_type (standard (scalar ()));
    }
    if (type == QLatin1StringView ("r")) {
      if (named) attributes ({"n", "ne", "t", "i"});
      else attributes ({"t", "i"});
      auto id= reference_id ();
      whitespace ();
      if (!xml.isEndElement ())
        fail (codec_error::invalid_structure, "Reference must not contain a value");
      return property_type (athena::node::reference {std::move (id)});
    }
    if (named) attributes ({"n", "ne", "t"});
    else attributes ({"t"});
    if (type == QLatin1StringView ("l")) {
      property_type::list items;
      whitespace ();
      while (xml.isStartElement ()) {
        items.push_back (property (depth + 1, false));
        whitespace ();
      }
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Missing list end");
      return property_type (std::move (items));
    }
    if (type == QLatin1StringView ("d"))
      return property_type (dictionary (depth + 1));
    if (type == QLatin1StringView ("h")) {
      whitespace ();
      if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing rich tree");
      tree content= node (depth + 1);
      whitespace ();
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Multiple rich trees");
      return property_type (athena::node::rich_text {content});
    }
    if (type != QLatin1StringView ("b") && type != QLatin1StringView ("i") &&
        type != QLatin1StringView ("f"))
      fail (codec_error::invalid_structure, "Unknown property type");
    auto value= standard (scalar ());
    if (type == QLatin1StringView ("b")) {
      if (value != "true" && value != "false")
        fail (codec_error::invalid_structure, "Invalid boolean property");
      return property_type (value == "true");
    }
    if (type == QLatin1StringView ("i")) {
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
        !std::isfinite (real) || number_text (real) != value)
      fail (codec_error::invalid_structure, "Invalid double property");
    return property_type (real);
  }
  athena::node::property::dictionary dictionary (std::size_t depth) {
    athena::node::property::dictionary result;
    whitespace ();
    while (xml.isStartElement ()) {
      if (xml.name () != QLatin1StringView ("p"))
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
    if (xml.attributes ().hasAttribute ("i")) {
      result.id= reference_id ();
      if (!identities.insert (result.id).second)
        throw identity_conflict (result.id, xml.lineNumber (), xml.columnNumber (), xml.characterOffset ());
    }
    whitespace ();
    if (xml.isStartElement () && xml.name () == QLatin1StringView ("x")) {
      attributes ({});
      result.properties= dictionary (depth + 1);
      if (result.properties.empty ())
        fail (codec_error::invalid_structure, "Empty properties element");
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
    if (xml.name () == QLatin1StringView (binary ? "b" : "t")) {
      attributes ({"i"});
      auto meta= metadata (depth);
      if (!xml.isStartElement () || xml.name () != QLatin1StringView ("v"))
        fail (codec_error::invalid_structure, "Missing atomic value");
      tree result= atom (binary);
      whitespace ();
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Unexpected atomic content");
      attach (result, meta);
      return result;
    }
    if (binary) fail (codec_error::invalid_structure, "Expected a binary value");

    if (xml.name () == QLatin1StringView ("d")) {
      attributes ({"l", "r", "s", "ls", "rs", "i"});
      const auto att= xml.attributes ();
      if (!att.hasAttribute ("l") || !att.hasAttribute ("r") ||
          !att.hasAttribute ("s"))
        fail (codec_error::invalid_structure, "Missing delimiter fields");
      const string left= native (decode (att.value ("l").toString (), false, false));
      const string right= native (decode (att.value ("r").toString (), false, false));
      const string sizing= native (decode (att.value ("s").toString (), false, false));
      const string left_size= native (decode (att.value ("ls").toString (), false, false));
      const string right_size= native (decode (att.value ("rs").toString (), false, false));
      if (N (sizing) != 1 ||
          ((sizing == "m") != (att.hasAttribute ("ls") && att.hasAttribute ("rs"))) ||
          (sizing != "m" && (att.hasAttribute ("ls") || att.hasAttribute ("rs"))))
        fail (codec_error::invalid_structure, "Invalid delimiter size policy");
      auto meta= metadata (depth);
      if (!xml.isStartElement ()) fail (codec_error::invalid_structure, "Missing delimiter body");
      tree body= node (depth + 1);
      whitespace ();
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Multiple delimiter bodies");
      tree result (DELIMITED, tree (left), tree (right), tree (sizing),
                   tree (left_size), tree (right_size), body);
      if (!math_v3::valid_delimited (result))
        fail (codec_error::invalid_structure, "Invalid delimiter glyph/size");
      attach (result, meta);
      return result;
    }

    if (xml.name () == QLatin1StringView ("o")) {
      attributes ({"k", "u", "i"});
      if (!xml.attributes ().hasAttribute ("k") ||
          !xml.attributes ().hasAttribute ("u"))
        fail (codec_error::invalid_structure, "Missing big-operator descriptor");
      const string glyph= native (decode (xml.attributes ().value ("k").toString (), false, false));
      const string upright= native (decode (xml.attributes ().value ("u").toString (), false, false));
      auto meta= metadata (depth);
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Big operator has children");
      tree result (BIG_OPERATOR, tree (glyph), tree (upright));
      if (!math_v3::valid_big_operator (result))
        fail (codec_error::invalid_structure, "Invalid big operator descriptor");
      attach (result, meta);
      return result;
    }

    if (xml.name () == QLatin1StringView ("m")) {
      attributes ({"k", "i"});
      if (!xml.attributes ().hasAttribute ("k"))
        fail (codec_error::invalid_structure, "Missing middle delimiter glyph");
      const string glyph= native (decode (xml.attributes ().value ("k").toString (), false, false));
      auto meta= metadata (depth);
      if (!xml.isEndElement ()) fail (codec_error::invalid_structure, "Middle delimiter has children");
      if (!math_v3::glyph (glyph))
        fail (codec_error::invalid_structure, "Invalid middle delimiter glyph");
      tree result (MATH_MIDDLE, tree (glyph));
      attach (result, meta);
      return result;
    }

    if (xml.name () != QLatin1StringView ("n"))
      fail (codec_error::invalid_structure, "Expected a node or text element");
    attributes ({"t", "te", "i", "p", "k", "b"});
    if (!xml.attributes ().hasAttribute ("t"))
      fail (codec_error::invalid_structure, "Missing document tag");
    auto encoding= xml.attributes ().value ("te");
    const bool base64= xml.attributes ().hasAttribute ("te");
    if (base64 && encoding != QLatin1StringView ("b"))
      fail (codec_error::invalid_structure, "Unexpected tag encoding");
    string tag= native (decode (xml.attributes ().value ("t").toString (),
                                base64, false));
    if (N (tag) == 0) fail (codec_error::invalid_structure, "Empty document tag");
    tree_label label= as_tree_label (tag);
    if (label <= TMSTRING || label == UNKNOWN ||
        label == DELIMITED || label == BIG_OPERATOR || label == MATH_MIDDLE)
      fail (codec_error::invalid_structure,
            ("Undeclared or noncanonical v3 document tag: " +
             std::string (tag.data (), N(tag))).c_str ());
    if (!v3_core_math_label (label))
      fail (codec_error::invalid_structure, "Noncanonical v3 mathematical node");
    if (label != MATH_SCRIPTS && xml.attributes ().hasAttribute ("p"))
      fail (codec_error::invalid_structure, "Unexpected script placement policy");
    if (label != MATH_GRID && xml.attributes ().hasAttribute ("k"))
      fail (codec_error::invalid_structure, "Unexpected MathGrid kind attribute");
    if (label != MATH_BLOCK && xml.attributes ().hasAttribute ("b"))
      fail (codec_error::invalid_structure, "Unexpected MathBlock numbering attribute");
    string placement;
    if (label == MATH_SCRIPTS) {
      if (!xml.attributes ().hasAttribute ("p"))
        fail (codec_error::invalid_structure, "Missing script placement policy");
      placement= native (decode (xml.attributes ().value ("p").toString (), false, false));
      if (!math_v3::valid_scripts_policy (placement))
        fail (codec_error::invalid_structure, "Invalid script placement policy");
    }
    std::string grid_kind;
    if (label == MATH_GRID) {
      if (!xml.attributes ().hasAttribute ("k"))
        fail (codec_error::invalid_structure, "Missing MathGrid kind");
      const QByteArray data= decode (xml.attributes ().value ("k").toString (),
                                     false, false);
      grid_kind.assign (data.constData (), std::size_t (data.size ()));
      if (!grid_kind_allowed (grid_kind))
        fail (codec_error::invalid_structure, "Invalid MathGrid kind");
    }
    bool block_numbered= false;
    if (label == MATH_BLOCK) {
      if (!xml.attributes ().hasAttribute ("b"))
        fail (codec_error::invalid_structure, "Missing MathBlock numbering");
      auto flag= xml.attributes ().value ("b");
      if (flag != QLatin1StringView ("0") && flag != QLatin1StringView ("1"))
        fail (codec_error::invalid_structure, "Invalid MathBlock numbering");
      block_numbered= flag == QLatin1StringView ("1");
    }
    array<tree> children;
    athena::node::metadata meta= metadata (depth);
    if (label == MATH_SCRIPTS) {
      if (meta.properties.count (math_v3::scripts_policy_key))
        fail (codec_error::invalid_structure, "Duplicate script placement metadata");
      meta.properties.emplace (math_v3::scripts_policy_key,
        athena::node::property (
          std::string (placement.data (), std::size_t (N (placement)))));
    }
    if (label == MATH_GRID) {
      if (meta.properties.count (grid_kind_property))
        fail (codec_error::invalid_structure, "Duplicate MathGrid kind metadata");
      meta.properties.emplace (grid_kind_property,
                               athena::node::property (std::move (grid_kind)));
    }
    if (label == MATH_BLOCK) {
      if (meta.properties.count (block_numbered_property))
        fail (codec_error::invalid_structure, "Duplicate MathBlock numbering metadata");
      meta.properties.emplace (block_numbered_property,
                               athena::node::property (block_numbered));
    }
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
    if (!math_v3::valid_native_math (result))
      fail (codec_error::invalid_structure, "Invalid native v3 mathematics node");
    return result;
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
    if (!xml.isStartElement () || xml.name () != QLatin1StringView ("a"))
      fail (codec_error::invalid_structure, "Unexpected XML document envelope");
    attributes ({"v", "k"});
    if (xml.attributes ().value ("v") != QLatin1StringView ("3") ||
        xml.attributes ().value ("k") !=
          QLatin1StringView (kind == xml_kind::document ? "d" : "t"))
      fail (codec_error::unsupported_version, "Unsupported ATHENA XML v3 envelope");
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

tree read_xml_v3 (std::string_view input, xml_kind kind, codec_limits limits) {
  return reader (input, limits).read (kind);
}
std::string write_xml_v3 (const tree& input, xml_kind kind, codec_limits limits) {
  return writer (limits).write (input, kind);
}
} // namespace athena::document
