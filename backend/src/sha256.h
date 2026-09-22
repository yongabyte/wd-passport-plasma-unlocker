#pragma once
#include <stdint.h>
#include <stddef.h>
#include <openssl/evp.h>

// Thin wrapper over system libcrypto (OpenSSL 3 EVP).
// Same API as before so unlock.c and tests are untouched;
// all deprecation/version handling lives in sha256.c.
typedef struct {
  EVP_MD_CTX *ctx;
} WD_SHA256_CTX;

void wd_sha256_init(WD_SHA256_CTX *ctx);
void wd_sha256_update(WD_SHA256_CTX *ctx, const uint8_t *data, size_t len);
void wd_sha256_final(WD_SHA256_CTX *ctx, uint8_t hash[32]);
void wd_sha256(const uint8_t *data, size_t len, uint8_t hash[32]);
