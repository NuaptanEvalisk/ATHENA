/******************************************************************************
* MODULE     : ios_tls_trust.mm
* DESCRIPTION: iPadOS Security.framework trust verification for GnuTLS peers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "iOS/ios_tls_trust.hpp"

#import <CoreFoundation/CoreFoundation.h>
#import <Security/Security.h>

bool
athena_ios_verify_server_trust (gnutls_session_t session, const char* hostname) {
  unsigned int peer_count= 0;
  const gnutls_datum_t* peers= gnutls_certificate_get_peers (session, &peer_count);
  if (peers == nullptr || peer_count == 0) return false;

  CFMutableArrayRef certificates=
    CFArrayCreateMutable (kCFAllocatorDefault, (CFIndex) peer_count,
                          &kCFTypeArrayCallBacks);
  if (certificates == nullptr) return false;

  bool complete_chain= true;
  for (unsigned int i= 0; i < peer_count; ++i) {
    CFDataRef data= CFDataCreate (kCFAllocatorDefault,
      reinterpret_cast<const UInt8*> (peers[i].data), (CFIndex) peers[i].size);
    if (data == nullptr) {
      complete_chain= false;
      break;
    }
    SecCertificateRef certificate= SecCertificateCreateWithData (nullptr, data);
    CFRelease (data);
    if (certificate == nullptr) {
      complete_chain= false;
      break;
    }
    CFArrayAppendValue (certificates, certificate);
    CFRelease (certificate);
  }

  if (!complete_chain) {
    CFRelease (certificates);
    return false;
  }

  CFStringRef hostname_string= nullptr;
  if (hostname != nullptr && hostname[0] != '\0') {
    hostname_string= CFStringCreateWithCString (
      kCFAllocatorDefault, hostname, kCFStringEncodingUTF8);
    if (hostname_string == nullptr) {
      CFRelease (certificates);
      return false;
    }
  }

  SecPolicyRef policy= SecPolicyCreateSSL (true, hostname_string);
  if (hostname_string != nullptr) CFRelease (hostname_string);
  if (policy == nullptr) {
    CFRelease (certificates);
    return false;
  }

  SecTrustRef trust= nullptr;
  OSStatus status= SecTrustCreateWithCertificates (certificates, policy, &trust);
  CFRelease (policy);
  CFRelease (certificates);
  if (status != errSecSuccess || trust == nullptr) {
    if (trust != nullptr) CFRelease (trust);
    return false;
  }

  CFErrorRef error= nullptr;
  bool trusted= SecTrustEvaluateWithError (trust, &error);
  if (error != nullptr) CFRelease (error);
  CFRelease (trust);
  return trusted;
}
