/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
/* Run in M-mode with code and stack in SRAM and XuanTie extensions enabled.
 * Return the initialized F101S3 PSRAM size in bytes, or zero on failure. */
uint32_t f101_psram_init(void);
