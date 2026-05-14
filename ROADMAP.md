# Roadmap

Living document. Reorder freely.

## Done

- ✅ **Phase 1** — Passive sniffer (CC1101 sync 0x57FD + soft 8N1 unframer)
- ✅ **Phase 2** — Frame decoder + CRC verification + structured logs
- ✅ **Phase 3** — Install-key extraction (Somfy + Velux, mathematically verified)
- ✅ **Phase 4** — 1W TX (Somfy UP/DOWN/STOP), +10 dBm PA, clean RX/TX coexistence

## Active next

### Phase 5 — Flipper as an independent emitter
Goal: end the "real-remote seq catch-up" pain. Flipper gets its own paired
identity per motor.

- Generate Flipper's own `(src_addr, install_key, seq)` on first run, persist
  to `/ext/apps_data/iohc_flipper/identity.bin`.
- Implement `iohc_frame_build_pair_0x30()` — encrypts Flipper's install key
  with `transfer_key` (same algorithm we already use to *decrypt* captured
  keys; just run it the other way).
- "Pair Flipper" button: TX a `0x39` announce + `0x30` install-key broadcast
  sequence with the Flipper's own identity.
- User flow: hold PROG on the real Smoove → press "Pair Flipper" → motor
  stores Flipper as a separate paired emitter, with its own seq counter.

### Phase 5.5 — 5-burst Somfy emission parity (optional)
For sessions where the user still wants the Flipper to *impersonate* the
real Smoove rather than be a separate identity: mimic the full 5-burst
sequence Phase 2 observed (directed → 3× broadcast → directed) instead of
the current single broadcast burst.

### Phase 6 — Velux TX
Same recipe as Somfy, different vendor byte (`0x61`) and address book
(`0000BF`, `0000FF`, `00037F` for the user's three Velux windows). Pair via
the rings button instead of PROG.

### Phase 7 — Encrypted on-device key storage
Use the STM32WB55 hardware AES + the enclave-provisioned key (we have 10
valid keys per `device_info`) to wrap `identity.bin` and any captured
external install keys. Currently they sit in plaintext on the SD card.

## Polishing & distribution

### Phase 8 — Submit to the Unleashed app catalog

Goal: anyone running Unleashed firmware can install `iohc_flipper` from
their on-device app catalog without building it themselves.

Work items:
- **UX overhaul** — current UI is functional but engineer-grade. Needs:
  - Proper icon (current is a placeholder 10×10 stub).
  - Cleaner main screen layout (status, last frame, lock indicator, TX
    shortcut all on one screen without overflow).
  - A welcome / first-run screen explaining what the app does and how to
    pair Flipper to a motor.
  - "Install key extraction" workflow as a guided wizard (one-screen
    explanation → instruct user to press PROG / rings → live capture →
    confirm key found → save).
  - Per-device labels (motor friendly names: "Living-room shutter" etc.).
  - Error / status toasts for TX outcomes.
- **Code polish**:
  - Remove debug `FURI_LOG_I` calls or gate behind a verbose flag.
  - Replace hardcoded keys in `tx_state.h` with persistent storage.
  - Add README screenshots, GIFs of the sniffer + TX.
  - Cleaner application.fam (`fap_author`, `fap_weburl`, version semver).
- **Submission process** — Unleashed accepts FAPs to their app catalog
  via GitHub PR to their apps repo. Need to research their guidelines:
  - License declaration (CC0/MIT/GPL?)
  - Screenshot requirements
  - Description format
  - Whether iohc/AES code requires any disclosure (likely not, given the
    crypto is public via rspaargaren et al for years)
- **Legal review** — interop research is protected under EU 2009/24/EC,
  but a public app increases visibility. Worth confirming with Unleashed
  maintainers that they're OK with shipping a tool that derives
  installation keys from on-air captures (it's strictly interop on owned
  hardware, not a hacking tool — but distribution = more scrutiny).

## Future / parking lot

### ESPSomfy-RTS integration (notes only, no commitment)

ESPSomfy-RTS (https://github.com/rstrouse/ESPSomfy-RTS) is an ESP32-based
Somfy bridge with Home Assistant integration. It currently supports only
**Somfy RTS** — the older **433 MHz one-way** protocol with rolling codes,
which is completely different from io-homecontrol.

Possible angles:
- **Add iohc support to ESPSomfy** as a parallel transport layer. Would
  require new RF backend (the project uses CC1101 at 433 MHz; iohc is
  CC1101/SX1276 at 868 MHz). Their CC1101 code could potentially be reused
  if the wiring supports both bands or with a hardware revision.
- **Stand-alone "ESP-iohc" project** — an ESP32 + SX1276 (or HelTec LoRa
  V3 like rspaargaren uses) firmware that mirrors what we've built on the
  Flipper, with MQTT + HA discovery. rspaargaren already has something
  similar — could fork or upstream improvements.
- **Bridge mode** — the Flipper acts as the RF endpoint, exposes commands
  over USB CDC, and an ESPHome / ESP32 sidecar relays them to Home
  Assistant. Cheap and uses the Flipper hardware we already have.

Decision deferred. Worth revisiting once the Flipper app is in the
Unleashed catalog and proven stable.

### 2W support (SX1262 add-on)
- ~30 € for the Flipper SubGhz Add-on board.
- Unlocks: receive motor ack frames, FHSS hopping during pairing,
  bidirectional control. Currently we're "fire and pray" for 1W TX.

### Multi-installation support
Single binary that can hold multiple identities (e.g. home + parents'
house). Per-identity address book, seq counter, key. Selector in UI.

### Pure-Python desktop tool
A pyserial-based desktop tool that talks to the Flipper via CDC for
recording / batch analysis / Home Assistant testing. Some of this already
exists under `tools/`.
