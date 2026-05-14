#pragma once

#include <stdint.h>

// XOR a 16-byte install key against AES-128-ECB(transfer_key, IV) where IV
// is derived from the source address by tiling: [src0 src1 src2] × 5 + src0.
//
// This is the forward of `encrypt_1W_key` from
// `iohcCryptoHelpers.cpp:174-215`. Used to wrap our own install key for
// transmission in a 0x30 pair frame. Symmetric — applying the same op to the
// output of this function recovers the cleartext (which is how Phase 3
// decrypts captured keys).
void iohc_wrap_install_key(
    uint8_t enc_out[16],
    const uint8_t cleartext_key[16],
    const uint8_t src_addr[3]);
