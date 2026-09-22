// SHA-256 via system libcrypto (OpenSSL 3 EVP API, no deprecated calls).
#include "sha256.h"
#include <string.h>

void wd_sha256_init(WD_SHA256_CTX *ctx) {
  ctx->ctx = EVP_MD_CTX_new();
  if (ctx->ctx)
    EVP_DigestInit_ex(ctx->ctx, EVP_sha256(), NULL);
}

void wd_sha256_update(WD_SHA256_CTX *ctx, const uint8_t *data, size_t len) {
  if (ctx->ctx && data)
    EVP_DigestUpdate(ctx->ctx, data, len);
}

void wd_sha256_final(WD_SHA256_CTX *ctx, uint8_t hash[32]) {
  unsigned int outl = 0;
  if (ctx->ctx) {
    EVP_DigestFinal_ex(ctx->ctx, hash, &outl);
    EVP_MD_CTX_free(ctx->ctx);
    ctx->ctx = NULL;
  }
  if (outl != 32)
    memset(hash, 0, 32);
}

void wd_sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
  size_t outl = 0;
  if (data &&
      EVP_Q_digest(NULL, "SHA256", NULL, data, len, hash, &outl) &&
      outl == 32)
    return;
  memset(hash, 0, 32);
}
