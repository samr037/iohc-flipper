#pragma once

#include <stdint.h>

// Minimal AES-128 ECB encrypt. Encrypt-only — we don't need decrypt
// because the iohc 1W HMAC only uses encryption.
// Both buffers are 16 bytes. `out` and `in` may overlap.
void iohc_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
