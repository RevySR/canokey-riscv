/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Whole-card byte layout, no MBR/GPT/FAT. Firmware update image ends at 2 MiB. */
#define K230D_BOOT_IMAGE_BYTES 2097152
#define K230D_STORAGE_LBA 8192
#define K230D_STORAGE_SECTORS 2048
