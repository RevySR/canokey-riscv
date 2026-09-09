/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <string.h>
#include <admin.h>
#include <sha.h>
#include <memzero.h>

static uint8_t identity_digest[32];

int k230d_identity_init(void) {
    /* SDK pufs_rt_regs: public PUFSLOT_0 at RT + 0x300. The SDK exports
     * this 256-bit UID in big-endian word order. Never access slots 1..3,
     * enroll/zeroize PUF, or program OTP as part of identity generation. */
    const volatile uint32_t *uid=(const volatile uint32_t *)0x91213300;
    static const char domain[]="CanoKey K230D serial v1";
    uint8_t input[sizeof(domain)+32];
    uint32_t any=0, all=UINT32_MAX;
    memcpy(input,domain,sizeof(domain));
    for (unsigned i=0;i<8;i++) {
        uint32_t value=uid[i];
        __asm__ volatile("fence iorw,iorw" ::: "memory");
        if (value!=uid[i]) { memzero(input,sizeof(input)); return -1; }
        any|=value; all&=value;
        for (unsigned j=0;j<4;j++)
            input[sizeof(domain)+i*4+j]=(uint8_t)(value>>(24-j*8));
    }
    if (!any || all==UINT32_MAX) { memzero(input,sizeof(input)); return -1; }
    sha256_raw(input,sizeof(input),identity_digest);
    memzero(input,sizeof(input));
    return 0;
}

void k230d_fill_sn(uint8_t *buf) { memcpy(buf,identity_digest,4); }

int admin_vendor_hw_sn(const CAPDU *capdu, RAPDU *rapdu) {
    if (LE<sizeof(identity_digest)) EXCEPT(SW_WRONG_LENGTH);
    memcpy(RDATA,identity_digest,sizeof(identity_digest));
    LL=sizeof(identity_digest);
    return 0;
}
