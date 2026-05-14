#include "hmac_1w.h"
#include "aes.h"
#include <string.h>

// Verbatim port of computeChecksum() from rspaargaren/iohcCryptoHelpers.cpp:92-102.
static void compute_checksum(uint8_t frame_byte, uint8_t* c1, uint8_t* c2) {
    uint8_t tmp = frame_byte ^ *c2;
    uint8_t new_c2 = (uint8_t)(((*c1 & 0x7F) << 1) & 0xFF);
    if(tmp >= 0x80) new_c2 |= 1;
    if((*c1 & 0x80) == 0) {
        *c1 = new_c2;
        *c2 = (uint8_t)((tmp << 1) & 0xFF);
    } else {
        *c1 = (uint8_t)(new_c2 ^ 0x55);
        *c2 = (uint8_t)(((tmp << 1) ^ 0x5B) & 0xFF);
    }
}

void iohc_hmac_1w(
    uint8_t hmac_out[6],
    const uint8_t install_key[16],
    const uint8_t* frame_data,
    size_t frame_data_len,
    const uint8_t sequence_msb_first[2]) {
    uint8_t iv[16];
    memset(iv, 0, 16);
    for(size_t i = 0; i < frame_data_len; i++) {
        compute_checksum(frame_data[i], &iv[8], &iv[9]);
        if(i < 8) iv[i] = frame_data[i];
    }
    for(size_t j = frame_data_len; j < 8; j++) iv[j] = 0x55;
    iv[10] = sequence_msb_first[0];
    iv[11] = sequence_msb_first[1];
    iv[12] = 0x55;
    iv[13] = 0x55;
    iv[14] = 0x55;
    iv[15] = 0x55;
    uint8_t block[16];
    iohc_aes128_encrypt(install_key, iv, block);
    memcpy(hmac_out, block, 6);
}
