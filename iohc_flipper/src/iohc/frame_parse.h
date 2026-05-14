#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Parsed view of an iohc frame. References into the raw bytes; the caller
// keeps the byte buffer alive for the lifetime of the IohcParsedFrame.
typedef struct {
    const uint8_t* raw;
    uint8_t raw_len;

    // Ctrl B1 decoded bits — SUMMARY.md §1.2
    uint8_t ctrl_b1;
    bool end_frame;     // bit 7
    bool start_frame;   // bit 6
    bool one_way;       // bit 5 (1 = 1W, 0 = 2W)
    uint8_t msg_len;    // bits 4..0

    // Ctrl B2 decoded bits — SUMMARY.md §1.3
    uint8_t ctrl_b2;
    bool use_beacon;    // bit 7
    bool routed;        // bit 6
    bool low_power;     // bit 5
    bool ack;           // bit 4
    uint8_t protocol_version;  // bits 1..0

    uint8_t dst[3];     // byte 0 first — SUMMARY.md §1.4
    uint8_t src[3];
    uint8_t cmdid;

    // Payload + 1W suffix split. payload_len = data bytes only.
    const uint8_t* payload;
    uint8_t payload_len;
    uint16_t seq_1w;        // valid only when one_way==true
    const uint8_t* mac_1w;  // 6 bytes, valid only when one_way==true

    uint16_t crc_received;
    uint16_t crc_computed;
    bool crc_ok;
} IohcParsedFrame;

// Returns true on a well-structured frame (length consistent). CRC OK/bad is
// in `out->crc_ok` — a frame with a bad CRC still parses.
bool iohc_frame_parse(const uint8_t* bytes, uint8_t len, IohcParsedFrame* out);
