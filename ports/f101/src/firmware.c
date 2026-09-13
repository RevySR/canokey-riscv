/* SPDX-License-Identifier: Apache-2.0 */
#include "platform.h"
#include <admin.h>
#include <apdu.h>
#include <applets.h>
#include <device.h>
#include <device/dcd.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <tusb.h>
#include <usb_device.h>

struct firmware_status {
  uint64_t magic, stage, loops, error;
  uint64_t mcause, mepc, mtval;
  uint32_t touch_request, mounted, rng_words, log_pos;
  char log[1024];
  uint32_t boot_ms, storage_ms, applets_ms, usb_ms;
  uint64_t cache_control;
  uint32_t applet_ms[7];
};
volatile struct firmware_status firmware_state;
_Static_assert(offsetof(struct firmware_status, mcause) == 32, "trap ABI");
_Static_assert(offsetof(struct firmware_status, mtval) == 48, "trap ABI");
_Static_assert(offsetof(struct firmware_status, boot_ms) == 1096, "boot status ABI");
volatile bool f101_usb_irq_enabled;
static bool usb_started, servicing;
static void (*timeout_callback)(void);
static uint32_t timeout_deadline;
static uint32_t touch_tick;
int f101_ramfs_init(void);
int f101_norfs_init(void);
int f101_identity_init(void);

static uint32_t ticks(void) {
  return (uint32_t)(f101_time() / 24000);
}

void f101_delay_ms(uint32_t ms) {
  uint32_t start = ticks();
  while ((uint32_t)(ticks() - start) < ms) {
  }
}

/* Linker wrappers preserve the pinned core's install order and return values. */
#define PROFILE_INSTALL(name, type, slot)                                                                              \
  extern type __real_##name##_install(uint8_t reset);                                                                  \
  type __wrap_##name##_install(uint8_t reset) {                                                                        \
    uint32_t start = ticks();                                                                                          \
    type result = __real_##name##_install(reset);                                                                      \
    if (firmware_state.stage == 3) firmware_state.applet_ms[slot] = ticks() - start;                                   \
    return result;                                                                                                     \
  }
PROFILE_INSTALL(openpgp, int, 0)
PROFILE_INSTALL(piv, int, 1)
PROFILE_INSTALL(oath, int, 2)
PROFILE_INSTALL(ctap, uint8_t, 3)
PROFILE_INSTALL(admin, int, 4)
PROFILE_INSTALL(ndef, int, 5)
PROFILE_INSTALL(pass, int, 6)

static void report_touch(uint8_t value) {
  touch_tick = ticks();
  set_touch_result(value);
}

static void service(void) {
  if (servicing) return;
  servicing = true;
  if (get_touch_result() != TOUCH_NO && (uint32_t)(ticks() - touch_tick) >= TOUCH_EXPIRE_TIME)
    set_touch_result(TOUCH_NO);
  if (usb_started) {
    if (f101_usb_irq_enabled) dcd_int_handler(0);
    tud_task();
    firmware_state.mounted = tud_mounted();
  }
  /* Explicit UART/debugger input only; no automatic user presence. */
  if (*(volatile uint32_t *)0x02500414 & 1) {
    unsigned ch = *(volatile uint32_t *)0x02500400;
    if (ch == 't') firmware_state.touch_request = TOUCH_SHORT;
    if (ch == 'l') firmware_state.touch_request = TOUCH_LONG;
  }
  uint32_t touch = firmware_state.touch_request;
  if (touch == TOUCH_SHORT || touch == TOUCH_LONG) {
    firmware_state.touch_request = 0;
    report_touch(touch);
  }
  if (timeout_callback && (int32_t)(ticks() - timeout_deadline) >= 0) {
    void (*callback)(void) = timeout_callback;
    timeout_callback = NULL;
    callback();
  }
  servicing = false;
}

void f101_nor_idle(void) {
  service();
}

/* Prime generation can exceed the CCID host timeout. Service USB and its
 * time-extension timer between crypto RNG requests, without recursively
 * dispatching application commands through device_loop(). */
extern int __real_mbedtls_rnd(void *ctx, unsigned char *buf, size_t len);

int __wrap_mbedtls_rnd(void *ctx, unsigned char *buf, size_t len) {
  service();
  int rc = __real_mbedtls_rnd(ctx, buf, len);
  service();
  return rc;
}

uint32_t device_get_tick(void) {
  service();
  return ticks();
}

void device_delay(int ms) {
  if (ms <= 0) return;
  uint32_t start = ticks();
  while ((uint32_t)(ticks() - start) < (uint32_t)ms)
    service();
}

void device_set_timeout(void (*callback)(void), uint16_t timeout) {
  timeout_deadline = ticks() + timeout;
  timeout_callback = callback;
}

void led_on(void) {
}

void led_off(void) {
}

