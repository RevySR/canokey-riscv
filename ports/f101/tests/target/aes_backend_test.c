/* SPDX-License-Identifier: Apache-2.0 */
#include "ce_aes.h"
#include "platform.h"
#include <aes.h>
#include <block-cipher.h>
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <string.h>
#define REG(a) (*(volatile uint32_t *)(a))

static void text(const char *s) {
  while (*s)
    f101_putchar(*s++);
}

static void number(uint32_t n) {
  char b[11];
  unsigned i = 0;
  do {
    b[i++] = '0' + n % 10;
    n /= 10;
  } while (n);
  while (i)
    f101_putchar(b[--i]);
}

static int entropy(void *ctx, unsigned char *out, size_t n) {
  (void)ctx;
  if (n != 48) return -1;
  for (size_t i = 0; i < n; i++)
    out[i] = i;
  return 0;
}

static int suite(void) {
  const uint8_t input[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                             0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  const uint8_t expected[3][16] = {
      {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a},
      {0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c, 0xdf, 0xe0, 0x6e, 0xaf, 0x70, 0xa0, 0xec, 0x0d, 0x71, 0x91},
      {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf, 0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89}};
  uint8_t key[32], out[65], iv[16], plain[64];
  for (unsigned i = 0; i < 32; i++)
    key[i] = i;
  for (unsigned i = 0; i < 16; i++)
    iv[i] = i;
  for (unsigned i = 0; i < 64; i++)
    plain[i] = i;
  mbedtls_aes_context contexts[3];
  for (unsigned i = 0; i < 3; i++) {
    mbedtls_aes_init(&contexts[i]);
    if (mbedtls_aes_setkey_enc(&contexts[i], key, 128 + i * 64)) return 1;
  }
  for (unsigned round = 0; round < 8; round++)
    for (unsigned i = 0; i < 3; i++) {
      memcpy(out + 1, input, 16);
      if (mbedtls_aes_setkey_enc(&contexts[i], key, 128 + i * 64) ||
          mbedtls_aes_crypt_ecb(&contexts[i], MBEDTLS_AES_ENCRYPT, out + 1, out + 1) ||
          memcmp(out + 1, expected[i], 16))
        return 2;
      if (mbedtls_aes_setkey_dec(&contexts[i], key, 128 + i * 64) ||
          mbedtls_aes_crypt_ecb(&contexts[i], MBEDTLS_AES_DECRYPT, out + 1, out + 1) || memcmp(out + 1, input, 16))
        return 3;
    }
  if (mbedtls_aes_setkey_enc(&contexts[0], key, 129) != MBEDTLS_ERR_AES_INVALID_KEY_LENGTH) return 4;
  for (unsigned i = 0; i < 3; i++)
    mbedtls_aes_free(&contexts[i]);
#if F101_CE_AES
  if (REG(0x03040800)) return 5;
  for (unsigned i = 0; i < 8; i++)
    if (REG(0x03040804 + 4 * i)) return 6;
#endif
  text("PASS: AES128/192/256, context switching, direction, in-place/unaligned, key cleanup\n");
  static const uint8_t cbc_expected[64] = {0xf2, 0x90, 0x00, 0xb6, 0x2a, 0x49, 0x9f, 0xd0, 0xa9, 0xf3, 0x9a, 0x6a, 0xdd,
                                           0x2e, 0x77, 0x80, 0x95, 0x43, 0xb8, 0x6f, 0xc0, 0x46, 0xfa, 0x88, 0x3a, 0x94,
                                           0x46, 0xb8, 0x2e, 0x47, 0xd1, 0x2d, 0xa1, 0x44, 0xfc, 0x25, 0x5a, 0xad, 0x45,
                                           0xbf, 0x68, 0x1d, 0x3a, 0x37, 0x73, 0xa3, 0x25, 0xc2, 0x93, 0x68, 0x8f, 0x47,
                                           0xda, 0xdb, 0xc9, 0xa6, 0xe1, 0xad, 0xca, 0xae, 0x6a, 0x1e, 0x3b, 0xd7};
  static const uint8_t ctr_expected[64] = {0x5a, 0x6f, 0x06, 0x54, 0x0c, 0xfe, 0x77, 0x91, 0xf8, 0x27, 0x5f, 0x36, 0x0e,
                                           0xce, 0xa8, 0x9d, 0x70, 0xe2, 0x02, 0xc6, 0xd7, 0x90, 0x4e, 0x4a, 0x4d, 0x0f,
                                           0xe1, 0x4a, 0x6e, 0xf8, 0x3e, 0xd0, 0x3c, 0x44, 0x55, 0x78, 0x1e, 0xe0, 0xea,
                                           0x83, 0x93, 0xc8, 0x21, 0x8e, 0xc9, 0x3c, 0xe9, 0xbd, 0xa6, 0xfa, 0x35, 0x41,
                                           0x1c, 0xa5, 0x91, 0xd8, 0x51, 0x79, 0x67, 0x2b, 0xc5, 0x59, 0xd3, 0x62};
  for (unsigned i = 0; i < 2; i++) {
    block_cipher_config cfg = {.mode = i ? CTR : CBC,
                               .in = plain,
                               .in_size = 64,
                               .out = out + 1,
                               .iv = iv,
                               .key = key,
                               .block_size = 16,
                               .encrypt = aes256_enc,
                               .decrypt = aes256_dec};
    if (block_cipher_enc(&cfg) || memcmp(out + 1, i ? ctr_expected : cbc_expected, 64)) return 7;
    cfg.in = out + 1;
    if (block_cipher_dec(&cfg) || memcmp(out + 1, plain, 64)) return 8;
  }
  text("PASS: CanoKey AES256 CBC/CTR vectors and in-place decrypt\n");
  mbedtls_ctr_drbg_context rng;
  mbedtls_ctr_drbg_init(&rng);
  mbedtls_ctr_drbg_set_entropy_len(&rng, 48);
  mbedtls_ctr_drbg_set_nonce_len(&rng, 0);
  if (mbedtls_ctr_drbg_seed(&rng, entropy, 0, (const unsigned char *)"F101 AES test", 13)) return 9;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts_ret(&sha, 0)) return 10;
  for (unsigned i = 0; i < 1000; i++) {
    if (mbedtls_ctr_drbg_random(&rng, out, 4)) return 11;
    if (mbedtls_sha256_update_ret(&sha, out, 4)) return 12;
    /* An unrelated context changes the CE key between DRBG calls. */
    if (aes128_enc(input, out + 16, key) || memcmp(out + 16, expected[0], 16)) return 13;
  }
  uint8_t digest[32];
  if (mbedtls_sha256_finish_ret(&sha, digest)) return 14;
  mbedtls_sha256_free(&sha);
  mbedtls_ctr_drbg_free(&rng);
  static const uint8_t expected_digest[32] = {0xce, 0xdf, 0x7b, 0x8a, 0x9f, 0x3f, 0x8e, 0x84, 0x40, 0x17, 0x39,
                                              0x16, 0x82, 0x83, 0x3b, 0x64, 0x13, 0x3d, 0xd9, 0x5f, 0x01, 0x51,
                                              0xd9, 0xc6, 0x59, 0x81, 0x4c, 0x14, 0xf7, 0xc7, 0x90, 0xb7};
  if (memcmp(digest, expected_digest, sizeof(digest))) return 16;
  text("PASS: CTR_DRBG known-answer test\n");
#if F101_CE_AES
  if (!f101_ce_aes_status.blocks || f101_ce_aes_status.errors) return 15;
#endif
  return 0;
}

