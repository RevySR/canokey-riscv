/* SPDX-License-Identifier: Apache-2.0 */
#include "platform.h"
#include "psram.h"
#include "spi_nor.h"

static void text(const char *s) {
#if F101_DEBUG
  while (*s)
    f101_putchar(*s++);
#else
  (void)s;
#endif
}

__attribute__((noreturn)) void boot_fault(void) {
  f101_uart_init();
  text("CanoKey SPL: failure\n");
  for (;;) {
  }
}

static uint32_t crc32(const uint8_t *p, uint32_t n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
  }
  return ~crc;
}

void boot_main(void) {
  f101_uart_init();
  text("CanoKey SPL: PSRAM init\n");
  if (!f101_psram_init()) boot_fault();
  text("CanoKey SPL: loading application\n");
  uint8_t id[3];
  uint32_t header[8];
  if (f101_nor_init(id) || f101_nor_read(0x10000, header, sizeof(header))) boot_fault();
  if (header[0] != 0x31464b43 || header[1] != 1 || !header[2] || header[2] > 0x70000 - 32 || header[3] != 0x40010000)
    boot_fault();
  uint8_t *image = (uint8_t *)0x40010000;
  if (f101_nor_read(0x10020, image, header[2]) || crc32(image, header[2]) != header[4]) boot_fault();
  /* Cold boot must never treat uninitialized PSRAM as a host entropy block. */
  *(volatile uint32_t *)0x40fff000 = 0;
  text("CanoKey SPL: starting application\n");
#if F101_DEBUG
  while (!(*(volatile uint32_t *)0x02500414 & 0x40)) {
  }
#endif
  __asm__ volatile("fence rw,rw; fence.i" ::: "memory");
  ((void (*)(void))image)();
  boot_fault();
}
