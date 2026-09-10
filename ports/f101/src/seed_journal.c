/* SPDX-License-Identifier: Apache-2.0 */
#include "seed_journal.h"
#include "spi_nor.h"
#include <memzero.h>
#include <sha.h>
#include <string.h>
#define SEED_MAGIC 0x31474e52u

struct record {
  uint32_t magic, generation, consumed;
  uint8_t seed[48], hash[32];
  uint32_t pad;
};

_Static_assert(sizeof(struct record) == 96, "seed record layout");
static unsigned active;
static uint32_t generation;

static void digest(const struct record *r, uint8_t out[32]) {
  uint8_t input[52];
  memcpy(input, &r->generation, 4);
  memcpy(input + 4, r->seed, 48);
  sha256_raw(input, sizeof(input), out);
  memzero(input, sizeof(input));
}

static int valid(const struct record *r) {
  uint8_t hash[32];
  digest(r, hash);
  int result = r->magic == SEED_MAGIC && !memcmp(hash, r->hash, 32);
  memzero(hash, sizeof(hash));
  return result;
}

int f101_seed_take(uint8_t seed[48]) {
  struct record r[2];
  uint8_t id[3];
  if (f101_nor_init(id)) return -1;
  for (unsigned i = 0; i < 2; i++)
    if (f101_nor_read(F101_NOR_SEED_OFFSET + i * 4096, &r[i], sizeof(r[i]))) return -1;
  int ok0 = valid(&r[0]), ok1 = valid(&r[1]);
  if (!ok0 && !ok1) {
    memzero(r, sizeof(r));
    return -1;
  }
  active = ok1 && (!ok0 || r[1].generation > r[0].generation) ? 1 : 0;
  generation = r[active].generation;
  if (r[active].consumed != UINT32_MAX || generation == UINT32_MAX) {
    memzero(r, sizeof(r));
    return -1;
  }
  /* Consume durably before this state can generate any application keys.
   * Never fall back to an older record after an interrupted update. */
  uint32_t consumed = 0;
  int rc = f101_nor_program(F101_NOR_SEED_OFFSET + active * 4096 + 8, &consumed, 4);
  if (!rc) memcpy(seed, r[active].seed, 48);
  memzero(r, sizeof(r));
  return rc;
}

int f101_seed_save(const uint8_t seed[48]) {
  struct record r;
  memset(&r, 0xff, sizeof(r));
  r.magic = SEED_MAGIC;
  r.generation = generation + 1;
  memcpy(r.seed, seed, 48);
  digest(&r, r.hash);
  uint32_t address = F101_NOR_SEED_OFFSET + (active ^ 1u) * 4096;
  int rc = f101_nor_erase(address);
  if (!rc) rc = f101_nor_program(address + 4, (uint8_t *)&r + 4, sizeof(r) - 4);
  if (!rc) rc = f101_nor_program(address, &r.magic, 4); /* Commit last. */
  memzero(&r, sizeof(r));
  return rc;
}
