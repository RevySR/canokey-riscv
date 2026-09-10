/* SPDX-License-Identifier: Apache-2.0 */
/* Development-only storage: every firmware start erases this RAM disk. */
#include <fs.h>
#include <stdint.h>
#include <string.h>

#define BLOCK_SIZE 1024u
#define BLOCK_COUNT 128u
static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];
static uint8_t read_cache[LFS_CACHE_SIZE], write_cache[LFS_CACHE_SIZE], lookahead[16];

static int bounds(lfs_block_t block, lfs_off_t off, lfs_size_t size) {
  return block < BLOCK_COUNT && off <= BLOCK_SIZE && size <= BLOCK_SIZE - off;
}

static int read_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *p, lfs_size_t n) {
  (void)c;
  if (!bounds(b, o, n)) return LFS_ERR_INVAL;
  memcpy(p, &disk[b][o], n);
  return 0;
}

static int write_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *p, lfs_size_t n) {
  (void)c;
  if (!bounds(b, o, n)) return LFS_ERR_INVAL;
  const uint8_t *in = p;
  for (lfs_size_t i = 0; i < n; ++i)
    disk[b][o + i] &= in[i];
  return 0;
}

static int erase_block(const struct lfs_config *c, lfs_block_t b) {
  (void)c;
  if (b >= BLOCK_COUNT) return LFS_ERR_INVAL;
  memset(disk[b], 0xff, BLOCK_SIZE);
  return 0;
}

static int sync_block(const struct lfs_config *c) {
  (void)c;
  return 0;
}
static const struct lfs_config config = {
    .read = read_block,
    .prog = write_block,
    .erase = erase_block,
    .sync = sync_block,
    .read_size = 1,
    .prog_size = 8,
    .block_size = BLOCK_SIZE,
    .block_count = BLOCK_COUNT,
    .block_cycles = 1000,
    .cache_size = LFS_CACHE_SIZE,
    .lookahead_size = sizeof(lookahead),
    .read_buffer = read_cache,
    .prog_buffer = write_cache,
    .lookahead_buffer = lookahead,
};

int f101_ramfs_init(void) {
  memset(disk, 0xff, sizeof(disk));
  int result = fs_format(&config);
  return result ? result : fs_mount(&config);
}
