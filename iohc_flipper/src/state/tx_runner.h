#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "device_book.h"

// Vendor byte values we have observed on-air. Stored only as the raw byte —
// the FAP treats vendor as an opaque pass-through, no per-vendor code paths.
//   0x43 → Somfy   (Phase 2/3 captures)
//   0x61 → Velux   (Phase 5.6 captures)
// These named constants exist only for the v1 → v2 migration fallback and
// for the UI labels. TX code reads dev->vendor directly.
#define IOHC_VENDOR_SOMFY 0x43
#define IOHC_VENDOR_VELUX 0x61

// Button codes at payload byte 2 of short-form broadcast frames.
#define IOHC_BTN_UP    0x00
#define IOHC_BTN_DOWN  0xC8
#define IOHC_BTN_STOP  0xD2

// Send a button-press burst (4 retransmissions) using a saved shutter's
// per-device identity. Always broadcasts (dst = 00 00 3F) — the motor that
// has this identity in its paired list reacts; others ignore.
//
// `book` and `index` together identify the IohcDevice to use; passing them
// instead of an IohcDevice* lets us bump+persist the seq counter inside.
bool iohc_tx_send_button_dev(IohcDeviceBook* book, uint8_t index, uint8_t button_code);

// Send the pairing sequence (4× cmd 0x39 announce + 4× cmd 0x30 install-key
// broadcast) using a saved shutter's identity. Targets whichever motor is
// currently in PROG-accept mode.
bool iohc_tx_send_pair_dev(IohcDeviceBook* book, uint8_t index);
