/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
int k230d_sd_init(void);
uint64_t k230d_sd_sectors(void);
int k230d_sd_read(uint32_t lba, void *buffer);
int k230d_sd_write(uint32_t lba, const void *buffer);

struct k230d_sd_status {
    uint64_t magic, stage, error, sectors;
    uint64_t mcause, mepc, mtval;
    uint32_t last_command, interrupt, response[4], csd[4], rca, ocr;
    uint32_t request, lba;
    uint8_t sector[512];
    uint32_t retries;
    uint32_t clock_hz;
    uint32_t stress_rounds, stress_done, stress_ms;
    uint32_t init_ms, card_ready_ms;
};

#ifdef K230D_SD_DRIVER
extern volatile struct k230d_sd_status sd_state;
#else
extern volatile struct k230d_sd_status firmware_state;
#endif
