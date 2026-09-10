/* SPDX-License-Identifier: Apache-2.0 */
#include "platform.h"
#define REG(a) (*(volatile uint32_t *)(a))

uint64_t f101_time(void) {
  uint32_t hi, lo, check;
  do {
    __asm__ volatile("rdtimeh %0; rdtime %1; rdtimeh %2" : "=&r"(hi), "=&r"(lo), "=&r"(check));
  } while (hi != check);
  return ((uint64_t)hi << 32) | lo;
}

void f101_putchar(char c) {
  if (c == '\n') f101_putchar('\r');
  unsigned timeout = 1000000;
  while (!(REG(0x0250047c) & 2) && --timeout) {
  }
  if (timeout) REG(0x02500400) = (uint8_t)c;
}

void f101_uart_init(void) {
  REG(0x0200190c) |= (1u << 17) | (1u << 1);
  REG(0x02000030) = (REG(0x02000030) & ~0xffu) | 0x44;
  REG(0x02500410) = 3;
  REG(0x0250040c) = 0x80;
  REG(0x02500404) = 0;
  REG(0x02500400) = 13;
  REG(0x0250040c) = 3;
  REG(0x02500408) = 7;
}
