/* SPDX-License-Identifier: Apache-2.0 */
#include "platform.h"
#include "spi_nor.h"

void fel_main(void) {
  volatile uint32_t *m = (volatile uint32_t *)0x27f00;
  uint8_t id[3];
  int rc = -1;
  if (m[0] == 0x49313046 && !f101_nor_init(id)) {
    if (m[1] == 1 && m[3] == 4096) rc = f101_nor_erase(m[2]);
    if (m[1] == 2 && m[3] <= 4096) rc = f101_nor_program(m[2], (const void *)0x40000000, m[3]);
    if (m[1] == 3 && m[3] <= 4096) rc = f101_nor_read(m[2], (void *)0x40000000, m[3]);
  }
  m[4] = rc;
}