void aes_test_main(void) {
  extern uint32_t __bss_start[], __bss_end[];
  for (uint32_t *p = __bss_start; p < __bss_end; p++)
    *p = 0;
  f101_uart_init();
  text("\nAES backend test hardware=");
  number(F101_CE_AES);
  text("\n");
  uint32_t h, x, t;
  __asm__ volatile("csrr %0,0x7c1;csrr %1,0x7c0;csrr %2,0x7c5" : "=r"(h), "=r"(x), "=r"(t));
  __asm__ volatile("csrw 0x7c0,%0;csrw 0x7c1,%1;csrw 0x7c5,%2;fence.i" ::"r"(x | 0x408000), "r"(0x11ff), "r"(0x16e30c)
                   : "memory");
  int rc = suite();
#if F101_CE_AES
  f101_ce_aes_forget();
#endif
  __asm__ volatile("csrw 0x7c2,%0;fence;fence.i;csrw 0x7c1,%1;csrw 0x7c5,%2;csrw 0x7c0,%3" ::"r"(0x70033), "r"(h),
                   "r"(t), "r"(x)
                   : "memory");
  text("AES backend result=");
  number(rc);
  text("\nReturning to FEL\n");
  while (!(REG(0x02500414) & 0x40)) {
  }
}

void _exit(int status) {
  text("AES backend abort=");
  number((uint32_t)status);
  text("\n");
  for (;;) {
  }
}
