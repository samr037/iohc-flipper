#include "key_encrypt.h"
#include "aes.h"

// Public iohc transfer_key constant (rspaargaren/iohcCryptoHelpers.cpp:47).
static const uint8_t IOHC_TRANSFER_KEY[16] = {
    0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
    0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73,
};

void iohc_wrap_install_key(
    uint8_t enc_out[16],
    const uint8_t cleartext_key[16],
    const uint8_t src_addr[3]) {
    uint8_t iv[16] = {0};
    for(int i = 0; i < 13; i += 3) {
        iv[i] = src_addr[0];
        iv[i + 1] = src_addr[1];
        iv[i + 2] = src_addr[2];
    }
    iv[15] = src_addr[0];

    // For a single 16-byte block, AES-CFB128 reduces to:
    //   output = input XOR AES_ECB_encrypt(key, IV)
    // Encrypt and decrypt are identical at one block.
    uint8_t mask[16];
    iohc_aes128_encrypt(IOHC_TRANSFER_KEY, iv, mask);
    for(int i = 0; i < 16; i++) {
        enc_out[i] = cleartext_key[i] ^ mask[i];
    }
}
