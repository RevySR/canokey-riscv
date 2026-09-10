/* SPDX-License-Identifier: Apache-2.0 */
/* F101S3 16 MiB type-3 PSRAM controller. */
#include "psram.h"
#include <stdint.h>
#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))

static void modify(uint32_t address, uint32_t keep, uint32_t set) {
  R(address) = (R(address) & keep) | set;
}

static uint32_t ticks(void) {
  uint32_t value;
  __asm__ volatile("rdtime %0" : "=r"(value));
  return value;
}

static int wait_bits(uint32_t address, uint32_t mask, uint32_t expected) {
  uint32_t start = ticks();
  while ((R(address) & mask) != expected)
    if ((uint32_t)(ticks() - start) > 2400000) return -1;
  return 0;
}

static int sid_word(unsigned offset, uint32_t *out) {
  R(0x03090310) = 0x80000000;
  R(0x03090204) = 1;
  R(0x03006004) = offset / 4;
  uint32_t control = R(0x03006000) & 0xfffc;
  R(0x03006000) = control | 0xadbf0002;
  if (wait_bits(0x03006000, 2, 0)) return -1;
  R(0x03006000) = control;
  *out = R(0x0300600c);
  return 0;
}

static void masters(int enable) {
  R(0x03102020) = enable ? 0xffffffffu : 1u;
  R(0x03102024) = enable ? 0xffu : 0;
  R(0x03102028) = enable ? 0xffffu : 0;
}

static int set_clock(void) {
  /* DDR PLL 504 MHz; the PSRAM clock is divided by two. */
  modify(0x02001010, 0xf7ff00fc, 0xc0001400);
  modify(0x02001010, ~(1u << 29), 0);
  modify(0x02001010, ~0u, 1u << 29);
  if (wait_bits(0x02001010, 1u << 28, 1u << 28)) return -1;
  modify(0x02001010, ~0u, 1u << 27);
  modify(0x02001800, 0xf8fffcfc, 0x81000000);
  return 0;
}

static int system_init(void) {
  modify(0x030001f0, ~0u, 1);
  modify(0x02001808, ~(1u << 16), 0);
  modify(0x02001544, ~(1u << 31), 0);
  modify(0x02001540, ~(1u << 30), 0);
  modify(0x0200180c, ~1u, 0);
  modify(0x0200180c, ~(1u << 16), 0);
  modify(0x02001800, ~(1u << 30), 0);
  modify(0x02001800, ~(1u << 31), 0);
  modify(0x02001800, ~0u, 1u << 27);
  if (set_clock()) return -1;
  masters(0);
  modify(0x02001544, ~0u, 0x03000001);
  modify(0x0200180c, ~0u, 1u << 16);
  modify(0x02001540, ~0u, 1u << 30);
  modify(0x02001800, ~0u, 1u << 30);
  modify(0x02001808, ~0u, 1u << 16);
  modify(0x0200180c, ~0u, 1);
  modify(0x02001544, ~0u, 1u << 31);
  modify(0x02001800, ~0u, 1u << 31);
  modify(0x02001800, ~0u, 1u << 27);
  modify(0x0310300c, ~32u, 64);
  modify(0x0310300c, ~0u, 1u << 15);
  return 0;
}

static void vref_init(void) {
  modify(0x03103110, 0x80808080, 0x48484848);
  modify(0x03103114, ~127u, 72);
}

static void bit_delays(void) {
  /* Default board tuning: zero per-bit overrides, hardware training enabled. */
  modify(0x03103100, ~(1u << 26), 0);
  for (unsigned i = 0; i < 8; i++)
    modify(0x03103310 + i * 4, 0xffffc0c0, 0);
  for (unsigned i = 0; i < 8; i++)
    modify(0x03103390 + i * 4, 0xffffc0c0, 0);
  modify(0x03103100, ~(1u << 26), 0);
  for (unsigned pass = 0; pass < 2; pass++) {
    modify(0x03103334, 0xffffc0c0, 0);
    modify(0x03103338, 0xffffc0c0, 0);
    modify(0x031033b4, 0xffffc0c0, 0);
    modify(0x031033b8, 0xffffc0c0, 0);
  }
  modify(0x02053058, 0x8000007f, 0);
  modify(0x0310333c, ~0u, 0);
  modify(0x031033bc, ~0u, 0);
  modify(0x03103100, ~0u, 0x06000000);
  for (uint32_t a = 0x03103240; a < 0x0310327c; a += 4)
    modify(a, ~0u, 0);
  for (uint32_t a = 0x03103228; a < 0x03103240; a += 4)
    modify(a, ~0u, 0);
  modify(0x03103218, ~0u, 0);
  modify(0x0310321c, ~0u, 0);
  modify(0x03103280, ~0u, 0);
}

