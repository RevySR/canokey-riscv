/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Measured BPI K230D Zero: GHWCFG2=228f5852, GHWCFG3=0bc884e8.
 * Six non-control endpoints, 3016 FIFO words. No DMA; L1 caches disabled. */
#define DWC2_EP_MAX 7
static const dwc2_controller_t _dwc2_controller[] = {
    {.reg_base = 0x91500000, .irqnum = 0, .ep_count = DWC2_EP_MAX,
     .ep_fifo_size = 3016 * 4}
};
extern volatile bool k230d_usb_irq_enabled;
void k230d_delay_ms(uint32_t ms);
void k230d_wait_bits(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t timeout);
static inline void dwc2_dcd_int_enable(uint8_t port) {
    (void)port;
    k230d_usb_irq_enabled = true;
}
static inline void dwc2_dcd_int_disable(uint8_t port) {
    (void)port;
    k230d_usb_irq_enabled = false;
}
static inline void dwc2_remote_wakeup_delay(void) { k230d_delay_ms(1); }
static inline void dwc2_phy_init(dwc2_regs_t *dwc2, uint8_t type) {
    (void)type;
    /* BootROM leaves internal DMA enabled. This driver uses slave FIFO I/O. */
    dwc2->gahbcfg &= ~GAHBCFG_DMAEN;
}
static inline void dwc2_phy_update(dwc2_regs_t *dwc2, uint8_t type) {
    (void)dwc2;
    (void)type;
}
