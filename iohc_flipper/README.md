# iohc_flipper

Flipper Zero application implementing the io-homecontrol protocol used by
Velux and Somfy IO motors on 868 MHz. Interoperability research on devices
the operator owns and controls (EU Directive 2009/24/EC).

## Status

Phase 1 (passive sniffer) — not yet implemented. Scaffold only.
See `../SUMMARY.md` for the protocol synthesis and `../FAP_LAYOUT.md` for
the design rationale.

## Build

Requires the Flipper SDK matching the firmware on the device (Unleashed
089e). Build with [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```sh
# one-time SDK fetch (Unleashed channel)
ufbt update --index-url https://up.unleashedflip.com/directory.json --channel release

# build the FAP
cd iohc_flipper
ufbt

# launch on a connected Flipper
ufbt launch
```

## Layout

```
src/
├── radio/    chip drivers (CC1101, later SX1262), register-level
├── phy/      iohc PHY: ISR, FIFO/bit drain, frame assembly
├── iohc/     pure protocol: frame parse/build, CRC, AES, addresses
├── pairing/  pairing handshake + encrypted key store
├── state/    FreeRTOS task with the protocol state machine
├── ui/       view dispatcher, scenes
└── log/      SD-card frame logger
```

ISR work is confined to `phy/`. The protocol layer (`iohc/`) has no
hardware dependencies and is host-testable.

## Roadmap

1. **Passive sniffer** — CC1101 RX-only, 868.95 MHz, dump raw frames to SD.
2. **Frame decoding** — port L1/L2/L3 parser.
3. **Pairing & key extraction** — capture installation key during a real pair.
4. **1W TX** — encrypt UP/DOWN/STOP/position with STM32WB55 HW AES, transmit.
5. **2W with FHSS** — SX1262 add-on, channel hop every 2.7 ms.

Each phase is gated by physical verification on hardware. See
`../BRIEF.md` for the full working agreement.

## Storage

On-device data path: `/ext/apps_data/iohc_flipper/`

- `frames.bin` — raw captured frames (binary, append-only)
- `frames.csv` — index `{timestamp, freq, rssi, len, sha8}`
- `keys.enc` — installation keys, encrypted at rest

## References

Cloned under `../refs/`:

- Velocet `iown-homecontrol` — protocol spec (CC0)
- rspaargaren `iohomecontrol` — ESP32 + SX1276 working impl
- CyrilOpenSource fork — sometimes more current on 2W
- rtl_433 `somfy_iohc.c` — passive decoder for cross-validation
- Unleashed firmware source — SDK reference for HAL surface
