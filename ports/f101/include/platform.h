/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#if F101_DEBUG
#include <stdio.h>
#define F101_LOG(...) printf(__VA_ARGS__)
#else
#define F101_LOG(...) ((void)0)
#endif

uint64_t f101_time(void);
void f101_uart_init(void);
void f101_putchar(char c);
int f101_rng_init(void);
void f101_delay_ms(uint32_t ms);
extern volatile bool f101_usb_irq_enabled;
