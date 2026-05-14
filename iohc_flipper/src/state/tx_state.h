#pragma once

#include <stdint.h>

// Hardcoded credentials for Phase 4 testing. These come from
// PHASE3_RESULTS.md and are validated against captured frames.
// TODO Phase 5: move to encrypted on-device storage.

static const uint8_t IOHC_SOMFY_SRC[3] = {0xA5, 0x8E, 0x29};
static const uint8_t IOHC_SOMFY_KEY[16] = {
    0xBC, 0x78, 0x37, 0x0A, 0xAB, 0x8A, 0xC4, 0x33,
    0xE4, 0x1B, 0x7F, 0x5B, 0x0B, 0x58, 0x18, 0x87,
};
// The Somfy shutter address observed in Phase 2 captures (directed bursts).
static const uint8_t IOHC_SOMFY_MOTOR[3] = {0x00, 0x01, 0xBF};
// The broadcast address — used for short-form button frames.
static const uint8_t IOHC_BROADCAST[3] = {0x00, 0x00, 0x3F};

// Button codes observed at payload byte 2 of short-form broadcast frames.
#define IOHC_BTN_UP    0x00
#define IOHC_BTN_DOWN  0xC8
#define IOHC_BTN_STOP  0xD2

// Vendor byte at payload offset 1.
#define IOHC_VENDOR_SOMFY 0x43

// Shared state for the TX path. Mutex-protected (the consumer thread
// updates last_seq from RX while the TX path reads it).
typedef struct IohcTxState IohcTxState;

IohcTxState* iohc_tx_state_alloc(void);
void iohc_tx_state_free(IohcTxState* s);

// Update from any RX frame matching the Somfy source. Receiver tracks
// per (src) typically; we just keep the global max seen.
void iohc_tx_state_observe_seq(IohcTxState* s, uint16_t observed);

// Return the next seq to use for a TX. Increments internal counter.
uint16_t iohc_tx_state_next_seq(IohcTxState* s);
