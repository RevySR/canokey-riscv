/* SPDX-License-Identifier: Apache-2.0 */
#include "psram.h"

struct fel_boot_result {
  uint32_t magic;
  uint32_t memory_size;
  uint32_t trap_cause;
  uint32_t trap_pc;
};

/* The ROM resumes USB transfers after this SRAM entry initializes PSRAM. */
void fel_boot_main(void) {
  volatile struct fel_boot_result *result = (void *)0x27f00;
  result->magic = 0;
  uint32_t size = f101_psram_init();
  result->memory_size = size;
  result->trap_cause = 0;
  result->trap_pc = 0;
  __asm__ volatile("fence rw,rw" ::: "memory");
  result->magic = 0x5053524d;
}
