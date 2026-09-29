#ifndef AKTUALIZR_OPENSSL_COMPAT_H
#define AKTUALIZR_OPENSSL_COMPAT_H

#include <openssl/opensslv.h>

#ifndef OPENSSL_VERSION_NUMBER
#error "OPENSSL_VERSION_NUMBER is not defined"
#endif

#define AKTUALIZR_OPENSSL_PRE_11 (OPENSSL_VERSION_NUMBER < 0x10100000)

#define AKTUALIZR_OPENSSL_AFTER_11 (!AKTUALIZR_OPENSSL_PRE_11)

// OpenSSL 4.0 removed the ENGINE API: PKCS#11 then goes through libp11 and curl's pkcs11-provider
// support instead. Older versions keep the ENGINE path.
#define AKTUALIZR_OPENSSL_NO_ENGINE (OPENSSL_VERSION_NUMBER >= 0x40000000L)

#endif  // AKTUALIZR_OPENSSL_COMPAT_H
