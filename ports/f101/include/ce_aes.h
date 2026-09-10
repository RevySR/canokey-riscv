/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

struct f101_ce_aes_status {
  uint32_t clock_hz, blocks, key_loads, errors;
};
extern volatile struct f101_ce_aes_status f101_ce_aes_status;
void f101_ce_aes_forget(void);
