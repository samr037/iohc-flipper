#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Maximum size of the encoded FIFO payload for any iohc frame.
// Worst case: 32-byte iohc frame UART-encoded = 320 bits, +8 bits residue
// for the trailing 8 bits of UART-wrapped 0x33 that follow sync, = 328 bits
// = 41 bytes.
#define IOHC_FIFO_MAX 42

typedef struct {
    uint8_t bytes[IOHC_FIFO_MAX];
    uint8_t len;
} IohcEncodedFrame;

// Build a 1W iohc frame and produce the FIFO-ready bit-packed buffer.
// Output `out` is the on-air stream after the CC1101-emitted sync word.
// On-air alignment:
//   ... preamble (chip) ... sync 0x57FD (chip) ... out.bytes ...
//
// Inputs:
//   src/dst         3-byte addresses, byte 0 first on the wire
//   cmd, data       command id + first payload byte (most cmds have
//                   a 1-byte "data" field; treat as the byte after cmd)
//   extra_payload   any further payload bytes (NULL if none)
//   extra_len       length of extra_payload
//   seq             16-bit sequence counter (we emit MSB-first)
//   install_key     16-byte AES key for this emitter
//   end_frame       set CtrlB1 bit 7
//   start_frame     set CtrlB1 bit 6
//   ctrl_b2         full Ctrl B2 byte (LPM bit etc.)
//
// Returns true on success, false if total iohc frame would exceed 32 bytes.
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
    uint8_t ctrl_b2);

// Build a 0x30 pairing-announce frame.
// Layout (per rspaargaren/iown-home docs/commands.md cmd 0x30):
//   CtrlB1 CtrlB2 dst[3] src[3] cmd(0x30)
//   enc_key[16] man_id ?? seq[2,MSB-first] CRC[2]
// Total = 31 bytes. NO HMAC — pairing is gated by physical button on
// receiver, not per-frame auth.
//
// Caller passes the *cleartext* install_key; this function wraps it with
// the public transfer_key + source-derived IV before placing it on-air.
//
// dst:    Somfy uses 00 00 3F (broadcast). Velux remotes send to product
//         type codes (00 00 BF window, 00 00 FF shutter, 00 03 7F other).
// man_id: vendor marker. Observed values: 0x02 (Somfy), 0x01 (Velux).
// data:   second status byte. Observed values: 0x01 (Somfy), 0x01 (Velux).
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
    uint8_t ctrl_b2);
