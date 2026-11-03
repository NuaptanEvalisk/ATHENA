/******************************************************************************
* MODULE     : ios_tls_trust.hpp
* DESCRIPTION: iPadOS system trust verification bridge for GnuTLS
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_IOS_TLS_TRUST_HPP
#define ATHENA_IOS_TLS_TRUST_HPP

#include <gnutls/gnutls.h>

// Verify the peer chain from an in-progress GnuTLS client session using
// iPadOS' system trust policy and hostname validation.  This is intentionally
// separate from GnuTLS' macOS trust-store importer: iOS does not expose its
// root certificates for enumeration.
bool athena_ios_verify_server_trust (gnutls_session_t session,
                                     const char* hostname);

#endif
