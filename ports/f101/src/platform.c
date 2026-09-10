/* SPDX-License-Identifier: Apache-2.0 */
#include "platform.h"
#include <mbedtls/ctr_drbg.h>
#include <stdio.h>
#include <string.h>
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

/* RAM development only: a fresh host OS seed is required on every load. */
static mbedtls_ctr_drbg_context rng;
static bool seeded;

static int host_entropy(void *ctx, unsigned char *out, size_t len) {
  (void)ctx;
  volatile uint8_t *seed = (volatile uint8_t *)0x40fff000;
  if (REG(0x40fff000) != 0x53454544 || len != 48) return -1;
  for (unsigned i = 0; i < len; i++) {
    out[i] = seed[4 + i];
    seed[4 + i] = 0;
  }
  REG(0x40fff000) = 0;
  return 0;
}

int f101_rng_init(void) {
  mbedtls_ctr_drbg_init(&rng);
  mbedtls_ctr_drbg_set_entropy_len(&rng, 48);
  mbedtls_ctr_drbg_set_nonce_len(&rng, 0);
  int rc = mbedtls_ctr_drbg_seed(&rng, host_entropy, NULL, (const unsigned char *)"F101 RAM", 8);
  mbedtls_ctr_drbg_set_reseed_interval(&rng, 0x7fffffff);
  seeded = rc == 0;
  return rc;
}

uint32_t random32(void) {
  uint32_t result;
  if (!seeded || mbedtls_ctr_drbg_random(&rng, (unsigned char *)&result, sizeof(result))) {
    puts("Random source unavailable");
    for (;;) {
    }
  }
  return result;
}

void f101_trap(uint32_t cause, uint32_t pc, uint32_t value) {
  f101_uart_init();
  printf("Trap cause=%08lx pc=%08lx value=%08lx\n", (unsigned long)cause, (unsigned long)pc, (unsigned long)value);
  for (;;) {
  }
}
