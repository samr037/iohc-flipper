#pragma once

#include <stdint.h>
#include <stddef.h>

// Compute the 6-byte iohc 1W HMAC.
//
// IV construction (rspaargaren/src/iohcCryptoHelpers.cpp:104-138):
//   - iv[0..min(len,8)-1] = frame_data
//   - iv[len..7]          = 0x55 padding (if frame_data shorter than 8)
//   - iv[8..9]            = running checksum across frame_data
//   - iv[10..11]          = sequence number (MSB first)
//   - iv[12..15]          = 0x55 0x55 0x55 0x55
//
// HMAC = AES-128-ECB(install_key, IV)[0..5]
//
// `frame_data` is typically 2 bytes: [cmd, data] where data is the byte
// immediately following the cmd byte in the iohc frame.
void iohc_hmac_1w(
    uint8_t hmac_out[6],
    const uint8_t install_key[16],
    const uint8_t* frame_data,
    size_t frame_data_len,
    const uint8_t sequence_msb_first[2]);
