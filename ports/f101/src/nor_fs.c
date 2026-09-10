/* SPDX-License-Identifier: Apache-2.0 */
#include "spi_nor.h"
#include <fs.h>
#include <stdio.h>
static uint8_t read_cache[LFS_CACHE_SIZE], write_cache[LFS_CACHE_SIZE], lookahead[16];

static int bounds(lfs_block_t b, lfs_off_t o, lfs_size_t n) {
  return b < F101_NOR_STORAGE_SIZE / F101_NOR_SECTOR_SIZE && o <= F101_NOR_SECTOR_SIZE && n <= F101_NOR_SECTOR_SIZE - o;
}

static uint32_t address(lfs_block_t b, lfs_off_t o) {
  return F101_NOR_STORAGE_OFFSET + b * F101_NOR_SECTOR_SIZE + o;
}

static int read_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *p, lfs_size_t n) {
  (void)c;
  if (!bounds(b, o, n)) return LFS_ERR_INVAL;
  return f101_nor_read(address(b, o), p, n) ? LFS_ERR_IO : 0;
}

static int prog_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *p, lfs_size_t n) {
  (void)c;
  if (!bounds(b, o, n)) return LFS_ERR_INVAL;
  return f101_nor_program(address(b, o), p, n) ? LFS_ERR_IO : 0;
}

static int erase_block(const struct lfs_config *c, lfs_block_t b) {
  (void)c;
  if (!bounds(b, 0, F101_NOR_SECTOR_SIZE)) return LFS_ERR_INVAL;
  return f101_nor_erase(address(b, 0)) ? LFS_ERR_IO : 0;
}

static int sync_block(const struct lfs_config *c) {
  (void)c;
  return 0;
}
static const struct lfs_config config = {
    .read = read_block,
    .prog = prog_block,
    .erase = erase_block,
    .sync = sync_block,
    .read_size = 1,
    .prog_size = 8,
    .block_size = F101_NOR_SECTOR_SIZE,
    .block_count = F101_NOR_STORAGE_SIZE / F101_NOR_SECTOR_SIZE,
    .block_cycles = 1000,
    .cache_size = LFS_CACHE_SIZE,
    .lookahead_size = sizeof(lookahead),
    .read_buffer = read_cache,
    .prog_buffer = write_cache,
    .lookahead_buffer = lookahead,
};

int f101_norfs_init(void) {
  uint8_t id[3], block[256];
  if (f101_nor_init(id) || id[0] != 0x85 || id[1] != 0x20 || id[2] != 0x18) return -1;
  printf("SPI NOR: %02x%02x%02x, storage 0x%lx + 0x%lx\n", id[0], id[1], id[2], (unsigned long)F101_NOR_STORAGE_OFFSET,
         (unsigned long)F101_NOR_STORAGE_SIZE);
  /* Only a completely erased storage area may be initialized. A failed
   * mount of existing data is an error, never permission to erase keys. */
  int blank = 1;
  for (uint32_t off = 0; off < F101_NOR_STORAGE_SIZE && blank; off += sizeof(block)) {
    if (f101_nor_read(F101_NOR_STORAGE_OFFSET + off, block, sizeof(block))) return -1;
    for (unsigned i = 0; i < sizeof(block); i++)
      if (block[i] != 0xff) {
        blank = 0;
        break;
      }
  }
  if (blank) {
    puts("Formatting blank NOR storage");
    int rc = fs_format(&config);
    if (rc) return rc;
  }
  return fs_mount(&config);
}
