#ifndef VITA_HTTPS_CA_BUNDLE_H
#define VITA_HTTPS_CA_BUNDLE_H

#include <stddef.h>

/* Root CA bundle (PEM, NUL-terminated) for libcurl CURLOPT_CAINFO_BLOB.
 * See ca_bundle.c for provenance and regeneration notes. */
extern const unsigned char ca_bundle_pem[];
extern const size_t ca_bundle_pem_len;

#endif
