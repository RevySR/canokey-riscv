/* SPDX-License-Identifier: Apache-2.0 */
#include "spi_nor.h"
#include "platform.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define SPI(r) REG(0x04025000u + (r))
#define TX (*(volatile uint8_t *)0x04025200u)
#define RX (*(volatile uint8_t *)0x04025300u)
#define TIMEOUT_TICKS 2400000u
static int writable;

static int wait_clear(unsigned reg, uint32_t mask) {
  uint64_t start = f101_time();
  while (SPI(reg) & mask)
    if (f101_time() - start > TIMEOUT_TICKS) return -1;
  return 0;
}

/* Single-bit command followed by input data. DHB discards command RX.
 * Hardware owns CS for the entire burst; no DMA/cache maintenance needed. */
static int transfer(const uint8_t *tx, size_t nt, uint8_t *rx, size_t nr) {
  if (!nt || nt > 260 || nr > 4096) return -1;
  SPI(0x18) = (1u << 31) | (1u << 15);
  if (wait_clear(0x18, (1u << 31) | (1u << 15))) return -1;
  SPI(0x14) = 0xffffffff;
  SPI(0x08) = (1u << 13) | (1u << 9) | (1u << 8) | (1u << 7) | (1u << 2);
  SPI(0x30) = nt + nr;
  SPI(0x34) = nt;
  SPI(0x38) = nt;
  size_t sent = 0, received = 0;
  while (sent < nt && sent < 64)
    TX = tx[sent++];
  SPI(0x08) |= 1u << 31;
  uint64_t start = f101_time();
  for (;;) {
    while (sent < nt && ((SPI(0x1c) >> 16) & 0xff) < 64)
      TX = tx[sent++];
    while (SPI(0x1c) & 0xff) {
      uint8_t value = RX;
      if (received >= nr) return -1;
      rx[received++] = value;
    }
    uint32_t status = SPI(0x14);
    if (status & ((1u << 8) | (1u << 9) | (1u << 10))) return -1;
    if (status & (1u << 12)) return sent == nt && received == nr ? 0 : -1;
    if (f101_time() - start > TIMEOUT_TICKS) return -1;
  }
}

int f101_nor_init(uint8_t id[3]) {
  writable = 0;
  /* PC0 CLK, PC1 CS, PC2 MISO, PC4 MOSI; WP/HOLD pulled high. */
  REG(0x02000070) |= (1u << 3) | (1u << 5);
  REG(0x02000060) = (REG(0x02000060) & ~0xffffffu) | 0x131333u;
  REG(0x02001940) = (1u << 31) | 1u; /* HOSC / 2 = 12 MHz */
  REG(0x0200196c) |= (1u << 16) | 1u;
  SPI(0x04) = (1u << 31) | 3;
  if (wait_clear(0x04, 1u << 31)) return -1;
  SPI(0x04) = 0x83;
  SPI(0x10) = 0;
  uint8_t command = 0x9f;
  int rc = transfer(&command, 1, id, 3);
  /* PY25Q128HA: 16 MiB, 256-byte pages, 4 KiB erase command 0x20.
   * Do not clear protection bits or unlock unknown devices. */
  writable = !rc && id[0] == 0x85 && id[1] == 0x20 && id[2] == 0x18;
  return rc;
}

static int read_command(uint8_t cmd, uint32_t address, void *data, size_t length) {
  if (address > 0xffffffu || length > 0x1000000u - address) return -1;
  uint8_t *out = data;
  while (length) {
    size_t count = length > 256 ? 256 : length;
    uint8_t command[5] = {cmd, address >> 16, address >> 8, address, 0};
    if (transfer(command, cmd == 0x5a ? 5 : 4, out, count)) return -1;
    address += count;
    out += count;
    length -= count;
  }
  return 0;
}

int f101_nor_read(uint32_t address, void *data, size_t length) {
  return read_command(0x03, address, data, length);
}

int f101_nor_sfdp(uint32_t address, void *data, size_t length) {
  return read_command(0x5a, address, data, length);
}

/* Permit mutations only inside CanoKey's fixed raw storage area. */
static int storage_range(uint32_t address, size_t length) {
  return address >= F101_NOR_STORAGE_OFFSET && address - F101_NOR_STORAGE_OFFSET < F101_NOR_STORAGE_SIZE &&
         length <= F101_NOR_STORAGE_SIZE - (address - F101_NOR_STORAGE_OFFSET);
}

__attribute__((weak)) void f101_nor_idle(void) {
}

static int status(uint8_t *value) {
  const uint8_t command = 0x05;
  return transfer(&command, 1, value, 1);
}

static int ready(void) {
  uint64_t start = f101_time();
  uint8_t value;
  do {
    if (status(&value)) return -1;
    if (!(value & 1)) return 0;
    f101_nor_idle();
  } while (f101_time() - start < 72000000u);
  return -1;
}

static int write_enable(void) {
  const uint8_t command = 0x06;
  uint8_t value;
  if (!writable || ready() || transfer(&command, 1, 0, 0) || status(&value)) return -1;
  return (value & 3) == 2 ? 0 : -1;
}

int f101_nor_program(uint32_t address, const void *data, size_t length) {
  if (!storage_range(address, length)) return -1;
  const uint8_t *in = data;
  while (length) {
    size_t count = 256 - (address & 255);
    if (count > length) count = length;
    uint8_t command[260];
    command[0] = 0x02;
    command[1] = address >> 16;
    command[2] = address >> 8;
    command[3] = address;
    for (size_t i = 0; i < count; i++)
      command[4 + i] = in[i];
    if (write_enable() || transfer(command, 4 + count, 0, 0) || ready()) return -1;
    uint8_t verify[256];
    if (f101_nor_read(address, verify, count)) return -1;
    for (size_t i = 0; i < count; i++)
      if (verify[i] != in[i]) return -1;
    address += count;
    in += count;
    length -= count;
  }
  return 0;
}

int f101_nor_erase(uint32_t address) {
  if ((address & 4095) || !storage_range(address, 4096)) return -1;
  const uint8_t command[4] = {0x20, address >> 16, address >> 8, address};
  if (write_enable() || transfer(command, 4, 0, 0) || ready()) return -1;
  uint8_t verify[256];
  for (unsigned offset = 0; offset < 4096; offset += sizeof(verify)) {
    if (f101_nor_read(address + offset, verify, sizeof(verify))) return -1;
    for (unsigned i = 0; i < sizeof(verify); i++)
      if (verify[i] != 0xff) return -1;
  }
  return 0;
}
