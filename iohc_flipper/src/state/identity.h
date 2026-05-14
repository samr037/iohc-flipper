#pragma once

#include <stdint.h>
#include <stdbool.h>

// Persistent Flipper-as-iohc-emitter identity. Generated once on first run,
// stored in /ext/apps_data/iohc_flipper/identity.bin (21 bytes).
//
// File layout:
//   [0..2]   src_addr (3 bytes, byte 0 first on the wire)
//   [3..18]  install_key (16 bytes, AES-128)
//   [19..20] seq (2 bytes, MSB-first, monotonically increasing)
//
// src_addr is chosen randomly, avoiding reserved values:
//   00 00 3F = broadcast
//   FF FF FF = broadcast
//   FF FF FE = gateway-originated
// (SUMMARY.md §1.4)

typedef struct {
    uint8_t src_addr[3];
    uint8_t install_key[16];
    uint16_t seq;
} IohcIdentity;

// Load identity from SD; generate + save if file doesn't exist.
// Returns true on success.
bool iohc_identity_load_or_generate(IohcIdentity* out);

// Persist updated identity (call after seq changes).
bool iohc_identity_save(const IohcIdentity* id);

// Convenience: return the next seq to use for TX, increment + save.
uint16_t iohc_identity_next_seq(IohcIdentity* id);
