/******************************************************************************
* MODULE     : compound_virtual_document.cpp
* DESCRIPTION: Versioned AVD storage, namespace membership and prefix caches
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "compound_virtual_document.hpp"
#include "namespaces.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Typeset/Env/compound_counters.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace athena::avd {
namespace {
constexpr qint64 maximum_descriptor_bytes= 64 * 1024 * 1024;

[[noreturn]] void fail (const QString& reason) {
  throw std::runtime_error (reason.toStdString ());
}

QByteArray digest (const QByteArray& bytes) {
  return QCryptographicHash::hash (bytes, QCryptographicHash::Sha256);
}

QByteArray read_bytes (const QString& filename) {
  QFile input (filename);
  if (!input.open (QIODevice::ReadOnly)) fail (input.errorString ());
  if (input.size () > maximum_descriptor_bytes)
    fail (QStringLiteral ("AVD descriptor exceeds the size limit"));
  const auto bytes= input.read (maximum_descriptor_bytes + 1);
  if (input.error () != QFileDevice::NoError) fail (input.errorString ());
  if (bytes.size () > maximum_descriptor_bytes)
    fail (QStringLiteral ("AVD descriptor exceeds the size limit"));
  return bytes;
}

QString required_string (const QJsonObject& object, const char* key) {
  const auto value= object.value (QLatin1String (key));
  if (!value.isString () || value.toString ().isEmpty ())
    fail (QStringLiteral ("Missing or invalid AVD field: ") + QLatin1String (key));
  return value.toString ();
}

void validate_descriptor (const descriptor& value) {
  const QUuid uuid (value.namespace_uuid);
  if (uuid.isNull () || uuid.toString (QUuid::WithoutBraces) != value.namespace_uuid)
    fail (QStringLiteral ("AVD namespace must be a canonical UUID"));
  if (value.vault_directory.isEmpty ())
    fail (QStringLiteral ("AVD requires a vault directory"));
}

void validate_counter_xml (const QByteArray& bytes) {
  // Decode on the receiving thread; no tree ownership crosses an actor boundary.
  const tree state= document::read_xml_v2 (
    std::string_view (bytes.constData (), std::size_t (bytes.size ())),
    document::xml_kind::fragment);
  validate_counter_state (state);
}

QByteArray prefix_digest (const QByteArray& previous, const member& value) {
  QCryptographicHash hash (QCryptographicHash::Sha256);
  // Length-framed fields distinguish paths/revisions containing separators.
  for (const auto& field: {previous, value.relative_filename.toUtf8 (),
                           value.content_revision, value.environment_revision}) {
    hash.addData (QByteArray::number (field.size ()));
    hash.addData (QByteArrayLiteral (":"));
    hash.addData (field);
  }
  return hash.result ();
}
} // namespace

descriptor_file read_descriptor (const QString& filename) {
  const auto bytes= read_bytes (filename);
  QJsonParseError error;
  const auto json= QJsonDocument::fromJson (bytes, &error);
  if (error.error != QJsonParseError::NoError)
    fail (QStringLiteral ("Invalid AVD JSON at byte %1: %2")
          .arg (error.offset).arg (error.errorString ()));
  if (!json.isObject ()) fail (QStringLiteral ("AVD must contain a JSON object"));
  const auto object= json.object ();
  if (object.value (QStringLiteral ("format")) != QStringLiteral ("athena-avd") ||
      object.value (QStringLiteral ("version")) != QJsonValue (1))
    fail (QStringLiteral ("Unsupported AVD format or version"));
  descriptor value;
  value.namespace_uuid= required_string (object, "namespace");
  value.vault_directory= required_string (object, "vault");
  validate_descriptor (value);
  const auto cache= object.value (QStringLiteral ("counter_cache"));
  if (!cache.isUndefined ()) {
    if (!cache.isObject ()) fail (QStringLiteral ("Invalid AVD counter cache"));
    const auto envelope= cache.toObject ();
    // Caches are disposable. An older/newer computation format is not a reason
    // to reject an otherwise valid namespace selection.
    if (envelope.value (QStringLiteral ("version")) == QJsonValue (1)) {
      const auto entries= envelope.value (QStringLiteral ("entries"));
      if (!entries.isArray ()) fail (QStringLiteral ("Invalid AVD checkpoints"));
      for (const auto& entry: entries.toArray ()) {
        if (!entry.isObject ()) fail (QStringLiteral ("Invalid AVD checkpoint"));
        const auto item= entry.toObject ();
        counter_checkpoint point;
        point.member= required_string (item, "member");
        const auto hex= required_string (item, "prefix_revision").toLatin1 ();
        point.prefix_revision= QByteArray::fromHex (hex);
        if (point.prefix_revision.size () != 32 || point.prefix_revision.toHex () != hex)
          fail (QStringLiteral ("Invalid AVD checkpoint revision"));
        point.counters_xml= required_string (item, "counters_xml").toUtf8 ();
        validate_counter_xml (point.counters_xml);
        value.checkpoints.push_back (std::move (point));
      }
    }
  }
  return {std::move (value), digest (bytes)};
}

QByteArray save_descriptor (const QString& filename, const descriptor& value,
                           const QByteArray& expected_revision) {
  validate_descriptor (value);
  QJsonArray entries;
  for (const auto& point: value.checkpoints) {
    if (point.member.isEmpty () || point.prefix_revision.size () != 32)
      fail (QStringLiteral ("Invalid AVD checkpoint identity"));
    validate_counter_xml (point.counters_xml);
    entries.append (QJsonObject {
      {QStringLiteral ("member"), point.member},
      {QStringLiteral ("prefix_revision"), QString::fromLatin1 (point.prefix_revision.toHex ())},
      {QStringLiteral ("counters_xml"), QString::fromUtf8 (point.counters_xml)}});
  }
  const auto bytes= QJsonDocument (QJsonObject {
    {QStringLiteral ("format"), QStringLiteral ("athena-avd")},
    {QStringLiteral ("version"), 1},
    {QStringLiteral ("namespace"), value.namespace_uuid},
    {QStringLiteral ("vault"), value.vault_directory},
    {QStringLiteral ("counter_cache"), QJsonObject {
      {QStringLiteral ("version"), 1}, {QStringLiteral ("entries"), entries}}}
  }).toJson (QJsonDocument::Indented);
  if (bytes.size () > maximum_descriptor_bytes)
    fail (QStringLiteral ("AVD descriptor exceeds the size limit"));
  QLockFile lock (filename + QStringLiteral (".lock"));
  if (!lock.tryLock (0)) fail (QStringLiteral ("AVD is being saved by another writer"));
  const bool exists= QFileInfo::exists (filename);
  if ((expected_revision.isEmpty () && exists) ||
      (!expected_revision.isEmpty () &&
       (!exists || digest (read_bytes (filename)) != expected_revision)))
    fail (QStringLiteral ("AVD changed externally; refusing to overwrite it"));
  QSaveFile output (filename);
  output.setDirectWriteFallback (false);
  if (!output.open (QIODevice::WriteOnly) || output.write (bytes) != bytes.size ())
    fail (output.errorString ());
  if (!output.commit ()) fail (output.errorString ());
  return digest (bytes);
}

std::vector<member> resolve_members (const QString& descriptor_filename,
                                    const descriptor& value,
                                    const vault_context_handle& context) {
  validate_descriptor (value);
  if (!context || !vault_context_is_current (context))
    fail (QStringLiteral ("The AVD vault is not active"));
  const QString root= QFileInfo (QDir (QFileInfo (descriptor_filename).absolutePath ())
    .absoluteFilePath (value.vault_directory)).canonicalFilePath ();
  if (root.isEmpty () || root != QFileInfo (
        QString::fromStdString (context->root.string ())).canonicalFilePath ())
    fail (QStringLiteral ("AVD belongs to a different vault"));
  const auto uuid= value.namespace_uuid.toUtf8 ();
  string error;
  const auto matches= athena_namespace_members (
    context, string (uuid.constData (), int (uuid.size ())), error);
  if (error != "") fail (QString::fromUtf8 (error.c_str (), N (error)));
  std::vector<member> result;
  std::set<QString> seen;
  for (const auto& match: matches) {
    const QString filename= QString::fromUtf8 (match.file_path.c_str (), N (match.file_path));
    if (seen.insert (filename).second)
      result.push_back ({filename, QDir (root).relativeFilePath (filename), {}, {}});
  }
  return result;
}

counter_cache::counter_cache (std::vector<member> members,
                             QByteArray computation_contract,
                             const std::vector<counter_checkpoint>& persisted):
  members_ (std::move (members)), prefixes_ (members_.size ()),
  checkpoints_ (members_.size ()) {
  if (computation_contract.isEmpty ())
    fail (QStringLiteral ("AVD counter computation requires a versioned contract"));
  seed_= digest (QByteArrayLiteral ("ATHENA AVD counters v1\n") + computation_contract);
  std::map<QString, counter_checkpoint> by_member;
  for (const auto& point: persisted) {
    if (!by_member.emplace (point.member, point).second)
      fail (QStringLiteral ("Duplicate AVD counter checkpoint"));
  }
  for (std::size_t i= 0; i < members_.size (); ++i) {
    const auto it= by_member.find (members_[i].relative_filename);
    if (it != by_member.end ()) checkpoints_[i]= it->second;
  }
  recompute_from (0);
}

void counter_cache::recompute_from (std::size_t first) {
  QByteArray previous= first == 0 ? seed_ : prefixes_.at (first - 1);
  for (std::size_t i= first; i < size (); ++i) {
    const auto& source= members_[i];
    if (previous.isEmpty () || source.content_revision.isEmpty () ||
        source.environment_revision.isEmpty ()) prefixes_[i].clear ();
    else prefixes_[i]= prefix_digest (previous, source);
    previous= prefixes_[i];
  }
}

void counter_cache::set_revision (std::size_t i, QByteArray content,
                                  QByteArray environment) {
  auto& source= members_.at (i);
  if (source.content_revision == content && source.environment_revision == environment) return;
  source.content_revision= std::move (content);
  source.environment_revision= std::move (environment);
  recompute_from (i);
}

QByteArray counter_cache::prefix_after (std::size_t i) const {
  return prefixes_.at (i);
}

void counter_cache::invalidate_from (std::size_t first) {
  if (first > size ()) throw std::out_of_range ("AVD counter boundary");
  for (std::size_t i= first; i < size (); ++i) {
    members_[i].content_revision.clear ();
    members_[i].environment_revision.clear ();
    prefixes_[i].clear ();
  }
}

std::optional<counter_checkpoint> counter_cache::before (std::size_t i) const {
  if (i > size ()) throw std::out_of_range ("AVD counter boundary");
  if (i == 0) return std::nullopt;
  const auto& point= checkpoints_[i - 1];
  if (prefixes_[i - 1].isEmpty () || point.counters_xml.isEmpty () ||
      point.prefix_revision != prefixes_[i - 1]) return std::nullopt;
  return point;
}

bool counter_cache::publish (std::size_t i, const QByteArray& expected_prefix,
                             QByteArray counters_xml) {
  if (prefixes_.at (i).isEmpty () || prefixes_[i] != expected_prefix) return false;
  validate_counter_xml (counters_xml);
  checkpoints_[i]= {members_[i].relative_filename, expected_prefix, std::move (counters_xml)};
  return true;
}

std::vector<counter_checkpoint> counter_cache::persistent_checkpoints () const {
  std::vector<counter_checkpoint> result;
  for (std::size_t i= 1; i <= size (); ++i)
    if (auto point= before (i)) result.push_back (std::move (*point));
  return result;
}

layout_index::layout_index (std::size_t count, double estimate):
  heights_ (count, 0.0), sums_ (count + 1, 0.0) {
  for (std::size_t i= 0; i < count; ++i) set_height (i, estimate);
}

void layout_index::set_height (std::size_t index, double value) {
  if (!std::isfinite (value) || value <= 0)
    throw std::invalid_argument ("AVD member height must be finite and positive");
  const double delta= value - heights_.at (index);
  heights_[index]= value;
  for (std::size_t i= index + 1; i < sums_.size (); i+= i & (~i + 1))
    sums_[i]+= delta;
}

double layout_index::top (std::size_t index) const {
  if (index > size ()) throw std::out_of_range ("AVD member boundary");
  double result= 0;
  for (std::size_t i= index; i != 0; i-= i & (~i + 1)) result+= sums_[i];
  return result;
}

std::size_t layout_index::member_at (double offset) const {
  if (!std::isfinite (offset)) throw std::invalid_argument ("Invalid AVD scroll offset");
  if (offset < 0) return 0;
  std::size_t index= 0, step= 1;
  while (step <= size () / 2) step*= 2;
  for (; step != 0; step/= 2) {
    const std::size_t next= index + step;
    if (next <= size () && sums_[next] <= offset) {
      index= next;
      offset-= sums_[next];
    }
  }
  return index;
}
} // namespace athena::avd