int device_atomic_compare_and_swap(volatile uint32_t *p, uint32_t old, uint32_t value) {
  return __atomic_compare_exchange_n(p, &old, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 0 : -1;
}

int device_spinlock_lock(volatile uint32_t *p, uint32_t blocking) {
  do {
    if (device_atomic_compare_and_swap(p, 0, 1) == 0) return 0;
  } while (blocking);
  return -1;
}

void device_spinlock_unlock(volatile uint32_t *p) {
  __atomic_store_n(p, 0, __ATOMIC_RELEASE);
}

__attribute__((noreturn)) static void fail(uint64_t reason) {
  F101_LOG("Fatal: %08lx\n", (unsigned long)reason);
  firmware_state.error = reason;
  firmware_state.stage = UINT64_MAX;
  for (;;) {
  }
}
#if F101_STORAGE_NOR
#define F101_VERSION "3.0.2-f101-nor-dev"
#define F101_VARIANT "CanoKey F101 YuzukiNeko (SPI NOR)"
#else
#define F101_VERSION "3.0.2-f101-ram-dev"
#define F101_VARIANT "CanoKey F101 YuzukiNeko (RAM)"
#endif
static int admin_string(const CAPDU *capdu, RAPDU *rapdu, const char *s) {
  LL = strlen(s);
  if (LL > LE) LL = LE;
  memcpy(RDATA, s, LL);
  return 0;
}

int admin_vendor_version(const CAPDU *capdu, RAPDU *rapdu) {
  return admin_string(capdu, rapdu, F101_VERSION);
}

int admin_vendor_hw_variant(const CAPDU *capdu, RAPDU *rapdu) {
  return admin_string(capdu, rapdu, F101_VARIANT);
}

int admin_vendor_specific(const CAPDU *capdu, RAPDU *rapdu) {
  (void)capdu;
  EXCEPT(SW_WRONG_P1P2);
}

int admin_vendor_nfc_enable(const CAPDU *capdu, RAPDU *rapdu, bool valid) {
  if (P1 > 1 || P2 > 1 || (P1 == 0 && P2 != 0)) EXCEPT(SW_WRONG_P1P2);
  if (LC != 0) EXCEPT(SW_WRONG_LENGTH);
  if (P1 == 0) {
    if (LE < 1) EXCEPT(SW_WRONG_LENGTH);
    RDATA[0] = 0; /* No NFC frontend on this port. */
    LL = 1;
    return 0;
  }
  if (!valid) EXCEPT(SW_SECURITY_STATUS_NOT_SATISFIED);
  if (P2 != 0) EXCEPT(SW_CONDITIONS_NOT_SATISFIED);
  return 0; /* Disabling absent NFC is an authenticated no-op. */
}

void firmware_main(void) {
  firmware_state.magic = UINT64_C(0x4631303143414e4f);
  firmware_state.stage = 1;
  errno = 0;
  f101_uart_init();
  F101_LOG(F101_VARIANT "\n");
  uint32_t boot_start = ticks();
  *(volatile uint8_t *)0x04100040 &= ~(1u << 6);
  if (f101_rng_init()) fail(0x524e47);
  if (f101_identity_init()) fail(0x554944);
  firmware_state.stage = 2;
  uint32_t storage_start = ticks();
#if F101_STORAGE_NOR
  if (f101_norfs_init()) fail(0x4653);
#else
  if (f101_ramfs_init()) fail(0x4653);
#endif

  firmware_state.storage_ms = ticks() - storage_start;
  firmware_state.stage = 3;
  uint32_t applets_start = ticks();
  set_nfc_state(0);
  device_init();
  applets_install();
  init_apdu_buffer();
  firmware_state.applets_ms = ticks() - applets_start;
  firmware_state.stage = 4;
  uint32_t usb_start = ticks();
  /* Keep a minimum 20 ms detach interval, including initialization time. */
  while ((uint32_t)(ticks() - boot_start) < 20) {
  }
  usb_device_init();
  usb_started = true;
  firmware_state.usb_ms = ticks() - usb_start;
  firmware_state.boot_ms = ticks() - boot_start;
  firmware_state.stage = 5;
  F101_LOG("USB ready: %lu ms\n", (unsigned long)firmware_state.boot_ms);
  for (;;) {
    service();
    device_loop(0);
    device_update_led();
    ++firmware_state.loops;
  }
}

/* Bare-metal libc hooks (newlib and picolibc). */
extern char __heap_start[], __heap_end[];

void *_sbrk(ptrdiff_t amount) {
  static char *end;
  if (!end) end = __heap_start;
  if ((amount > 0 && amount > __heap_end - end) || (amount < 0 && amount < __heap_start - end)) {
    errno = ENOMEM;
    return (void *)-1;
  }
  char *previous = end;
  end += amount;
  return previous;
}

void *sbrk(ptrdiff_t amount) {
  return _sbrk(amount);
}
#ifdef __PICOLIBC__
static int log_putchar(char c, FILE *stream) {
  (void)stream;
  f101_putchar(c);
  firmware_state.log[firmware_state.log_pos++ % sizeof(firmware_state.log)] = c;
  return 0;
}

static FILE log_stream = FDEV_SETUP_STREAM(log_putchar, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &log_stream;
FILE *const stderr = &log_stream;
FILE *const stdin = &log_stream;
#endif
int _write(int fd, const void *data, size_t length) {
  (void)fd;
  const char *p = data;
  for (size_t i = 0; i < length; ++i) {
    f101_putchar(p[i]);
    firmware_state.log[firmware_state.log_pos++ % sizeof(firmware_state.log)] = p[i];
  }
  return length;
}

int _read(int fd, void *data, size_t length) {
  (void)fd;
  (void)data;
  (void)length;
  return 0;
}

int _close(int fd) {
  (void)fd;
  return -1;
}

int _fstat(int fd, struct stat *st) {
  (void)fd;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int fd) {
  (void)fd;
  return 1;
}

int _lseek(int fd, int offset, int whence) {
  (void)fd;
  (void)offset;
  (void)whence;
  return 0;
}

int _getpid(void) {
  return 1;
}

int _kill(int pid, int signal) {
  (void)pid;
  (void)signal;
  errno = EINVAL;
  return -1;
}

void _exit(int status) {
  fail((uint32_t)status);
}
