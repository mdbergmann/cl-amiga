/*
 * tls_openssl.h — internal interface between platform_posix.c and the
 * dlopen'd OpenSSL TLS backend (tls_openssl.c).
 *
 * Not part of the public platform API: the public entry points are the
 * platform_tls_* functions declared in platform.h, which platform_posix.c
 * implements on top of these.  The AmigaOS/MorphOS builds do not compile
 * this backend — they provide platform_tls_* natively (AmiSSL /
 * openssl3.library inside the bsdsocket reactor).
 */
#ifndef CL_TLS_OPENSSL_H
#define CL_TLS_OPENSSL_H

#include <stdint.h>
#include "platform.h"   /* PlatformTLSParams, PLATFORM_TLS_CERT_* */

typedef struct TLSConn TLSConn;

/* Load the OpenSSL provider on first call (thread-safe, cached).
 * Returns 0 when usable; -1 with a diagnostic in err (NUL-terminated when
 * errlen > 0) when no suitable libssl/libcrypto could be loaded. */
int tls_openssl_available(char *err, uint32_t errlen);

/* Static provider/version string ("OpenSSL 3.2.1 ..."), or NULL. */
const char *tls_openssl_version(void);

/* Run the TLS handshake on the connected TCP fd and return a connection
 * handle, or NULL with a diagnostic in err.  Switches the fd to O_NONBLOCK
 * permanently — from here on ALL I/O on the fd must go through tls_conn_*.
 * All strings in params must point outside the Lisp arena; the caller is
 * expected to bracket this call in a GC safe region (it parks in poll()). */
TLSConn *tls_conn_start(int fd, const PlatformTLSParams *params,
                        char *err, uint32_t errlen);

/* Read up to len plaintext bytes.  >0 = byte count, 0 = clean EOF
 * (close_notify), -1 = error/hard EOF, -2 = timeout (timeout_ms > 0 only;
 * timeout_ms == 0 blocks indefinitely).  Caller brackets a GC safe region. */
int tls_conn_read(TLSConn *c, char *buf, uint32_t len, int timeout_ms);

/* Write len plaintext bytes (all of them).  0 = ok, -1 = error,
 * -2 = timeout.  Caller brackets a GC safe region. */
int tls_conn_write(TLSConn *c, const char *buf, uint32_t len, int timeout_ms);

/* Decrypted bytes already buffered inside the TLS record layer — readable
 * without touching the fd.  Never blocks. */
int tls_conn_pending(TLSConn *c);

/* Non-blocking data probe backing CL:LISTEN once the fd is readable:
 * 1 = a plaintext byte can be read now, 0 = nothing decodable yet (partial
 * record / handshake traffic), 2 = peer closed.  Never blocks. */
int tls_conn_probe(TLSConn *c);

/* Copy a peer-certificate field (PLATFORM_TLS_CERT_*) into out.
 * 0 = ok, -1 = no peer certificate / unsupported field. */
int tls_conn_peer_cert_field(TLSConn *c, int field, char *out, uint32_t outlen);

/* Free the connection.  send_close_notify != 0 attempts one non-blocking
 * SSL_shutdown first (never parks).  The caller must guarantee no other
 * thread still uses the connection (detach-then-free, as with SockSlot). */
void tls_conn_close(TLSConn *c, int send_close_notify);

#endif /* CL_TLS_OPENSSL_H */
