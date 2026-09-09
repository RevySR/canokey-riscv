/* SPDX-License-Identifier: Apache-2.0 */
/* SDHCI PIO backend for SDHC/SDXC cards.
 * K230 PHY programming follows the SDK snps_sdhci.c. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "raw_layout.h"
#ifdef K230D_SD_DRIVER
#define firmware_state sd_state
#endif

#include "sd_driver.h"

volatile struct k230d_sd_status firmware_state;
_Static_assert(offsetof(struct k230d_sd_status, retries) == 624, "SD debug ABI");
_Static_assert(offsetof(struct k230d_sd_status, stress_rounds) == 632, "SD stress ABI");
#ifndef SD_PROBE_PORT
#define SD_PROBE_PORT 1
#endif
static const uintptr_t base = 0x91580000 + SD_PROBE_PORT * 0x1000;
#ifndef K230D_SD_CLOCK_HZ
#define K230D_SD_CLOCK_HZ 25000000
#endif
_Static_assert(K230D_SD_CLOCK_HZ >= 400000 && K230D_SD_CLOCK_HZ <= 25000000,
               "Legacy SD mode supports at most 25 MHz");
static void fence(void) { __asm__ volatile("fence iorw,iorw" ::: "memory"); }
static uint32_t r32(unsigned off) { uint32_t v=*(volatile uint32_t *)(base+off); fence(); return v; }
static uint16_t r16(unsigned off) { uint16_t v=*(volatile uint16_t *)(base+off); fence(); return v; }
static uint8_t r8(unsigned off) { uint8_t v=*(volatile uint8_t *)(base+off); fence(); return v; }
static void w32(unsigned off,uint32_t v) { *(volatile uint32_t *)(base+off)=v; fence(); }
static void w16(unsigned off,uint16_t v) { *(volatile uint16_t *)(base+off)=v; fence(); }
static void w8(unsigned off,uint8_t v) { *(volatile uint8_t *)(base+off)=v; fence(); }
static uint32_t now(void) { uint64_t t; __asm__ volatile("rdtime %0":"=r"(t)); return t/27000; }
static void delay(uint32_t ms) { uint32_t t=now(); while ((uint32_t)(now()-t)<ms) {} }
static int wait32(unsigned off,uint32_t mask,bool set,uint32_t ms) {
    uint32_t t=now();
    while (((r32(off)&mask)!=0)!=set) if ((uint32_t)(now()-t)>=ms) return -1;
    return 0;
}
static int wait_interrupt(uint32_t mask) {
    uint32_t t=now(), value;
    do {
        value=r32(0x30);
        firmware_state.interrupt=value;
        if (value&0xffff0000u) return -2;
        if (value&mask) return 0;
    } while ((uint32_t)(now()-t)<1000);
    return -3;
}
static int command(unsigned index,uint32_t arg,unsigned flags) {
    firmware_state.last_command=index;
    if (wait32(0x24,3,false,1000)) return -4;
    w32(0x30,0xffffffff);
    w32(0x08,arg);
    w16(0x0e,(index<<8)|flags);
    int rc=wait_interrupt(1);
    if (rc) return rc;
    for (int i=0;i<4;i++) firmware_state.response[i]=r32(0x10+4*i);
    w32(0x30,1);
    return 0;
}
static int read_sector(uint32_t lba) {
    if (lba>=firmware_state.sectors) return -5;
    w16(4,512); w16(6,1); w16(0x0c,0x10);
    int rc=command(17,lba,0x3a);
    if (rc) return rc;
    if ((rc=wait_interrupt(1u<<5))) return rc;
    if (wait32(0x24,1u<<11,true,1000)) return -19;
    w32(0x30,1u<<5);
    for (int i=0;i<128;i++) {
        uint32_t v=r32(0x20);
        for (int j=0;j<4;j++) firmware_state.sector[4*i+j]=v>>(8*j);
    }
    rc=wait_interrupt(2);
    w32(0x30,2);
    return rc;
}
static int write_sector(uint32_t lba) {
    if (lba>=firmware_state.sectors) return -5;
    w16(4,512); w16(6,1); w16(0x0c,2);
    int rc=command(24,lba,0x3a);
    if (rc) return rc;
    if ((rc=wait_interrupt(1u<<4))) return rc;
    if (wait32(0x24,1u<<10,true,1000)) return -19;
    w32(0x30,1u<<4);
    for (int i=0;i<128;i++) {
        uint32_t v=0;
        for (int j=0;j<4;j++) v|=(uint32_t)firmware_state.sector[4*i+j]<<(8*j);
        w32(0x20,v);
    }
    if ((rc=wait_interrupt(2))) return rc;
    w32(0x30,2);
    if (wait32(0x24,2,false,1000)) return -17;
    if ((rc=command(13,firmware_state.rca,0x1a))) return rc;
    return (firmware_state.response[0]&0xfdffe008u) ? -18 : 0;
}
static int initialize(void) {
    if (!(r32(0x24)&(1u<<16))) return -10;
    w8(0x2f,1);
    uint32_t start=now();
    while (r8(0x2f)&1) if ((uint32_t)(now()-start)>100) return -11;
    /* Only SD0 has this PHY register block; SD1 is the 4-bit controller. */
#if SD_PROBE_PORT == 0
    /* PHY pads at 3.3 V, as in the BPI SDK. No GPIO bank-voltage changes. */
    w16(0x300,0); w16(0x2c,0);
    w16(0x304,0x26a); w16(0x306,0x26a); w16(0x308,0x262);
    w16(0x30a,0x272); w16(0x30c,0x26a);
    w8(0x31d,0x10); w8(0x31e,0x40); w8(0x31d,0);
    w8(0x320,0x0d); w8(0x321,0x0c);
    w32(0x540,r32(0x540)|0x1b0000); w32(0x544,0);
    if (wait32(0x300,2,true,150)) return -12;
    w32(0x300,0x890001);
