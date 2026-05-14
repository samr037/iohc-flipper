#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "identity.h"

#define IOHC_DEVICE_NAME_MAX 24
#define IOHC_DEVICE_BOOK_MAX 16

// One saved shutter. The motor_addr is the on-air NodeID we sniffed from a
// real remote — used as a label, since iohc NodeIDs are not globally unique
// across same-model motors. Per-shutter (src, install_key, seq) identity
// gives us true isolation: pair each identity with one motor, the motor
// only acts on broadcast frames bearing that identity's src.
//
// `vendor` is the raw payload[1] byte we observed when sniffing this remote
// (e.g. 0x43 for Somfy, 0x61 for Velux). `man_id` is the raw byte at
// position 25 of a cmd 0x30 pair frame (0x02 Somfy, 0x01 Velux). Both are
// stored as wire values rather than enums — the TX path reads them verbatim
// with no per-vendor code paths. man_id is sniffed from a cmd 0x30 frame
// when available; falls back to a vendor→man_id mapping at capture time.
typedef struct {
    char name[IOHC_DEVICE_NAME_MAX + 1];
    uint8_t motor_addr[3];
    uint8_t vendor;
    uint8_t man_id;
    IohcIdentity identity;
} IohcDevice;

typedef struct IohcDeviceBook IohcDeviceBook;

IohcDeviceBook* iohc_device_book_alloc(void);
void iohc_device_book_free(IohcDeviceBook* b);

uint8_t iohc_device_book_count(const IohcDeviceBook* b);
IohcDevice* iohc_device_book_get_mut(IohcDeviceBook* b, uint8_t index);
const IohcDevice* iohc_device_book_get(const IohcDeviceBook* b, uint8_t index);

// Adds an entry with a freshly-generated random identity. Returns the new
// index, or -1 if the book is full.
// `vendor` is the raw payload[1] byte observed on-air for cmd 0x00 button
// frames; passed through verbatim on TX.
// `man_id` is the raw byte at position 25 of cmd 0x30 pair frames; passed
// through verbatim on TX. If unknown at save time, derive from vendor via
// iohc_man_id_default_for_vendor() below.
int iohc_device_book_add(
    IohcDeviceBook* b, const char* name, const uint8_t motor_addr[3],
    uint8_t vendor, uint8_t man_id);

// Best-effort default for man_id when we haven't sniffed a cmd 0x30 frame
// (which is rare in normal use). Covers the two vendors we have observed.
// Anything unknown gets the safer Somfy default (more permissive in tests).
uint8_t iohc_man_id_default_for_vendor(uint8_t vendor);
void iohc_device_book_rename(IohcDeviceBook* b, uint8_t index, const char* new_name);
void iohc_device_book_remove(IohcDeviceBook* b, uint8_t index);
bool iohc_device_book_has_addr(const IohcDeviceBook* b, const uint8_t addr[3]);

// Bump a device's seq counter and persist.
uint16_t iohc_device_book_next_seq(IohcDeviceBook* b, uint8_t index);

// File: /ext/apps_data/iohc_flipper/devices.bin (binary, per-entry struct).
// Migrates from legacy /ext/apps_data/iohc_flipper/devices.txt on first load
// if the binary file doesn't exist yet (generates fresh identities for
// migrated entries — the old global pairings on motors are invalidated).
bool iohc_device_book_load(IohcDeviceBook* b);
bool iohc_device_book_save(const IohcDeviceBook* b);

// Write a human-readable JSON snapshot of the full state (global identity +
// every device's identity, including seq). Format is the migration handoff
// artifact for the Home Assistant bridge — see project_flipper_temporary memo.
// Returns true if the file was written successfully.
bool iohc_device_book_export_json(
    const IohcDeviceBook* b, const IohcIdentity* global, const char* path);
