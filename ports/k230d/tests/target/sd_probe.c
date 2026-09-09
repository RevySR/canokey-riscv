/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_driver.h"
#include "raw_layout.h"
static uint32_t now(void) {
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t/27000;
}

static uint8_t stress_buffer[512];
static uint8_t pattern(unsigned round, unsigned sector, unsigned byte) {
    uint32_t x=(round+1)*0x9e3779b9u ^ sector*0x85ebca6bu ^ byte*0xc2b2ae35u;
    x^=x>>16; x*=0x7feb352du; x^=x>>15;
    return x;
}
static int stress(void) {
    /* Only the existing 16-sector scratch area, never boot or key storage. */
    const uint32_t first=K230D_STORAGE_LBA+K230D_STORAGE_SECTORS;
    unsigned rounds=firmware_state.stress_rounds;
    if (!rounds || rounds>1024 || first+16>firmware_state.sectors) return -23;
    uint32_t start=now();
    firmware_state.stress_done=0;
    for (unsigned r=0;r<rounds;r++) {
        for (unsigned s=0;s<16;s++) {
            for (unsigned b=0;b<512;b++) stress_buffer[b]=pattern(r,s,b);
            int rc=k230d_sd_write(first+s,stress_buffer);
            if (rc) return rc;
        }
        for (unsigned s=0;s<16;s++) {
            for (unsigned b=0;b<512;b++) stress_buffer[b]=0;
            int rc=k230d_sd_read(first+s,stress_buffer);
            if (rc) return rc;
            for (unsigned b=0;b<512;b++)
                if (stress_buffer[b]!=pattern(r,s,b)) return -24;
        }
        firmware_state.stress_done=r+1;
    }
    firmware_state.stress_ms=now()-start;
    return 0;
}
void firmware_main(void) {
    int rc=k230d_sd_init();
    for (;;) {
        uint32_t request=firmware_state.request;
        if (request>=1 && request<=3) {
            firmware_state.stage=4;
            rc=request==3 ? stress() : request==1 ?
                k230d_sd_read(firmware_state.lba,(void *)firmware_state.sector) :
                k230d_sd_write(firmware_state.lba,(const void *)firmware_state.sector);
            firmware_state.error=(int64_t)rc;
            firmware_state.stage=rc ? UINT64_MAX : 3;
            firmware_state.request=0;
        }
    }
}
