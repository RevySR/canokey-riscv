/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
int f101_nor_init(uint8_t id[3]);
int f101_nor_read(uint32_t address, void *data, size_t length);
int f101_nor_sfdp(uint32_t address, void *data, size_t length);
#define F101_NOR_STORAGE_OFFSET 0xfc0000u
#define F101_NOR_STORAGE_SIZE 0x40000u
#define F101_NOR_SECTOR_SIZE 4096u
int f101_nor_program(uint32_t address, const void *data, size_t length);
int f101_nor_erase(uint32_t address);
void f101_nor_idle(void);
#define F101_NOR_SEED_OFFSET 0xfbe000u
#define F101_NOR_SEED_SIZE 0x2000u
