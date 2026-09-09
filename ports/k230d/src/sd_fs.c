/* SPDX-License-Identifier: Apache-2.0 */
#include <fs.h>
#include <string.h>
#include "sd_driver.h"
#include "raw_layout.h"

/* Fixed raw-sector region. No partition table or FAT. No automatic formatting of a
 * damaged filesystem: only an explicit sd_host.py provisioning marker
 * permits the first format. */
static const uint32_t first_sector=K230D_STORAGE_LBA;
static uint8_t read_cache[512], write_cache[512], lookahead[16];
static uint8_t sector[512];
static struct lfs_config config;
/* littlefs repeatedly scans the same directory metadata across applets.
 * Keep read-through copies; writes still reach the card synchronously.
 * Never retain an entry across a write attempt, even a failed one. */
#define SECTOR_CACHE_ENTRIES 64
static struct {
    uint32_t lba, age;
    bool valid;
    uint8_t data[512];
} sector_cache[SECTOR_CACHE_ENTRIES];
static uint32_t cache_age;
volatile struct { uint32_t hits, misses; } fs_cache_stats;
static void invalidate_sector(uint32_t lba) {
    for (unsigned i=0;i<SECTOR_CACHE_ENTRIES;i++)
        if (sector_cache[i].lba==lba) sector_cache[i].valid=false;
}
static int cached_read(uint32_t lba, void *p) {
    unsigned victim=0;
    for (unsigned i=0;i<SECTOR_CACHE_ENTRIES;i++) {
        if (sector_cache[i].valid && sector_cache[i].lba==lba) {
            ++fs_cache_stats.hits;
            sector_cache[i].age=++cache_age;
            memcpy(p,sector_cache[i].data,512);
            return 0;
        }
        if (!sector_cache[i].valid || (sector_cache[victim].valid &&
            (uint32_t)(cache_age-sector_cache[i].age)>
            (uint32_t)(cache_age-sector_cache[victim].age))) victim=i;
    }
    ++fs_cache_stats.misses;
    sector_cache[victim].valid=false;
    if (k230d_sd_read(lba,sector_cache[victim].data)) return LFS_ERR_IO;
    sector_cache[victim].lba=lba;
    sector_cache[victim].age=++cache_age;
    sector_cache[victim].valid=true;
    memcpy(p,sector_cache[victim].data,512);
    return 0;
}
static bool valid(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,lfs_size_t n) {
    return b<c->block_count && off<=4096 && n<=4096-off && !(off%512) && !(n%512);
}
static int read_block(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *p,lfs_size_t n) {
    if (!valid(c,b,off,n)) return LFS_ERR_INVAL;
    uint32_t lba=first_sector+b*8+off/512;
    for (lfs_size_t i=0;i<n;i+=512)
        if (cached_read(lba+i/512,(uint8_t *)p+i)) return LFS_ERR_IO;
    return 0;
}
static int write_block(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *p,lfs_size_t n) {
    if (!valid(c,b,off,n)) return LFS_ERR_INVAL;
    uint32_t lba=first_sector+b*8+off/512;
    for (lfs_size_t i=0;i<n;i+=512) {
        invalidate_sector(lba+i/512);
        if (k230d_sd_write(lba+i/512,(const uint8_t *)p+i)) return LFS_ERR_IO;
    }
    return 0;
}
static int erase_block(const struct lfs_config *c,lfs_block_t b) {
    if (b>=c->block_count) return LFS_ERR_INVAL;
    memset(sector,0xff,sizeof(sector));
    for (unsigned i=0;i<8;i++) {
        invalidate_sector(first_sector+b*8+i);
        if (k230d_sd_write(first_sector+b*8+i,sector)) return LFS_ERR_IO;
    }
    return 0;
}
static int sync_block(const struct lfs_config *c) { (void)c; return 0; }

int k230d_sdfs_init(void) {
    memset(sector_cache,0,sizeof(sector_cache));
    cache_age=0;
    fs_cache_stats.hits=fs_cache_stats.misses=0;
    if (k230d_sd_init()) return LFS_ERR_IO;
    uint32_t length=K230D_STORAGE_SECTORS;
    if ((uint64_t)first_sector+length>k230d_sd_sectors()) return LFS_ERR_INVAL;
    config=(struct lfs_config){
        .read=read_block,.prog=write_block,.erase=erase_block,.sync=sync_block,
        .read_size=512,.prog_size=512,.block_size=4096,.block_count=length/8,
        .block_cycles=1000,.cache_size=512,.lookahead_size=sizeof(lookahead),
        .read_buffer=read_cache,.prog_buffer=write_cache,.lookahead_buffer=lookahead,
    };
    int rc=fs_mount(&config);
    if (!rc) return 0;
    if (k230d_sd_read(first_sector,sector)) return LFS_ERR_IO;
    static const char marker[]="K230D-LFS-FORMAT-v1";
    if (memcmp(sector,marker,sizeof(marker))) return rc;
    rc=fs_format(&config);
    return rc ? rc : fs_mount(&config);
}