static int phy_init(void) {
  modify(0x03102008, 0xffffc0ff, 0x1000);
  (void)R(0x03102014);
  R(0x03102014) = 0x2020;
  modify(0x0310300c, ~0u, 0xa020);
  modify(0x0310200c, 0xfffff000, 0x18f);
  modify(0x03102000, 0xffff8fff, 0x08000000);
  modify(0x03103044, ~63u, 0xc3);
  modify(0x03103208, 0xfff80037, 0x10000);
  bit_delays();
  modify(0x03103108, 0xfffff03f, 0x380);
  modify(0x031030bc, ~7u, 0x104);
  modify(0x0310311c, 0x00ffffff, 0);
  modify(0x03103140, 0xf8000000, 0x027bfbfb);
  R(0x03103444) = 0;
  R(0x031034c4) = 0;
  modify(0x031030c0, 0xf0000000, 0x01003087);
  uint32_t request = R(0x03103000);
  R(0x03103000) = request | 0x62;
  R(0x03103000) = request | 0x63;
  if (wait_bits(0x03103010, 1, 1)) return -1;
  uint32_t status = R(0x03103010);
  if (((status >> 20) & 255) && (status & (1u << 20))) return -1;
  if (wait_bits(0x03103018, 1, 1)) return -1;
  modify(0x0310308c, ~0u, 1u << 31);
  modify(0x0310308c, ~(1u << 31), 0);
  modify(0x03102014, ~0u, 1u << 31);
  modify(0x0310310c, 0xf9ffffff, 0);
  uint32_t current = R(0x03102050), mode = R(0x03102000);
  R(0x03102050) = current & ~((mode >> 12) & 2);
  return 0;
}

static void controller_reset(void) {
  (void)R(0x02052210);
  R(0x02052210) = 0xffff0000;
  (void)R(0x0205220c);
  R(0x0205220c) = 0;
  modify(0x02052104, ~15u, 13);
}

static int mr_write(unsigned index, unsigned value) {
  R(0x02052210) = 0xc0c00000;
  R(0x0205220c) = ((index << 16) & 0x00ff0000) | ((value << 8) & 0xffff);
  modify(0x02052104, ~15u, 13);
  return wait_bits(0x02052108, 1, 0);
}

static int controller_init(void) {
  modify(0x0205308c, 0xffffbbff, 0);
  modify(0x0205308c, 0xff3f0000, 0x00c00084);
  modify(0x02052000, ~0u, 7);
  modify(0x0205200c, 0x03ffffff, 0x80000000);
  modify(0x02052100, ~0u, 2);
  (void)R(0x02052118);
  R(0x02052118) = 0x8d95e4cf;
  modify(0x0205211c, ~15u, 0);
  modify(0x02052124, ~4u, 0);
  modify(0x02052130, ~15u, 4);
  modify(0x02052150, 0x80ff80ff, 0x28001600);
  modify(0x02052200, 0x00ffffff, 0x20000000);
  modify(0x02052214, 0xffff8080, 0x516);
  controller_reset();
  /* Serialize reset and the first mode-register command. */
  if (wait_bits(0x02052108, 1, 0)) return -1;
  if (mr_write(0, 0x39) || mr_write(4, 0x60) || mr_write(8, 0x43)) return -1;
  modify(0x0205200c, ~0u, 0x82000000);
  modify(0x02052100, ~0u, 0x100);
  modify(0x02052200, 0x00ffffff, 0x60000000);
  modify(0x0205217c, ~0u, 1);
  R(0x02052180) = 2000;
  modify(0x0205212c, ~8u, 0);
  modify(0x02052154, 0xff000000, 0x00880c05);
  /* Full-width memory access, normal controller mode. */
  modify(0x0205200c, 0x1fffffff, 0x80000000);
  modify(0x03102014, ~0u, 1u << 31);
  masters(1);
  (void)R(0x03103140);
  __asm__ volatile("fence iorw,iorw;.word 0x01b0000b" ::: "memory");
  return 0;
}

static int memory_check(void) {
  volatile uint32_t *low = (void *)0x40000000, *high = (void *)0x40800000;
  for (unsigned i = 0; i < 4096; i++) {
    low[i] = 0x01234567u + i;
    high[i] = 0xfedcba98u + i;
  }
  __asm__ volatile("fence rw,rw" ::: "memory");
  for (unsigned i = 0; i < 4096; i++) {
    uint32_t hi = high[i], lo = low[i];
    if (lo != 0x01234567u + i || hi != 0xfedcba98u + i) {
      return -1;
    }
  }
  return 0;
}

uint32_t f101_psram_init(void) {
  uint32_t chip, type, cache;
  __asm__ volatile("csrr %0,0x7c1" : "=r"(cache));
  __asm__ volatile("csrw 0x7c2,%0;.word 0x01b0000b;fence;fence.i;csrci 0x7c1,2" ::"r"(0x70033u) : "memory");
  uint32_t size = 0;
  if (sid_word(0, &chip) || sid_word(28, &type)) goto out;
  type >>= 26;
  if ((chip & 0xffff) != 0x4300 || (type != 3 && type != 6)) goto out;
  modify(0x03000160, ~2u, 0);
  (void)R(0x03000174);
  R(0x03000174) = 0x19370505;
  (void)R(0x03000174);
  (void)R(0x03000150);
  if (system_init()) goto out;
  vref_init();
  if (phy_init()) goto out;
  if (controller_init()) goto out;
  if (memory_check()) goto out;
  size = 0x01000000;
out:
  __asm__ volatile("csrw 0x7c2,%0;.word 0x01b0000b;fence;fence.i;csrw 0x7c1,%1;fence" ::"r"(0x70033u), "r"(cache)
                   : "memory");
  return size;
}
