# Flipper Zero io-homecontrol FAP — Project Brief

## Context

I'm building a Flipper Zero FAP (Flipper Application Package) that implements
the io-homecontrol protocol used by Velux and Somfy roller shutters and windows
on the 868 MHz band. Goal: local, cloud-free control of my own devices from a
Flipper Zero app.

This is interoperability research on hardware I own and legally control
(protected under EU Directive 2009/24/EC). The protocol has been reverse-
engineered by the open-source community over ~8 years; I am building on top
of their published work, not breaking new cryptographic ground.

## Hardware target

- Flipper Zero — STM32WB55RG @ 64 MHz, FreeRTOS-based firmware, hardware AES accelerator
- **Phase 1–2 (sniffing & decoding)**: internal TI CC1101 sub-GHz transceiver
- **Phase 4+ (TX, 2W with FHSS)**: external Flipper SubGhz Add-on with Semtech SX1262
  (same family as the reference iown-homecontrol implementation on HelTec WiFi LoRa 32 v3)
- Firmware base: official Flipper firmware unless I say otherwise.
  Ask before assuming Momentum / Xtreme / Unleashed — APIs differ.

## Protocol facts (treat as ground truth, do not re-derive)

- 868 MHz band, FSK modulation, 38400 bps, 19.2 kHz deviation
- 3 channels: 868.25, 868.95, 869.85 MHz
- FHSS in 2W mode: channel hop every 2.7 ms during a single transaction
- AES-128 in a custom block mode with non-standard padding
- 3 distinct CRC schemes (over different segments of the frame)
- Link-layer wrapping with UART 8N1 byte framing
- 1W = unidirectional, no FHSS during emission, command-only
- 2W = bidirectional with acknowledgments, requires FHSS
- Pairing exchanges the installation key from receiver to new emitter — we
  obtain the key via legitimate pairing, never by brute force

## Reference repositories (read these first, in this order)

1. https://github.com/Velocet/iown-homecontrol — protocol documentation
   (Layer 1/2/3), CC0 licensed, the reference for what the protocol does
2. https://github.com/rspaargaren/iohomecontrol — working ESP32 + SX1276
   implementation with MQTT and Home Assistant discovery, focus on 1W
3. https://github.com/CyrilOpenSource/iown-homecontrol-esp32sx1276 — fork
   origin of #2, sometimes more current on 2W
4. https://github.com/merbanan/rtl_433 — decoder #189 (`io_homecontrol`) for
   passive validation of our sniffer output

## Architecture rules

- Single FAP, C only, built with `ufbt`
- Direct CC1101 / SX1262 register-level access via the Flipper SPI HAL.
  Do **not** rely on the high-level `furi_hal_subghz_*` API beyond initial
  bring-up — it makes assumptions that break iohc framing.
- Time-critical work (RX FIFO drain, channel retune, frame TX) runs in ISRs
  on GDO0/GDO2 (CC1101) or DIO1 (SX1262); wrap critical sections with
  `FURI_CRITICAL_ENTER` / `FURI_CRITICAL_EXIT`.
- Protocol state machine in a normal FreeRTOS task, fed by an ISR-to-task
  message queue.
- Use the STM32WB55 hardware AES accelerator. No software AES.
- Pairing keys persisted to Flipper internal storage, encrypted at rest with
  a device-derived key.
- UI: standard Flipper view dispatcher. Main screen lists discovered devices
  and last known state.

## Roadmap — implement in order, ask before skipping ahead

### Phase 1 — Passive sniffer  *(starting point)*
- CC1101 in RX-only mode: FSK, 868.95 MHz, 38400 bps, 19.2 kHz deviation
- Dump raw frames to SD card as `{timestamp, freq, rssi, hex_bytes}`
- Live frame counter and last-frame hex preview on the LCD
- Validation: same bytes as `rtl_433 -R 189` on a separate RTL-SDR.
  I will provide reference captures.
- No payload decoding yet.

### Phase 2 — Frame decoding
- Port Layer 1/2/3 parser from Velocet: preamble, sync word, length, source
  and destination addresses, CMDid, the 3 CRCs
- Display parsed fields on screen, log to SD
- Payload stays encrypted at this stage

### Phase 3 — Pairing & key extraction
- Emitter-side pairing handshake
- Capture installation key from receiver's pairing response
- Persist key in encrypted NVS storage

### Phase 4 — 1W transmission
- Encrypt UP / DOWN / STOP / position commands with the HW AES engine
- TX through CC1101, verify physically (the shutter moves)

### Phase 5 — 2W with FHSS
- Switch to SX1262 add-on
- Channel hopping during transactions within the 2.7 ms budget
- Parse acknowledgment / state frames, expose device state in UI

## Working agreement

- **First action of every session**: read `PROGRESS.md`. Create it on the first
  session. Update it at the end of every session with: what works, what's
  known to fail, what's next.
- **For every protocol decision** (CRC byte order, address endianness, AES
  block layout, sync word value, etc.) cite the source: a specific
  file/commit in Velocet or rspaargaren, or a sniffer capture in our repo.
- **Don't guess.** If something isn't in the references and isn't in a capture,
  say so and list what would resolve it.
- **Don't auto-advance phases.** Each phase ends with me physically verifying
  on hardware. Wait for explicit go-ahead.
- **For any change to ISR or register-init code**, explain the timing budget
  and worst-case execution path *before* writing the code.
- **Commit messages** reference the phase and the source: e.g.
  `phase1: cc1101 init for fsk rx (per Velocet/docs/phy.md)`

## First task for this session

1. Read all three reference repos plus the rtl_433 decoder. Produce
   `SUMMARY.md` covering:
   - Frame structure: preamble, sync word, length byte semantics, address
     fields, header, payload, all 3 CRCs (which bytes each covers)
   - Differences between Velocet's documentation and rspaargaren's working
     implementation (where do they disagree? which one is closer to truth
     according to rtl_433 captures?)
   - Exact CC1101 register values used by the closest equivalent project
     (modulation, deviation, data rate, sync word, packet config, AGC, FEC)
2. Propose the FAP directory layout and the `application.fam` manifest
3. **Stop and wait for my review.** Do not write any C code yet.

Working directory: Flipper firmware source tree, our FAP under
`applications_user/iohc_flipper/`.
