/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revision_transfer.hpp"
#include <QCborArray>
#include <QCborValue>
#include <algorithm>
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
  if (operation == QStringLiteral ("have")) {
    if (value.size () != 4 || message.size () > 4096)
      throw std::invalid_argument ("Invalid revision inventory probe");
    auto vault= string (value[2]), id= string (value[3]);
    if (!authorized_ (vault)) throw std::invalid_argument ("Revision Vault is not authorized");
    return encode ({domain, QStringLiteral ("have"), text (vault), text (id), store_.contains (vault, id)});
  }
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

vault_revision_sender::vault_revision_sender (revision_store& store, std::string vault,
  revision_receiver::authorization authorized, std::int64_t after):
  store_ (store), vault_ (std::move (vault)), authorized_ (std::move (authorized)), cursor_ (after) {
  if (cursor_ < 0) throw std::invalid_argument ("Invalid Vault inventory cursor");
  authorize ();
}
void vault_revision_sender::authorize () const {
  if (!authorized_ || !authorized_ (vault_))
    throw std::invalid_argument ("Outgoing revision Vault authorization expired");
}
QByteArray vault_revision_sender::begin () {
  authorize ();
  if (phase_ != phase::initial) throw std::logic_error ("Vault exchange already started");
  through_= store_.inventory_tip (vault_);
  if (cursor_ > through_) throw std::invalid_argument ("Vault inventory cursor is stale");
  return advance ();
}
QByteArray vault_revision_sender::advance () {
  authorize ();
  if (stack_.empty ()) {
    if (heads_.empty ()) {
      heads_= store_.inventory_heads (vault_, cursor_, through_);
      if (heads_.empty ()) { phase_= phase::complete; return {}; }
      cursor_= heads_.back ().sequence;
      std::reverse (heads_.begin (), heads_.end ());
    }
    stack_.push_back ({heads_.back ().id}); heads_.pop_back ();
  }
  if (!stack_.back ().expanded) {
    phase_= phase::probe;
    return encode ({domain, QStringLiteral ("have"), text (vault_), text (stack_.back ().id)});
  }
  transfer_= std::make_unique<revision_sender> (store_, vault_, stack_.back ().id, authorized_);
  phase_= phase::transfer; return transfer_->begin ();
}
QByteArray vault_revision_sender::acknowledge (const QByteArray& message) {
  authorize ();
  if (phase_ == phase::transfer) {
    auto next= transfer_->acknowledge (message);
    if (!transfer_->complete ()) return next;
    transfer_.reset (); stack_.pop_back (); return advance ();
  }
  if (phase_ != phase::probe) throw std::logic_error ("Vault exchange has no outstanding request");
  auto value= parse (message, 4096);
  if (value.size () != 5 || value[1].toString () != QStringLiteral ("have") ||
      string (value[2]) != vault_ || string (value[3]) != stack_.back ().id || !value[4].isBool ())
    throw std::invalid_argument ("Inventory response does not match the outstanding probe");
  if (value[4].toBool ()) stack_.pop_back ();
  else {
    auto offered= store_.offer (stack_.back ().id);
    if (!offered || offered->metadata.vault != vault_)
      throw std::invalid_argument ("Missing outgoing causal revision");
    if (stack_.size () + offered->metadata.parents.size () > 65536)
      throw std::invalid_argument ("Revision ancestry exceeds traversal budget");
    stack_.back ().expanded= true;
    for (auto p= offered->metadata.parents.rbegin (); p != offered->metadata.parents.rend (); ++p)
      stack_.push_back ({*p});
  }
  return advance ();
}
bool vault_revision_sender::complete () const { return phase_ == phase::complete; }
std::int64_t vault_revision_sender::completed_through () const {
  if (!complete ()) throw std::logic_error ("Vault exchange has not completed");
  return through_;
}

