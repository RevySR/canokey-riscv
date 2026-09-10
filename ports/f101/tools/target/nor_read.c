/* SPDX-License-Identifier: Apache-2.0 */
#include "spi_nor.h"
#include <stdint.h>

uint64_t f101_time(void) {
  uint32_t hi, lo, check;
  do {
    __asm__ volatile("rdtimeh %0; rdtime %1; rdtimeh %2" : "=&r"(hi), "=&r"(lo), "=&r"(check));
  } while (hi != check);
  return ((uint64_t)hi << 32) | lo;
}

void fel_main(void) {
  volatile uint32_t *mail = (volatile uint32_t *)0x27f00;
  uint8_t id[3];
  int result = -1;
  if (mail[1] && mail[1] <= 65536 && !f101_nor_init(id)) result = f101_nor_read(mail[0], (void *)0x40000000, mail[1]);
  mail[2] = result;
}
