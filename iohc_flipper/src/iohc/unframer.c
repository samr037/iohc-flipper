#include "unframer.h"

void iohc_unframer_reset(IohcUnframer* u) {
    u->bit_buffer = 0;
    u->bit_count = 0;
    u->skip_bits = 8;  // residue of wrapped 0x33 — see header
    u->expected_len = 0;
    u->frame_pos = 0;
}

static inline IohcUnframerResult try_extract_byte(IohcUnframer* u) {
    while(u->bit_count >= 10) {
        uint16_t group = (uint16_t)(u->bit_buffer >> (u->bit_count - 10)) & 0x3FF;
        u->bit_count -= 10;

        const uint16_t start = (group >> 9) & 1;
        const uint16_t stop = group & 1;
        if(start != 0 || stop != 1) {
            return IohcUnframerFramingError;
        }

        // bits 8..1 = data bits, transmitted LSB-first on the wire.
        // group bit 8 = LSB (d0) ... group bit 1 = MSB (d7).
        uint8_t data = 0;
        for(int i = 0; i < 8; i++) {
            if(group & (1 << (8 - i))) data |= (1 << i);
        }

        if(u->frame_pos >= IOHC_FRAME_MAX_LEN) return IohcUnframerOverflow;
        u->frame[u->frame_pos++] = data;

        // Ctrl B1 = first iohc byte. MsgLen = bits 4..0; total frame = MsgLen + 1 (Ctrl B1) + 2 (CRC).
        // SUMMARY.md §1.2.
        if(u->frame_pos == 1) {
            const uint8_t msglen = data & 0x1F;
            if(msglen < 8) return IohcUnframerFramingError;  // SUMMARY.md §1.2 min
            u->expected_len = (uint8_t)(1 + msglen + 2);
            if(u->expected_len > IOHC_FRAME_MAX_LEN) return IohcUnframerOverflow;
        }

        if(u->expected_len && u->frame_pos >= u->expected_len) {
            return IohcUnframerFrameDone;
        }
    }
    return IohcUnframerNeedBits;
}

IohcUnframerResult iohc_unframer_feed_byte(IohcUnframer* u, uint8_t fifo_byte) {
    if(u->skip_bits >= 8) {
        u->skip_bits -= 8;
        return IohcUnframerNeedBits;
    }
    if(u->skip_bits > 0) {
        // partial skip — should not happen with our 8-aligned FIFO, but be defensive.
        fifo_byte <<= u->skip_bits;
        u->bit_buffer = (u->bit_buffer << (8 - u->skip_bits)) | (fifo_byte >> u->skip_bits);
        u->bit_count += (8 - u->skip_bits);
        u->skip_bits = 0;
    } else {
        u->bit_buffer = (u->bit_buffer << 8) | fifo_byte;
        u->bit_count += 8;
    }
    return try_extract_byte(u);
}
