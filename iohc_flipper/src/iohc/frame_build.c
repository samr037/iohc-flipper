#include "frame_build.h"
#include "crc.h"
#include "hmac_1w.h"
#include "key_encrypt.h"

#include <string.h>

// 8-bit residue that must lead the FIFO content: the trailing 8 bits of
// the UART-wrapped iohc 0x33 (d1..d7 + stop) follow the CC1101 sync. The
// sync 0x57FD already covered the preamble tail + wrapped 0xFF + start
// bit + d0 of wrapped 0x33. See PHASE4_PLAN.md "Bit-stream alignment".
//   d1..d7 of 0x33 (LSB-first) = 1, 0, 0, 1, 1, 0, 0
//   stop                       = 1
// MSB-first packed: 1 0 0 1 1 0 0 1 = 0x99 (the same byte rtl_433 hunts for).
#define IOHC_TX_PREFIX_BYTE 0x99

// Bit-pack helper: append `count` bits from `bits` (in LSB-first transmission
// order) into an MSB-first output stream.
typedef struct {
    uint8_t* buf;
    uint16_t bit_count;
    uint16_t bit_capacity;
} BitPacker;

static void bp_push_bit(BitPacker* p, uint8_t bit) {
    uint16_t byte_idx = p->bit_count >> 3;
    uint8_t bit_idx = 7 - (uint8_t)(p->bit_count & 7);
    if(byte_idx >= (p->bit_capacity >> 3)) return;
    if(bit & 1) p->buf[byte_idx] |= (uint8_t)(1u << bit_idx);
    else p->buf[byte_idx] &= (uint8_t)~(1u << bit_idx);
    p->bit_count++;
}

// UART-wrap a single iohc byte: start(0), 8 data bits LSB-first, stop(1).
static void bp_push_uart_byte(BitPacker* p, uint8_t b) {
    bp_push_bit(p, 0);
    for(int i = 0; i < 8; i++) bp_push_bit(p, (uint8_t)((b >> i) & 1));
    bp_push_bit(p, 1);
}

