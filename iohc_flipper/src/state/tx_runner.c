#include "tx_runner.h"
#include "../iohc/frame_build.h"
#include "../radio/radio_cc1101.h"

#include <furi.h>

static const uint8_t BROADCAST_DST[3] = {0x00, 0x00, 0x3F};

// Velux product type codes observed in real-remote rings captures. Real
// KLI 313 remotes send cmd 0x30 to each of these (one burst per type code);
// motors filter and only the type matching their own accepts the frame.
static const uint8_t PAIR_DST_BROADCAST[3] = {0x00, 0x00, 0x3F};  // Somfy
static const uint8_t PAIR_DST_WINDOW[3]    = {0x00, 0x00, 0xBF};  // Velux window (GGL/GGU/CVP)
static const uint8_t PAIR_DST_SHUTTER[3]   = {0x00, 0x00, 0xFF};  // Velux shutter (SSL/SST)
static const uint8_t PAIR_DST_OTHER[3]     = {0x00, 0x03, 0x7F};  // Velux blind/MSL/MSG variants


// Short-form button frame (p0x00_14, msglen=22).
// Payload after [cmd=0x00, data=0x01]: [vendor, button, 0x00, 0x00, 0x00]
// `vendor` is the raw payload[1] byte we captured from the original remote
// (0x43 Somfy, 0x61 Velux, …) — passed through verbatim.
static bool build_button_frame(
    IohcEncodedFrame* out, const IohcIdentity* id, uint8_t vendor,
    uint8_t button_code, uint16_t seq, uint8_t ctrl_b2) {
    uint8_t extra[5] = {vendor, button_code, 0x00, 0x00, 0x00};
    return iohc_frame_build_1w(
        out, id->src_addr, BROADCAST_DST,
        /*cmd=*/0x00, /*data=*/0x01,
        extra, sizeof(extra),
        seq, id->install_key, true, true, ctrl_b2);
}

static bool send_button_with_identity(
    IohcIdentity* id, uint8_t vendor, uint16_t seq, uint8_t button_code) {
    IohcEncodedFrame first, plain;
    if(!build_button_frame(&first, id, vendor, button_code, seq, 0x20)) return false;  // LPM on first
    if(!build_button_frame(&plain, id, vendor, button_code, seq, 0x00)) return false;

    // 6 total frames (1 LPM-marked + 5 plain retransmissions) to match real
    // Smoove behavior — 4 was too low for marginal-RSSI links.
    bool all_ok = iohc_radio_tx_frame(first.bytes, first.len);
    for(uint8_t rep = 0; rep < 5; rep++) {
        furi_delay_ms(14);
        if(!iohc_radio_tx_frame(plain.bytes, plain.len)) all_ok = false;
    }
    return all_ok;
}

// `vendor` is the raw payload[1] byte; required for the STOP+DOWN follow-up
// frames per Velux KLI 313 manual page 10 step 5 ("Appuyer sur la touche
// ARRÊT puis sur la touche DESCENTE dans les 3 secondes"). Somfy motors
// pair on the install-key alone — the trailing button frames are a brief
// stop + slight close, tolerable as a side-effect during pairing.
static bool send_pair_with_identity(
    IohcIdentity* id, uint8_t vendor, uint8_t man_id,
    uint16_t seq39, uint16_t seq30,
    uint16_t seq_stop, uint16_t seq_down) {
    // Sequence #1: 4× cmd 0x39 announce (no payload).
    IohcEncodedFrame ann_lpm, ann_plain;
    if(!iohc_frame_build_1w(
        &ann_lpm, id->src_addr, BROADCAST_DST,
        /*cmd=*/0x39, /*data=*/0x00, NULL, 0,
        seq39, id->install_key, true, true, 0x20)) return false;
    if(!iohc_frame_build_1w(
        &ann_plain, id->src_addr, BROADCAST_DST,
        /*cmd=*/0x39, /*data=*/0x00, NULL, 0,
        seq39, id->install_key, true, true, 0x00)) return false;

    bool all_ok = iohc_radio_tx_frame(ann_lpm.bytes, ann_lpm.len);
    for(uint8_t rep = 0; rep < 3; rep++) {
        furi_delay_ms(14);
        if(!iohc_radio_tx_frame(ann_plain.bytes, ann_plain.len)) all_ok = false;
    }

    // Sequence #2: cmd 0x30 install-key sent to ALL 4 dsts a real KLI 313
    // emits to (broadcast for Somfy + 3 Velux product type codes). Motors
    // filter by their own dst; only the matching type code accepts the
    // frame. man_id is the raw wire byte stored per-device (sniffed or
    // derived at save time — no per-vendor branching here).
    const uint8_t* pair_dsts[4] = {
        PAIR_DST_BROADCAST, PAIR_DST_WINDOW, PAIR_DST_SHUTTER, PAIR_DST_OTHER,
    };
    furi_delay_ms(40);
    for(uint8_t d = 0; d < 4; d++) {
        IohcEncodedFrame ik_lpm, ik_plain;
        if(!iohc_frame_build_pair_0x30(
            &ik_lpm, id->src_addr, pair_dsts[d], id->install_key,
            man_id, /*data=*/0x01,
            seq30, true, true, 0x20)) return false;
        if(!iohc_frame_build_pair_0x30(
            &ik_plain, id->src_addr, pair_dsts[d], id->install_key,
            man_id, /*data=*/0x01,
            seq30, true, true, 0x00)) return false;

        if(!iohc_radio_tx_frame(ik_lpm.bytes, ik_lpm.len)) all_ok = false;
        for(uint8_t rep = 0; rep < 3; rep++) {
            furi_delay_ms(14);
            if(!iohc_radio_tx_frame(ik_plain.bytes, ik_plain.len)) all_ok = false;
        }
        if(d + 1 < 4) furi_delay_ms(20);
    }

    // Sequence #3: STOP then DOWN button frames (Velux registration completion).
    // Fits well inside the 3-sec window the motor opens after APPAIRAGE.
    furi_delay_ms(40);
    if(!send_button_with_identity(id, vendor, seq_stop, IOHC_BTN_STOP)) all_ok = false;
    furi_delay_ms(40);
    if(!send_button_with_identity(id, vendor, seq_down, IOHC_BTN_DOWN)) all_ok = false;

    return all_ok;
}

// ---------- per-device APIs ----------

bool iohc_tx_send_button_dev(IohcDeviceBook* book, uint8_t index, uint8_t button_code) {
    IohcDevice* dev = iohc_device_book_get_mut(book, index);
    if(!dev) return false;
    uint16_t seq = iohc_device_book_next_seq(book, index);
    return send_button_with_identity(&dev->identity, dev->vendor, seq, button_code);
}

bool iohc_tx_send_pair_dev(IohcDeviceBook* book, uint8_t index) {
    IohcDevice* dev = iohc_device_book_get_mut(book, index);
    if(!dev) return false;
    uint16_t seq39    = iohc_device_book_next_seq(book, index);
    uint16_t seq30    = iohc_device_book_next_seq(book, index);
    uint16_t seq_stop = iohc_device_book_next_seq(book, index);
    uint16_t seq_down = iohc_device_book_next_seq(book, index);
    return send_pair_with_identity(&dev->identity, dev->vendor, dev->man_id,
                                   seq39, seq30, seq_stop, seq_down);
}

