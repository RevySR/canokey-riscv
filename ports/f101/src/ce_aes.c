/* SPDX-License-Identifier: Apache-2.0 */
#include "ce_aes.h"
#include "platform.h"
#include <aes.h>
#include <mbedtls/aes.h>
#include <mbedtls/platform_util.h>
#include <string.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CE(r) REG(0x03040800u + (r))
volatile struct f101_ce_aes_status f101_ce_aes_status;
static uint32_t lock, loaded_key[8], loaded_mode;
static int initialized, key_valid;

static uint32_t load32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32(unsigned char *p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = n >> (8 * i);
}

static void clear_key(void) {
  if (initialized) {
    CE(0) = 0;
    for (unsigned i = 0; i < 8; i++)
      CE(4 + 4 * i) = 0;
    __asm__ volatile("fence iorw,iorw" ::: "memory");
  }
  mbedtls_platform_zeroize(loaded_key, sizeof(loaded_key));
  loaded_mode = 0;
  key_valid = 0;
}

void f101_ce_aes_forget(void) {
  /* A context cannot be freed concurrently with its own operation. */
  if (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE)) return;
  clear_key();
  __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);
}

static void initialize(void) {
  uint32_t pll = REG(0x02001020), clock = 1u << 31, rate = 24000000;
  if ((pll & 0x98000000u) == 0x98000000u) {
    uint32_t source = (uint32_t)(UINT64_C(24000000) * (((pll >> 8) & 255) + 1) / (((pll >> 1) & 1) + 1) /
                                 (((pll >> 16) & 7) + 1) / 2);
    unsigned n = 0, m = (source + 99999999u) / 100000000u;
    while (m > 16 && n < 3) {
      ++n;
      m = (source + 100000000u * (1u << n) - 1) / (100000000u * (1u << n));
    }
    if (m && m <= 16) {
      clock |= (2u << 24) | (n << 8) | (m - 1);
      rate = source / (1u << n) / m;
    }
  }
  REG(0x02001680) = clock;
  REG(0x0200168c) = (REG(0x0200168c) & ~(1u << 16)) | 1u;
  REG(0x0200168c) |= 1u << 16;
  __asm__ volatile("fence iorw,iorw" ::: "memory");
  initialized = 1;
  clear_key();
  f101_ce_aes_status.clock_hz = rate;
}

static int setkey(mbedtls_aes_context *ctx, const unsigned char *key, unsigned bits) {
  if (!ctx || !key) return MBEDTLS_ERR_AES_BAD_INPUT_DATA;
  if (bits != 128 && bits != 192 && bits != 256) return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;
  /* Keep the public context ABI; the port stores a raw key, not round keys.
   * All four mbedTLS key setup/block primitives are replaced together. */
  uint32_t raw[8] = {0};
  for (unsigned i = 0; i < bits / 32; i++)
    raw[i] = load32(key + 4 * i);
  ctx->nr = (int)(bits / 32 + 6);
  ctx->rk = ctx->buf;
  memcpy(ctx->buf, raw, sizeof(raw));
  mbedtls_platform_zeroize(raw, sizeof(raw));
  return 0;
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key, unsigned bits) {
  return setkey(ctx, key, bits);
}

int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx, const unsigned char *key, unsigned bits) {
  return setkey(ctx, key, bits);
}

static int block(mbedtls_aes_context *ctx, const unsigned char input[16], unsigned char output[16], int decrypt) {
  if (!ctx || !input || !output || ctx->rk != ctx->buf || (ctx->nr != 10 && ctx->nr != 12 && ctx->nr != 14))
    return MBEDTLS_ERR_AES_BAD_INPUT_DATA;
  if (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE)) return MBEDTLS_ERR_AES_HW_ACCEL_FAILED;
  if (!initialized) initialize();
  uint32_t mode = 1u | ((unsigned)(ctx->nr - 10) / 2 << 8) | (decrypt ? 128u : 0);
  uint32_t different = loaded_mode ^ mode;
  /* Compare every word: no key-dependent early exit. Comparing the key
   * rather than a context address also handles reused stack contexts. */
  for (unsigned i = 0; i < 8; i++)
    different |= loaded_key[i] ^ ctx->buf[i];
  if (!key_valid || different) {
    CE(0) = 0;
    for (unsigned i = 0; i < 8; i++)
      CE(4 + 4 * i) = loaded_key[i] = ctx->buf[i];
    CE(0) = mode;
    loaded_mode = mode;
    key_valid = 1;
    ++f101_ce_aes_status.key_loads;
  }
  for (unsigned i = 0; i < 4; i++)
    CE(0x200) = load32(input + 4 * i);
  uint64_t start = f101_time();
  while (((CE(0x44) >> 16) & 63) < 4) {
    if (f101_time() - start > 240000) {
      ++f101_ce_aes_status.errors;
      clear_key();
      initialized = 0;
      mbedtls_platform_zeroize(output, 16);
      __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);
      return MBEDTLS_ERR_AES_HW_ACCEL_FAILED;
    }
  }
  for (unsigned i = 0; i < 4; i++)
    store32(output + 4 * i, CE(0x204));
  ++f101_ce_aes_status.blocks;
  __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);
  return 0;
}

int mbedtls_internal_aes_encrypt(mbedtls_aes_context *ctx, const unsigned char in[16], unsigned char out[16]) {
  return block(ctx, in, out, 0);
}

int mbedtls_internal_aes_decrypt(mbedtls_aes_context *ctx, const unsigned char in[16], unsigned char out[16]) {
  return block(ctx, in, out, 1);
}

/* CanoKey's weak software wrappers ignore crypt_ecb errors and omit free.
 * Override them so hardware errors propagate and short-lived keys are wiped. */
static int canokey_aes(const uint8_t *in, uint8_t *out, const uint8_t *key, unsigned bits, int mode) {
  mbedtls_aes_context ctx; /* setkey initializes every field used by this backend. */
  int rc = setkey(&ctx, key, bits);
  if (!rc) rc = mbedtls_aes_crypt_ecb(&ctx, mode, in, out);
  mbedtls_aes_free(&ctx);
  return rc;
}

int aes128_enc(const uint8_t *in, uint8_t *out, const uint8_t *key) {
  return canokey_aes(in, out, key, 128, MBEDTLS_AES_ENCRYPT);
}

int aes128_dec(const uint8_t *in, uint8_t *out, const uint8_t *key) {
  return canokey_aes(in, out, key, 128, MBEDTLS_AES_DECRYPT);
}

int aes256_enc(const uint8_t *in, uint8_t *out, const uint8_t *key) {
  return canokey_aes(in, out, key, 256, MBEDTLS_AES_ENCRYPT);
}

int aes256_dec(const uint8_t *in, uint8_t *out, const uint8_t *key) {
  return canokey_aes(in, out, key, 256, MBEDTLS_AES_DECRYPT);
}