#endif
    w8(0x28,0); /* 1-bit, legacy speed, PIO */
    w8(0x29,0x0f); /* 3.3V card supply */
    w8(0x2e,0x0e);
    w32(0x34,0xffffffff); w32(0x38,0);
    /* K230 SDK sdhci_init disables vendor command-conflict checking. */
    w8(0x508,0);
    /* SD1 has no programmable-clock multiplier: divided clock mode,
     * 100 MHz / (2 * 125) = 400 kHz identification clock. */
    w16(0x2c,0x7d01);
    start=now();
    while (!(r16(0x2c)&2)) if ((uint32_t)(now()-start)>100) return -13;
    w16(0x2c,r16(0x2c)|4);
    delay(10);
    int rc=command(0,0,0);
    if (rc) return rc;
    if ((rc=command(8,0x1aa,0x1a))) return rc;
    if ((firmware_state.response[0]&0xfff)!=0x1aa) return -14;
    start=now();
    do {
        if ((rc=command(55,0,0x1a))) return rc;
        if ((rc=command(41,0x40ff8000,2))) return rc;
        firmware_state.ocr=firmware_state.response[0];
        if (firmware_state.ocr&0x80000000) break;
        delay(10);
    } while ((uint32_t)(now()-start)<1500);
    firmware_state.card_ready_ms=now()-start;
    if ((firmware_state.ocr&0xc0000000)!=0xc0000000) return -15;
    if ((rc=command(2,0,9))) return rc;
    if ((rc=command(3,0,0x1a))) return rc;
    firmware_state.rca=firmware_state.response[0]&0xffff0000;
    if ((rc=command(9,firmware_state.rca,9))) return rc;
    for (int i=0;i<4;i++) {
        uint32_t v=firmware_state.response[3-i]<<8;
        if (i!=3) v|=firmware_state.response[2-i]>>24;
        firmware_state.csd[i]=v;
    }
    if ((firmware_state.csd[0]>>30)!=1) return -16;
    uint32_t c_size=((firmware_state.csd[1]&0x3f)<<16)|(firmware_state.csd[2]>>16);
    firmware_state.sectors=((uint64_t)c_size+1)*1024;
    if ((rc=command(7,firmware_state.rca,0x1b))) return rc;
    if (wait32(0x24,2,false,1000)) return -17;
    if ((rc=command(55,firmware_state.rca,0x1a))) return rc;
    if ((rc=command(6,2,0x1a))) return rc;
    w8(0x28,2); /* ACMD6 and host agree on the board's four data lines. */
    w16(0x2c,0);
    /* SD1 input is 100 MHz. Round up the divisor so the card clock never
     * exceeds the requested rate. Stay in 3.3 V legacy mode (<=25 MHz),
     * without claiming CMD6 high-speed negotiation or UHS support. */
    unsigned divisor=(100000000u+2u*K230D_SD_CLOCK_HZ-1)/(2u*K230D_SD_CLOCK_HZ);
    w16(0x2c, (divisor<<8) | 1);
    start=now();
    while (!(r16(0x2c)&2)) if ((uint32_t)(now()-start)>100) return -13;
    w16(0x2c,r16(0x2c)|4);
    firmware_state.clock_hz=100000000u/(2u*divisor);
    return 0;
}
int k230d_sd_init(void) {
    *(volatile uint32_t *)0x91108020=1;
    *(volatile uint32_t *)0x91108000=0x69;
    firmware_state.magic=UINT64_C(0x4b32333053445052);
    firmware_state.stage=1;
    uint32_t start=now();
    int rc=initialize();
    if (!rc) { firmware_state.stage=2; rc=read_sector(0); }
    firmware_state.init_ms=now()-start;
    firmware_state.error=(int64_t)rc;
    firmware_state.stage=rc ? UINT64_MAX : 3;
    return rc;
}
uint64_t k230d_sd_sectors(void) { return firmware_state.sectors; }
static int recover(void) {
    /* Same CMD/DAT reset recovery used by the SDK sdhci_send_command. */
    w8(0x2f,6);
    uint32_t start=now();
    while (r8(0x2f)&6) if ((uint32_t)(now()-start)>100) return -20;
    w32(0x30,0xffffffff);
    do {
        delay(2);
        if (command(13,firmware_state.rca,0x1a)) return -21;
        uint32_t status=firmware_state.response[0];
        if ((status&0x1f00)==0x900) return 0; /* ready + TRAN state */
    } while ((uint32_t)(now()-start)<1000);
    return -22;
}
int k230d_sd_read(uint32_t lba, void *buffer) {
    int rc=-1;
    for (int attempt=0;attempt<3;attempt++) {
        rc=read_sector(lba);
        if (!rc) break;
        ++firmware_state.retries;
        if (recover()) break;
    }
    firmware_state.error=(int64_t)rc;
    if (!rc) for (size_t i=0;i<512;i++) ((uint8_t *)buffer)[i]=firmware_state.sector[i];
    return rc;
}
int k230d_sd_write(uint32_t lba, const void *buffer) {
    for (size_t i=0;i<512;i++) firmware_state.sector[i]=((const uint8_t *)buffer)[i];
    int rc=-1;
    for (int attempt=0;attempt<3;attempt++) {
        rc=write_sector(lba);
        if (!rc) break;
        ++firmware_state.retries;
        if (recover()) break;
    }
    firmware_state.error=(int64_t)rc;
    return rc;
}
