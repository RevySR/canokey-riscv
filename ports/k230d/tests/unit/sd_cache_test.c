/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only fake media: exercise the actual littlefs backend callbacks. */
#include <assert.h>
#include <stdio.h>
#include "../../src/sd_fs.c"
static uint8_t media[K230D_STORAGE_SECTORS][512];
static unsigned reads;
static int fail_read, fail_write;
int k230d_sd_init(void) { return 0; }
uint64_t k230d_sd_sectors(void) { return K230D_STORAGE_LBA+K230D_STORAGE_SECTORS; }
int k230d_sd_read(uint32_t lba, void *p) {
    assert(lba>=first_sector && lba<first_sector+K230D_STORAGE_SECTORS);
    ++reads;
    if (fail_read) return -1;
    memcpy(p,media[lba-first_sector],512);
    return 0;
}
int k230d_sd_write(uint32_t lba,const void *p) {
    assert(lba>=first_sector && lba<first_sector+K230D_STORAGE_SECTORS);
    /* A reported failure may still have changed physical media. */
    memcpy(media[lba-first_sector],p,512);
    return fail_write ? -1 : 0;
}
int fs_mount(const struct lfs_config *c) { (void)c; return 0; }
int fs_format(const struct lfs_config *c) { (void)c; assert(0); return -1; }
int main(void) {
    uint8_t out[512],data[512];
    assert(!k230d_sdfs_init());
    assert(!read_block(&config,0,0,out,512));
    assert(!read_block(&config,0,0,out,512) && reads==1);
    memset(data,0xa5,512);
    assert(!write_block(&config,0,0,data,512));
    assert(!read_block(&config,0,0,out,512) && !memcmp(out,data,512));
    fail_write=1; memset(data,0x5a,512);
    assert(write_block(&config,0,0,data,512)==LFS_ERR_IO);
    fail_write=0;
    assert(!read_block(&config,0,0,out,512) && !memcmp(out,data,512));
    assert(!erase_block(&config,0));
    assert(!read_block(&config,0,0,out,512));
    for (unsigned i=0;i<512;i++) assert(out[i]==0xff);
    assert(!k230d_sdfs_init());
    fail_read=1;
    assert(read_block(&config,0,0,out,512)==LFS_ERR_IO);
    fail_read=0;
    assert(!read_block(&config,0,0,out,512));
    unsigned old=reads;
    for (unsigned i=1;i<=SECTOR_CACHE_ENTRIES;i++)
        assert(!read_block(&config,i/8,(i%8)*512,out,512));
    assert(reads==old+SECTOR_CACHE_ENTRIES);
    old=reads;
    assert(!read_block(&config,0,0,out,512) && reads==old+1);
    old=reads;
    assert(read_block(&config,config.block_count,0,out,512)==LFS_ERR_INVAL);
    assert(write_block(&config,0,4096,data,512)==LFS_ERR_INVAL);
    assert(erase_block(&config,config.block_count)==LFS_ERR_INVAL && reads==old);
    assert(!k230d_sdfs_init());
    assert(!read_block(&config,0,0,out,512) && reads==old+1);
    puts("PASS: cache hits, eviction, write/erase invalidation, I/O failures, bounds and reinit");
}
