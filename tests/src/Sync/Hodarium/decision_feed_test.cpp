/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "decision_feed.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <iostream>
using namespace athena::hodarium;
using json= nlohmann::json;
namespace {
void require (bool value,const char* message) { if (!value) throw std::runtime_error (message); }
std::string base64 (const unsigned char* data,std::size_t size) {
  return QByteArray (reinterpret_cast<const char*> (data),qsizetype (size))
    .toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
}
struct authority {
  unsigned char secret[crypto_sign_SECRETKEYBYTES];
  authority_pin pin;
  std::string epoch,nonce,subject,vault,conflict;
  authority () {
    unsigned char key[crypto_sign_PUBLICKEYBYTES];crypto_sign_keypair (key,secret);
    auto id= [] { unsigned char bytes[32];randombytes_buf (bytes,sizeof bytes);return base64 (bytes,sizeof bytes); };
    pin.group=id ();pin.generation=id ();pin.public_key=base64 (key,sizeof key);
    epoch=id ();nonce=id ();subject=id ();vault=id ();conflict=id ();
  }
  ~authority () { sodium_memzero (secret,sizeof secret); }
  json sign (const char* domain,const json& value) const {
    auto payload=value.dump ();auto message=std::string (domain)+'\0'+payload;
    unsigned char signature[crypto_sign_BYTES];
    crypto_sign_detached (signature,nullptr,reinterpret_cast<const unsigned char*> (message.data ()),message.size (),secret);
    return {{"payload",base64 (reinterpret_cast<const unsigned char*> (payload.data ()),payload.size ())},
      {"signature",base64 (signature,sizeof signature)}};
  }
  json record (std::int64_t version) const {
    auto decision=json{{"vault",vault},{"conflict",conflict},{"version",version},{"created",1},
      {"request_id",conflict},{"branches",subject},{"resolution",version==1 ? epoch : nonce},
      {"member",pin.public_key},{"generation",pin.generation},{"epoch",epoch}};
    return sign ("ATHENA-HODARIUM-DECISION-v1",{{"protocol",1},{"group",pin.group},{"generation",pin.generation},
      {"epoch",epoch},{"challenge",nonce},{"subject",subject},{"decision",decision}});
  }
  decision_evidence page (std::int64_t after,std::int64_t next,std::int64_t watermark,
                          std::int64_t version=0) const {
    auto entries=json::array ();
    if (version) entries.push_back ({{"sequence",next},{"receipt",record (version)}});
    return {sign ("ATHENA-HODARIUM-DECISION-FEED-v1",{{"protocol",1},{"group",pin.group},
      {"generation",pin.generation},{"epoch",epoch},{"challenge",nonce},{"subject",subject},
      {"after",after},{"next",next},{"watermark",watermark},{"more",next<watermark},{"entries",entries}}).dump (),epoch,nonce,subject};
  }
};
}
int main (int argc,char** argv) {
  QCoreApplication application (argc,argv);
  try {
    require (sodium_init ()>=0,"Cannot initialize sodium");
    QTemporaryDir temporary;require (temporary.isValid (),"Cannot create isolated feed directory");
    auto path=std::filesystem::path (temporary.path ().toStdString ())/"feed.sqlite";
    authority signer;
    auto first_conflict=signer.conflict;
    {
      decision_feed_store store (path,signer.pin);
      require (!store.caught_up () && store.cursor ()==0,"New feed trusted as complete");
      require (store.accept (signer.page (0,1,2,1),0),"Intermediate feed lost continuation");
      require (!store.caught_up () && store.pending ().size ()==1,"Intermediate feed incorrectly complete");
    }
    decision_feed_store store (path,signer.pin);
    require (store.cursor ()==1 && !store.caught_up (),"Feed cursor lost across restart");
    signer.conflict=signer.subject;
    require (!store.accept (signer.page (1,2,2,1),1) && store.caught_up (),"Feed catch-up failed");
    auto current=store.latest (signer.vault,signer.conflict);
    require (current && current->decision.version==1 && store.pending ().size ()==2,"Independent scope lost during catch-up");
    store.acknowledge (signer.vault,first_conflict,2);
    require (store.pending ().size ()==2,"Wrong version acknowledgement hid decision");
    store.acknowledge (signer.vault,first_conflict,1);
    require (store.pending ().size ()==1,"Current acknowledgement failed");
    signer.conflict=signer.nonce;
    auto wrong=signer.page (2,3,3,1);wrong.nonce=signer.subject;
    bool rejected=false;
    try { store.accept (wrong,2); } catch (const std::exception&) { rejected=true; }
    require (rejected && store.cursor ()==2,"Unbound feed advanced durable cursor");
    rejected=false;
    try { store.accept (signer.page (2,3,3,2),2); } catch (const std::exception&) { rejected=true; }
    require (rejected && store.cursor ()==2,"Legacy re-adjudication advanced cursor");
    require (!store.accept (signer.page (2,3,3,1),2),"New scope decision failed");
    require (store.pending ().size ()==2 && store.pending ()[1].decision.version==1,
      "New decision did not wake durable pending state");
    require (!store.accept (signer.page (3,3,3),3),"Empty feed failed");
    signer.pin.generation=signer.epoch;
    decision_feed_store recovered (std::filesystem::path (temporary.path ().toStdString ())/"recovered.sqlite",signer.pin);
    require (!recovered.accept (signer.page (0,0,0),0) && recovered.caught_up () && recovered.cursor ()==0,
      "Recovered generation rejected its initial empty feed");
    require (!recovered.accept (signer.page (0,5,5,1),0) && recovered.cursor ()==5 && recovered.pending ().size ()==1,
      "Recovered feed rejected legitimate global sequence gaps");
    std::cout << "Decision feed signatures, cursor recovery, immutable winners and catch-up passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what () << '\n';return 1; }
}
