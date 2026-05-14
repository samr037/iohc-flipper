#include "crc.h"

uint16_t iohc_crc16_kermit(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1);
        }
    }
    return crc;
}