bool iohc_frame_build_1w(
    IohcEncodedFrame* out,
    const uint8_t src[3],
    const uint8_t dst[3],
    uint8_t cmd,
    uint8_t data,
    const uint8_t* extra_payload,
    uint8_t extra_len,
    uint16_t seq,
    const uint8_t install_key[16],
    bool end_frame,
    bool start_frame,
    uint8_t ctrl_b2) {
    // iohc frame layout:
    //   [0]  CtrlB1     (set after we know msg_len)
    //   [1]  CtrlB2
    //   [2..4]  dst
    //   [5..7]  src
    //   [8]  cmd
    //   [9]  data        (always present; called the "data" field of p0x2e etc.)
    //   [10..10+extra_len-1] extra_payload
    //   then 2-byte seq (MSB-first), 6-byte MAC
    //   then 2-byte CRC (LSB first on the wire = byte order in buffer)
    uint8_t frame[32];
    uint8_t msg_len = (uint8_t)(8 + 1 + extra_len + 8);  // header(ex-ctrlb1)+data+extra+1W_suffix
    if(msg_len > 31) return false;
    uint8_t total = (uint8_t)(1 + msg_len + 2);  // +CtrlB1 +CRC
    if(total > 32) return false;

    frame[0] = (uint8_t)(msg_len & 0x1F);
    if(end_frame)   frame[0] |= 0x80;
    if(start_frame) frame[0] |= 0x40;
    frame[0] |= 0x20;  // 1W bit
    frame[1] = ctrl_b2;
    memcpy(&frame[2], dst, 3);
    memcpy(&frame[5], src, 3);
    frame[8] = cmd;
    frame[9] = data;
    if(extra_len > 0) memcpy(&frame[10], extra_payload, extra_len);

    const uint8_t seq_off = (uint8_t)(10 + extra_len);
    frame[seq_off]     = (uint8_t)((seq >> 8) & 0xFF);
    frame[seq_off + 1] = (uint8_t)(seq & 0xFF);

    // HMAC frame_data = bytes from `cmd` up to (and including) the last byte
    // before `seq`. Length depends on command:
    //   pair/remove (0x2E/0x39): 2 bytes (cmd + data)
    //   button (0x00 with p0x00_14): 7 bytes (cmd + origin + acei + main[2] + fp1 + fp2)
    // See iohcRemote1W.cpp:178-184 (pair) and the p0x00_14 path (`toAdd = 6 + 1`).
    // The general rule: HMAC covers cmd + every payload byte before seq.
    uint8_t hmac_data[16];
    uint8_t hmac_len = (uint8_t)(1 + 1 + extra_len);  // cmd + data + extra
    hmac_data[0] = cmd;
    hmac_data[1] = data;
    if(extra_len > 0) memcpy(&hmac_data[2], extra_payload, extra_len);
    uint8_t seq_bytes[2] = {frame[seq_off], frame[seq_off + 1]};
    iohc_hmac_1w(&frame[seq_off + 2], install_key, hmac_data, hmac_len, seq_bytes);

    // CRC-16/KERMIT over CtrlB1 ... last MAC byte. LSB transmitted first.
    uint16_t crc = iohc_crc16_kermit(frame, (size_t)(1 + msg_len));
    frame[1 + msg_len] = (uint8_t)(crc & 0xFF);
    frame[1 + msg_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    // Encode for on-air emission:
    //   FIFO byte 0 = 0x99 (residue after sync)
    //   then UART-wrap each frame byte: start(0) + 8 data LSB-first + stop(1)
    memset(out->bytes, 0, sizeof(out->bytes));
    out->bytes[0] = IOHC_TX_PREFIX_BYTE;
    BitPacker bp = {
        .buf = out->bytes,
        .bit_count = 8,  // residue byte already at offset 0
        .bit_capacity = IOHC_FIFO_MAX * 8,
    };
    for(uint8_t i = 0; i < total; i++) {
        bp_push_uart_byte(&bp, frame[i]);
    }
    // Pad the final partial byte with 1s (idle line state) so the chip
    // doesn't fall back to a bit value that could be misinterpreted by a
    // receiver still sampling.
    while(bp.bit_count & 7) {
        bp_push_bit(&bp, 1);
    }
    out->len = (uint8_t)(bp.bit_count >> 3);
    return true;
}

// Encode pre-built iohc bytes for CC1101 FIFO output (residue + UART-wrap).
static void encode_for_fifo(IohcEncodedFrame* out, const uint8_t* frame, uint8_t total) {
    memset(out->bytes, 0, sizeof(out->bytes));
    out->bytes[0] = IOHC_TX_PREFIX_BYTE;
    BitPacker bp = {
        .buf = out->bytes,
        .bit_count = 8,
        .bit_capacity = IOHC_FIFO_MAX * 8,
    };
    for(uint8_t i = 0; i < total; i++) {
        bp_push_uart_byte(&bp, frame[i]);
    }
    while(bp.bit_count & 7) bp_push_bit(&bp, 1);
    out->len = (uint8_t)(bp.bit_count >> 3);
}

bool iohc_frame_build_pair_0x30(
    IohcEncodedFrame* out,
    const uint8_t src[3],
    const uint8_t dst[3],
    const uint8_t install_key[16],
    uint8_t man_id,
    uint8_t data,
    uint16_t seq,
    bool end_frame,
    bool start_frame,
    uint8_t ctrl_b2) {
    // Layout: ctrlb1 ctrlb2 dst src cmd enc_key[16] man_id data seq[2] crc[2]
    // msg_len = 1+3+3+1+16+1+1+2 = 28
    uint8_t frame[32];
    const uint8_t msg_len = 28;

    frame[0] = (uint8_t)(msg_len & 0x1F);
    if(end_frame)   frame[0] |= 0x80;
    if(start_frame) frame[0] |= 0x40;
    frame[0] |= 0x20;  // 1W
    frame[1] = ctrl_b2;
    memcpy(&frame[2], dst, 3);
    memcpy(&frame[5], src, 3);
    frame[8] = 0x30;

    iohc_wrap_install_key(&frame[9], install_key, src);
    frame[25] = man_id;
    frame[26] = data;
    frame[27] = (uint8_t)((seq >> 8) & 0xFF);
    frame[28] = (uint8_t)(seq & 0xFF);

    uint16_t crc = iohc_crc16_kermit(frame, 1 + msg_len);
    frame[29] = (uint8_t)(crc & 0xFF);
    frame[30] = (uint8_t)((crc >> 8) & 0xFF);

    encode_for_fifo(out, frame, 31);
    return true;
}
