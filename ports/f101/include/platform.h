/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
uint64_t f101_time(void);
void f101_uart_init(void);
void f101_putchar(char c);
int f101_rng_init(void);
void f101_delay_ms(uint32_t ms);
extern volatile bool f101_usb_irq_enabled;
