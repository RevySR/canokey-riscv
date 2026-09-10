#!/usr/bin/env python3
"""Adapt the pinned MIT TinyUSB sunxi MUSB driver without changing the submodule."""
from pathlib import Path
import re
import sys
src, out = map(Path, sys.argv[1:])
s = (src / 'dcd_sunxi_musb.c').read_text()
s = s.replace('CFG_TUSB_MCU == OPT_MCU_F1C100S', 'defined(CANOKEY_F101)')
s = s.replace('#include <f1c100s-irq.h>', '#include "platform.h"')
s = s.replace('#include "bsp/board.h"', '')
a = s.index('static void usb_phy_write')
b = s.index('static unsigned int USBC_WakeUp', a)
s = s[:a] + '''static void delay_ms(uint32_t ms) { f101_delay_ms(ms); }
static void USBC_HardwareReset(void) {
  volatile uint32_t *phy=(volatile uint32_t *)0x02001a70;
  volatile uint32_t *bus=(volatile uint32_t *)0x02001a8c;
  *phy |= (1u<<31)|(1u<<30);
  *bus |= (1u<<8)|(1u<<24);
  __asm__ volatile("fence iorw,iorw" ::: "memory");
  delay_ms(2);
}
static void USBC_PhyConfig(void) {
  volatile uint32_t *ctl=(volatile uint32_t *)0x04100410;
  *ctl=(*ctl & ~((1u<<1)|(1u<<3))) | (1u<<5);
}
static void USBC_ConfigFIFO_Base(void) {
  *(volatile uint32_t *)0x03000004 &= ~((1u<<25)|(1u<<27));
}

''' + s[b:]
s = s.replace('#define USB_FIFO_SIZE_KB 4', '#define USB_FIFO_SIZE_KB 8')
s = re.sub(r'static void usb_isr_handler\(void\) \{.*?\}', '', s, flags=re.S)
s = re.sub(r'  f1c100s_intc_(clear_pend|set_isr)\([^;]+;\n', '', s)
for name, code in [('enable_irq', 'f101_usb_irq_enabled = true'), ('disable_irq', 'f101_usb_irq_enabled = false'), ('mask_irq', '__asm__ volatile("" ::: "memory")'), ('unmask_irq', '__asm__ volatile("" ::: "memory")')]:
    s = re.sub(r'f1c100s_intc_'+name+r'\([^;]+;', code+';', s)
(out / 'dcd_f101.c').write_text(s)
h = (src/'musb_def.h').read_text()
h = re.sub(r'(#define\s+USBC0_BASE\s+)\S+', r'\g<1>0x04100000', h)
(out/'musb_def.h').write_text(h)
