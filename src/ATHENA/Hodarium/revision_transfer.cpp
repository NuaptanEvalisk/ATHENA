/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revision_transfer.hpp"
#include <QCborArray>
#include <QCborValue>
#include <limits>
#include <stdexcept>

namespace athena::hodarium {
namespace {
const QString domain= QStringLiteral ("ATHENA-HODARIUM-REVISION-v1");
QString text (const std::string& bytes) {
  auto result= QString::fromUtf8 (bytes.data (), qsizetype (bytes.size ()));
  if (result.toUtf8 ().toStdString () != bytes)
    throw std::invalid_argument ("Revision descriptor is not UTF-8");
  return result;
}
std::string string (const QCborValue& value) {
  if (!value.isString ()) throw std::invalid_argument ("Expected revision text field");
  return value.toString ().toUtf8 ().toStdString ();
}
std::uint64_t integer (const QCborValue& value) {
  if (!value.isInteger () || value.toInteger () < 0)
    throw std::invalid_argument ("Expected nonnegative revision integer");
  return std::uint64_t (value.toInteger ());
}
QCborArray parse (const QByteArray& bytes, qsizetype budget= 512*1024) {
  if (bytes.size () > budget) throw std::invalid_argument ("Revision message exceeds budget");
  QCborParserError error;
  auto value= QCborValue::fromCbor (bytes, &error);
  if (error.error != QCborError::NoError || error.offset != bytes.size () || !value.isArray ())
    throw std::invalid_argument ("Invalid revision message encoding");
  auto array= value.toArray ();
  if (array.size () < 3 || !array[0].isString () || array[0].toString () != domain || !array[1].isString ())
    throw std::invalid_argument ("Unsupported revision message protocol");
  return array;
}
QByteArray encode (const QCborArray& array) { return QCborValue (array).toCbor (); }
QCborArray metadata (const revision& r) {
  QCborArray parents;
  for (const auto& parent: r.parents) parents.append (text (parent));
  return {text (r.id), text (r.vault), text (r.object), text (r.origin_member),
    text (r.format), qint64 (r.semantic_version), text (r.relative_path), r.deleted, parents};
}
revision metadata (const QCborValue& value) {
  if (!value.isArray ()) throw std::invalid_argument ("Invalid revision descriptor");
  auto fields= value.toArray ();
  if (fields.size () != 9 || !fields[7].isBool () || !fields[8].isArray () || fields[8].toArray ().size () > 256)
    throw std::invalid_argument ("Invalid revision descriptor fields");
  auto version= integer (fields[5]);
  if (version == 0 || version > std::numeric_limits<std::uint32_t>::max ())
    throw std::invalid_argument ("Invalid revision semantic version");
  revision r;
  r.id= string (fields[0]); r.vault= string (fields[1]); r.object= string (fields[2]);
  r.origin_member= string (fields[3]); r.format= string (fields[4]);
  r.semantic_version= std::uint32_t (version); r.relative_path= string (fields[6]); r.deleted= fields[7].toBool ();
  for (const auto& parent: fields[8].toArray ()) r.parents.push_back (string (parent));
  return r;
}
QByteArray ack (const std::string& id, std::uint64_t offset, bool complete) {
  return encode ({domain, QStringLiteral ("ack"), text (id), qint64 (offset), complete});
}
}
revision_receiver::revision_receiver (revision_store& store, authorization authorized):
  store_ (store), authorized_ (std::move (authorized)) {
  if (!authorized_) throw std::invalid_argument ("Revision receiver requires Vault authorization");
}
QByteArray revision_receiver::accept (const QByteArray& message) {
  auto value= parse (message);
  auto operation= value[1].toString ();
  if (operation == QStringLiteral ("offer")) {
    if (value.size () != 4 || message.size () > 128*1024)
      throw std::invalid_argument ("Invalid revision offer");
    revision_offer offer{metadata (value[2]), integer (value[3])};
    if (!authorized_ (offer.metadata.vault)) throw std::invalid_argument ("Revision Vault is not authorized");
    if (!active_.count (offer.metadata.id) && active_.size () >= 32)
      throw std::invalid_argument ("Too many active revision transfers");
    auto offset= store_.begin_receive (offer.metadata, offer.size);
    auto id= offer.metadata.id; active_[id]= std::move (offer);
    return ack (id, offset, false);
  }
  auto id= string (value[2]);
  auto found= active_.find (id);
  if (found == active_.end ()) throw std::invalid_argument ("Revision message has no accepted offer");
  const auto& offer= found->second;
  if (!authorized_ (offer.metadata.vault)) {
    active_.erase (found);
    throw std::invalid_argument ("Revision Vault authorization expired");
  }
  if (operation == QStringLiteral ("chunk")) {
    if (value.size () != 5 || !value[4].isByteArray ()) throw std::invalid_argument ("Invalid revision chunk");
    auto offset= store_.receive_chunk (id, integer (value[3]), value[4].toByteArray ().toStdString ());
    return ack (id, offset, false);
  }
  if (operation == QStringLiteral ("finish")) {
    if (value.size () != 3) throw std::invalid_argument ("Invalid revision completion");
    store_.finish_receive (id);
    auto size= offer.size; active_.erase (found);
    return ack (id, size, true);
  }
  throw std::invalid_argument ("Unknown revision transfer operation");
}
revision_sender::revision_sender (revision_store& store, std::string vault, std::string id,
  revision_receiver::authorization authorized): store_ (store), authorized_ (std::move (authorized)) {
  if (!authorized_ || !authorized_ (vault)) throw std::invalid_argument ("Outgoing revision Vault is not authorized");
  auto offered= store.offer (id);
  if (!offered || offered->metadata.vault != vault || offered->size > 512ULL*1024*1024)
    throw std::invalid_argument ("Outgoing revision does not belong to the requested Vault or exceeds budget");
  offer_= std::move (*offered);
}
QByteArray revision_sender::begin () {
  if (!authorized_ (offer_.metadata.vault)) throw std::invalid_argument ("Outgoing revision Vault authorization expired");
  if (phase_ != phase::initial) throw std::logic_error ("Revision transfer already started");
  auto result= encode ({domain, QStringLiteral ("offer"), metadata (offer_.metadata), qint64 (offer_.size)});
  if (result.size () > 128*1024) throw std::invalid_argument ("Outgoing revision descriptor exceeds budget");
  phase_= phase::offer; return result;
}
QByteArray revision_sender::next (std::uint64_t offset) {
  if (offset == offer_.size) {
    phase_= phase::finish;
    return encode ({domain, QStringLiteral ("finish"), text (offer_.metadata.id)});
  }
  auto bytes= store_.payload_chunk (offer_.metadata.id, offset);
  if (bytes.empty ()) throw std::runtime_error ("Outgoing revision ended before advertised size");
  expected_= offset + bytes.size (); phase_= phase::chunk;
  return encode ({domain, QStringLiteral ("chunk"), text (offer_.metadata.id),
    qint64 (offset), QByteArray::fromStdString (bytes)});
}
QByteArray revision_sender::acknowledge (const QByteArray& message) {
  if (!authorized_ (offer_.metadata.vault)) throw std::invalid_argument ("Outgoing revision Vault authorization expired");
  auto value= parse (message, 1024);
  if (value.size () != 5 || value[1].toString () != QStringLiteral ("ack") ||
      string (value[2]) != offer_.metadata.id || !value[4].isBool ())
    throw std::invalid_argument ("Invalid revision acknowledgement");
  auto offset= integer (value[3]); bool complete= value[4].toBool ();
  if (offset > offer_.size || phase_ == phase::initial || phase_ == phase::complete ||
      (phase_ == phase::chunk && offset != expected_) ||
      (phase_ == phase::finish && (!complete || offset != offer_.size)) ||
      (phase_ != phase::finish && complete))
    throw std::invalid_argument ("Revision acknowledgement does not match the outstanding request");
  if (phase_ == phase::finish) { phase_= phase::complete; return {}; }
  return next (offset);
}
bool revision_sender::complete () const { return phase_ == phase::complete; }
} // namespace athena::hodarium