namespace {
const QString exchange_domain= QStringLiteral ("ATHENA-HODARIUM-EXCHANGE-v1");
QByteArray envelope (bool reply, std::int64_t sequence, const std::string& vault, QByteArray bytes) {
  return encode ({exchange_domain, reply, qint64 (sequence), text (vault), std::move (bytes)});
}
}
revision_exchange::revision_exchange (revision_store& store, revision_receiver::authorization authorized):
  store_ (store), authorized_ (std::move (authorized)),
  receiver_ (store, [this] (const std::string& vault) {
    return vault == receiving_vault_ && authorized_ (vault);
  }) {
  if (!authorized_) throw std::invalid_argument ("Revision exchange requires Vault authorization");
}
bool revision_exchange::busy () const { return sender_ && !sender_->complete (); }
bool revision_exchange::start (const std::string& vault) {
  if (busy ()) return false;
  if (vault.empty () || vault.size () > 1024 || !authorized_ (vault))
    throw std::invalid_argument ("Invalid or unauthorized exchange Vault");
  auto cursor= cursors_.find (vault);
  if (cursor == cursors_.end () && cursors_.size () >= 256)
    throw std::invalid_argument ("Too many Vaults on a revision session");
  sending_vault_= vault;
  sender_= std::make_unique<vault_revision_sender> (store_, vault, authorized_,
    cursor == cursors_.end () ? 0 : cursor->second);
  request (sender_->begin ()); return true;
}
void revision_exchange::request (QByteArray message) {
  if (sender_->complete ()) {
    cursors_[sending_vault_]= sender_->completed_through (); return;
  }
  if (next_ == std::numeric_limits<std::int64_t>::max ())
    throw std::runtime_error ("Revision session sequence exhausted");
  waiting_= next_++;
  output_.push_back ({envelope (false, waiting_, sending_vault_, std::move (message)), sending_vault_, false});
}
QByteArray revision_exchange::outgoing () const {
  if (output_.empty ()) return {};
  if (!authorized_ (output_.front ().vault))
    throw std::invalid_argument ("Queued revision Vault authorization expired");
  return output_.front ().bytes;
}
void revision_exchange::sent () {
  if (output_.empty ()) throw std::logic_error ("No queued revision message");
  if (output_.front ().reply) reply_pending_= false;
  output_.pop_front ();
}
void revision_exchange::accept (const QByteArray& message) {
  if (message.size () > 512*1024) throw std::invalid_argument ("Revision envelope exceeds budget");
  QCborParserError error;
  auto decoded= QCborValue::fromCbor (message, &error);
  if (error.error != QCborError::NoError || error.offset != message.size () || !decoded.isArray ())
    throw std::invalid_argument ("Invalid revision envelope encoding");
  auto value= decoded.toArray ();
  if (value.size () != 5 || value[0] != exchange_domain || !value[1].isBool () ||
      !value[4].isByteArray ()) throw std::invalid_argument ("Invalid revision envelope");
  auto sequence= integer (value[2]); auto vault= string (value[3]);
  if (vault.empty () || vault.size () > 1024 || !authorized_ (vault))
    throw std::invalid_argument ("Incoming exchange Vault is not authorized");
  if (value[1].toBool ()) {
    if (!busy () || waiting_ == 0 || sequence != std::uint64_t (waiting_) || vault != sending_vault_ ||
        std::any_of (output_.begin (), output_.end (), [] (const queued& q) { return !q.reply; }))
      throw std::invalid_argument ("Unexpected revision exchange response");
    waiting_= 0;
    request (sender_->acknowledge (value[4].toByteArray ()));
  }
  else {
    if (reply_pending_ || sequence != std::uint64_t (incoming_) || incoming_ == std::numeric_limits<std::int64_t>::max ())
      throw std::invalid_argument ("Unexpected revision exchange request");
    receiving_vault_= vault;
    auto reply= receiver_.accept (value[4].toByteArray ());
    ++incoming_; reply_pending_= true;
    output_.push_back ({envelope (true, std::int64_t (sequence), vault, std::move (reply)), vault, true});
  }
}
} // namespace athena::hodarium
