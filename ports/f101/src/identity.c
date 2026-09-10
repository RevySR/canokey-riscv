/* SPDX-License-Identifier: Apache-2.0 */
#include <admin.h>
#include <memzero.h>
#include <sha.h>
#include <stdint.h>
#include <string.h>

static uint8_t identity_digest[32];

int f101_identity_init(void) {
  static const char domain[] = "CanoKey F101 serial v1";
  uint8_t input[sizeof(domain) + 16];
  uint32_t any = 0, all = UINT32_MAX;
  memcpy(input, domain, sizeof(domain));
  for (unsigned i = 0; i < 4; i++) {
    volatile uint32_t *sid = (volatile uint32_t *)0x03006000;
    sid[1] = i;
    sid[0] = (sid[0] & 0xfffc) | 0xadbf0002;
    unsigned timeout = 1000000;
    while ((sid[0] & 2) && --timeout) {
    }
    if (!timeout) return -1;
    sid[0] &= 0xfffc;
    uint32_t value = sid[3];
    any |= value;
    all &= value;
    for (unsigned j = 0; j < 4; j++)
      input[sizeof(domain) + i * 4 + j] = value >> (24 - j * 8);
  }
  if (!any || all == UINT32_MAX) return -1;
  sha256_raw(input, sizeof(input), identity_digest);
  memzero(input, sizeof(input));
  return 0;
}

void f101_fill_sn(uint8_t *buf) {
  memcpy(buf, identity_digest, 4);
}

int admin_vendor_hw_sn(const CAPDU *capdu, RAPDU *rapdu) {
  if (LE < sizeof(identity_digest)) EXCEPT(SW_WRONG_LENGTH);
  memcpy(RDATA, identity_digest, sizeof(identity_digest));
  LL = sizeof(identity_digest);
  return 0;
}
