#include "frame_parse.h"
#include "crc.h"

bool iohc_frame_parse(const uint8_t* b, uint8_t len, IohcParsedFrame* o) {
    if(len < 11) return false;  // SUMMARY.md §1.2 minimum

    o->raw = b;
    o->raw_len = len;

    o->ctrl_b1 = b[0];
    o->end_frame = (b[0] >> 7) & 1;
    o->start_frame = (b[0] >> 6) & 1;
    o->one_way = (b[0] >> 5) & 1;
    o->msg_len = b[0] & 0x1F;
    if(len != (uint8_t)(1 + o->msg_len + 2)) return false;

    o->ctrl_b2 = b[1];
    o->use_beacon = (b[1] >> 7) & 1;
    o->routed = (b[1] >> 6) & 1;
    o->low_power = (b[1] >> 5) & 1;
    o->ack = (b[1] >> 4) & 1;
    o->protocol_version = b[1] & 0x03;

    o->dst[0] = b[2]; o->dst[1] = b[3]; o->dst[2] = b[4];
    o->src[0] = b[5]; o->src[1] = b[6]; o->src[2] = b[7];
    o->cmdid = b[8];

    // payload_len = msg_len - 8 (Ctrl B2 + dst + src + CMDid = 8 bytes) - (1W ? 8 : 0)
    // SUMMARY.md §1.1
    uint8_t suffix = o->one_way ? 8 : 0;
    if(o->msg_len < (uint8_t)(8 + suffix)) return false;
    o->payload_len = o->msg_len - 8 - suffix;
    o->payload = (o->payload_len > 0) ? &b[9] : NULL;

    if(o->one_way) {
        uint8_t seq_off = 9 + o->payload_len;
        // Seq is MSB-first on the wire — empirically verified against burst-to-burst +1
        // increments captured in docs/phase2_first_capture.csv (1E FA → 1E FB → ... → 1E FE).
        o->seq_1w = ((uint16_t)b[seq_off] << 8) | (uint16_t)b[seq_off + 1];
        o->mac_1w = &b[seq_off + 2];
    } else {
        o->seq_1w = 0;
        o->mac_1w = NULL;
    }

    o->crc_received = (uint16_t)b[len - 2] | ((uint16_t)b[len - 1] << 8);
    o->crc_computed = iohc_crc16_kermit(b, len - 2);
    o->crc_ok = (o->crc_received == o->crc_computed);
    return true;
}
