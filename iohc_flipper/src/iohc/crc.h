#pragma once

#include <stdint.h>
#include <stddef.h>

// CRC-16/KERMIT over the iohc frame from Ctrl B1 (inclusive) up to and
// including the byte before the 2-byte CRC trailer. Poly 0x8408 reflected,
// init 0, no final XOR, LSB transmitted first. SUMMARY.md §1.1 / §1.6.
//
// Magic property used for validation: crc(frame || crc_bytes_lsb_first) == 0.
uint16_t iohc_crc16_kermit(const uint8_t* data, size_t len);
