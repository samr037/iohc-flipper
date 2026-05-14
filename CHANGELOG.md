# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Per-device `man_id` byte (Phase 6c) — fully vendor-agnostic TX path.
  Replaces the previous `man_id_for_vendor()` mapping function. Storage
  format `devices.bin` v2 → v3 (entries grow from 50 to 51 bytes). v2 → v3
  migration is automatic on load; identities and seq counters preserved.
- JSON identity export (`export-YYYYMMDD-HHMMSS.json`) now includes the
  `man_id` field for Home Assistant handover.

### Changed
- TX hot path is now data-driven from `dev->vendor` and `dev->man_id`.
  Vendor names appear only in UI labels (Sniff & Capture, device list) for
  presentation, and in capture-time fallbacks for unknown remotes.

## [0.6.0] — 2026-05-14 — Velux + Somfy validated

### Added
- **Velux pairing and control (Phase 6b)**: the FAP can now pair itself
  with Velux KLI 313 motors entirely in software. The pair sequence emits:
  - 4× cmd 0x39 announce (broadcast)
  - cmd 0x30 install-key to all 4 destinations (broadcast `00 00 3F` for
    Somfy compatibility + the 3 Velux product type codes `00 00 BF`,
    `00 00 FF`, `00 03 7F`)
  - 6× STOP + 6× DOWN button frames as the page-10-step-5 follow-up
    required by Velux KLI 313 protocol
- **Per-device vendor byte (Phase 6a)**: auto-detected from sniffed button
  frames (`0x43` Somfy, `0x61` Velux). Stored as the raw wire byte.
- **Capture view indexed by source remote** (one row per physical remote,
  not per destination). Vendor only trusted from cmd 0x00 button frames.
- **Success chime on remote-learned** (`sequence_success`) matches Sub-GHz
  / IR app behavior.
- Identity backup now produces three artifacts: binary copies of
  `identity.bin` and `devices.bin`, plus a JSON export.
- Auto-mirror on save: every write to `identity.bin` and `devices.bin`
  also writes a `*.bak.bin` shadow file on the same SD card.
- 6 retransmissions per button frame (1 LPM + 5 plain at ~14 ms gaps) —
  matches real Smoove behavior. Earlier 4-retransmission count was too
  few for marginal RSSI.
- Per-TX LED feedback: blue flash at TX start, green on full success,
  red on any frame failure.

### Fixed
- Migration of legacy `devices.txt` to v2 binary format; generates fresh
  per-device identities so the global pairing is invalidated cleanly.

## [0.5.0] — Phase 5: independent paired emitter

- The Flipper acts as a new remote: it pairs itself with a Somfy motor
  via a single button-press while holding PROG on a real Smoove. After
  pairing, UP/DOWN/STOP from the FAP move only that motor.
- Per-shutter identity isolation: each saved shutter gets its own
  `(src, install_key, seq)`. Frames are HMAC-signed with that
  install_key — only the paired motor verifies and reacts; others ignore.

## [0.4.0] — Phase 4: 1W transmitter

- Bit-packed UART-wrapped frame builder for the CC1101 FIFO.
- HMAC computed in C, validated against the Python reference.
- Calibration timing fixed: separate SPI sessions for register init,
  PA table, and frequency/calibration phases. Chip-ready wait inserted
  before every SPI op. CCA disabled (CCA_MODE = 00) so STX never blocks.

## [0.3.0] — Phase 3: install-key recovery

- Captured a cross-pair incident, reverse-engineered the cmd 0x30 frame
  structure, and extracted the install_key for the user's installation.
- Validated with the Python reference (`tools/iohc_key_decrypt.py`).

## [0.2.0] — Phase 2: frame parser

- Parse Ctrl B1 / Ctrl B2, dst, src, cmdid, payload, 1W suffix (seq + MAC),
  CRC-16/KERMIT. Live frame inspector view on the Flipper.

## [0.1.0] — Phase 1: passive sniffer

- CC1101 in FSK mode, 868.95 MHz, 38.4 kbps. Sync trick
  (`0x57FD` aligning preamble-tail + UART-wrapped 0xFF) so the hardware
  sync detector locks the bitstream without an in-software bit-finder.
