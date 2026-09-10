/* SPDX-License-Identifier: Apache-2.0 */
#include "seed_journal.h"
#include "spi_nor.h"
#include <assert.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
static unsigned char flash[8192];
static int fail_at, operation;

void sha256_raw(const uint8_t *p, size_t n, uint8_t out[32]) {
  SHA256(p, n, out);
}

void memzero(void *p, size_t n) {
  memset(p, 0, n);
}

int f101_nor_init(uint8_t id[3]) {
  id[0] = 0x85;
  id[1] = 0x20;
  id[2] = 0x18;
  return 0;
}

int f101_nor_read(uint32_t a, void *p, size_t n) {
  assert(a >= F101_NOR_SEED_OFFSET && a + n <= F101_NOR_SEED_OFFSET + sizeof(flash));
  memcpy(p, flash + a - F101_NOR_SEED_OFFSET, n);
  return 0;
}

int f101_nor_program(uint32_t a, const void *p, size_t n) {
  assert(a >= F101_NOR_SEED_OFFSET && a + n <= F101_NOR_SEED_OFFSET + sizeof(flash));
  int fail = ++operation == fail_at;
  for (size_t i = 0; i < (fail ? n / 2 : n); i++)
    flash[a - F101_NOR_SEED_OFFSET + i] &= ((const uint8_t *)p)[i];
  return fail ? -1 : 0;
}

int f101_nor_erase(uint32_t a) {
  assert(a == F101_NOR_SEED_OFFSET || a == F101_NOR_SEED_OFFSET + 4096);
  int fail = ++operation == fail_at;
  memset(flash + a - F101_NOR_SEED_OFFSET, 255, fail ? 2048 : 4096);
  return fail ? -1 : 0;
}

static void provision(void) {
  memset(flash, 255, sizeof(flash));
  uint32_t magic = 0x31474e52, generation = 0;
  memcpy(flash, &magic, 4);
  memcpy(flash + 4, &generation, 4);
  for (unsigned i = 0; i < 48; i++)
    flash[12 + i] = i;
  uint8_t input[52];
  memcpy(input, flash + 4, 4);
  memcpy(input + 4, flash + 12, 48);
  SHA256(input, sizeof(input), flash + 60);
  operation = 0;
  fail_at = 0;
}

int main(void) {
  uint8_t seed[48], next[48];
  memset(next, 0xa5, sizeof(next));
  provision();
  assert(!f101_seed_take(seed));
  assert(!f101_seed_save(next));
  assert(!f101_seed_take(seed));
  assert(!memcmp(seed, next, 48));
  assert(f101_seed_take(seed)); /* Consumed state cannot be reused. */
  for (int cut = 1; cut <= 3; cut++) {
    provision();
    assert(!f101_seed_take(seed));
    operation = 0;
    fail_at = cut;
    assert(f101_seed_save(next));
    fail_at = 0;
    assert(f101_seed_take(seed)); /* No old-record fallback. */
  }
  provision();
  fail_at = 1;
  assert(f101_seed_take(seed));
  fail_at = 0;
  assert(f101_seed_take(seed)); /* Torn consumed marker. */
  provision();
  flash[12] ^= 1;
  assert(f101_seed_take(seed));
  puts("PASS: seed rotation, reuse refusal, torn writes and corruption");
}
