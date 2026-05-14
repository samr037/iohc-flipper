# CLAUDE.md — iohc-flipper

Context for future Claude Code sessions in this project. Read once at session
start. Cross-reference with PROGRESS.md for the full historical trail.

## What this is

A Flipper Zero FAP that speaks io-homecontrol (868 MHz, Velux + Somfy +
others). User-driven RE / interop research on hardware the user owns.
Phase 6 is achieved as of 2026-05-14: cross-vendor pairing + control
works in software, no external programmer needed.

## Hard constraints (from the user's brief)

These override defaults. Read first.

1. **Don't auto-advance phases.** Each phase ends with the user physically
   verifying on hardware. Wait for explicit go-ahead before starting the
   next phase.
2. **For any change to ISR or register-init code, explain the timing budget
   and worst-case execution path *before* writing the code.** CC1101 SPI
   timing is fragile; we burned a day on a chip-ready-wait omission.
3. **For every protocol decision cite the source.** Captured frames,
   official docs, or upstream RE projects. Never guess byte semantics.
4. **Don't guess.** Capture first, then reason.
5. **First action of every session: read PROGRESS.md.** It carries the full
   context window of decisions and gotchas from prior sessions.

## Code map

```
iohc_flipper/src/
├── iohc_app.c           # Entry, view dispatcher, consumer thread, callbacks
├── radio/
│   └── radio_cc1101.c   # SPI driver, register init, RX/TX state machine
├── phy/
│   └── phy_rx.c         # FIFO drain → frame assembly with sync alignment
├── iohc/
│   ├── frame_parse.c    # Bytes → IohcParsedFrame (Ctrl/dst/src/cmd/payload/seq/MAC/CRC)
│   ├── frame_build.c    # Build 1W frame + UART-wrap + bit-pack for FIFO
│   ├── crc.c            # CRC-16/KERMIT
│   ├── hmac_1w.c        # AES-128-ECB based HMAC for 1W suffix
│   ├── aes.c            # AES-128 primitives (CBC/ECB/CFB)
│   └── key_encrypt.c    # iohc_wrap_install_key (cmd 0x30 enc_key field)
├── state/
│   ├── identity.c       # Global Flipper identity (src+key+seq) on SD
│   ├── device_book.c    # Per-shutter identities, binary format v2 + JSON export
│   └── tx_runner.c      # High-level send_button / send_pair, vendor-mapped man_id
├── ui/
│   ├── view_main_menu.c
│   ├── view_sniffer.c       # Live raw frame display
│   ├── view_capture.c       # SRC-indexed capture (vendor only trusted on cmd 0x00)
│   ├── view_device_list.c   # Saved shutters with [S]/[V]/[?] prefix
│   ├── view_device_actions.c
│   └── view_identity.c
└── log/
    └── log.c            # Per-session CSV + raw .bin write
```

## Critical gotchas (paid for in blood)

These are easy to break. See PROGRESS.md "code-level gotchas" for the full set.

1. **CC1101 needs chip-ready wait before EVERY SPI op.** Poll MISO low via
   `cc1101_wait_ready`. Skipping this gives silent failures (most SPI ops
   succeed in dev because the chip happens to be ready, then production
   timing breaks at random).
2. **SPI burst write >16 bytes silently truncates.** Use single-byte writes
   for the FIFO (`spi_write_fifo_burst` already does this).
3. **PA table write breaks RX unless done in a separate SPI session AFTER
   calibration.** Wait for `MARCSTATE == IDLE` between phases.
4. **`MARCSTATE` polling after STX exits too early** — it transitions
   IDLE → CALIBRATE → TX → IDLE. We poll after a fixed 25 ms delay to be
   in TX, not still in CALIBRATE.
5. **HMAC `frame_data` for button cmd=0x00 is 7 bytes** (cmd+data+5 extras),
   not 2. Off-by-five is a common mistake.
6. **`CCA_MODE=11` blocks STX on busy channel.** We set `MCSM1=0x00`
   (CCA_MODE=00, always TX). Necessary for the rapid retransmission pattern.
7. **Don't auto-launch the FAP via `ufbt launch` while it's running** —
   you'll get `ERROR_APP_NOT_RUNNING` from `app_exit` then `ERROR_APP_SYSTEM_LOCKED`
   on the start. Have the user back out first, or kill it via `app_exit`
   only when actually running.
8. **Strict aliasing / `-Werror=format-truncation`** is on. Buffer for
   `%04u%02u%02u-%02u%02u%02u` needs ≥24 bytes (year can be 1-3 digits even
   though we expect 4).

## Vendor-specific protocol differences (the *only* per-vendor branches)

| Field | Somfy | Velux | Where |
|---|---|---|---|
| Button frame vendor byte (payload[1] of cmd 0x00) | `0x43` | `0x61` | captured from sniff, stored in `IohcDevice.vendor` |
| Pair frame `manufacturer_id` (byte[25] of cmd 0x30) | `0x02` | `0x01` | `man_id_for_vendor()` in `tx_runner.c` |
| Pair frame dst (cmd 0x30) | broadcast `00 00 3F` | type codes `00 00 BF` / `00 00 FF` / `00 03 7F` | `send_pair_with_identity()` loops over all 4 dsts |
| STOP+DOWN follow-up | harmless (single STOP, single DOWN) | required per KLI 313 page 10 step 5 | `send_pair_with_identity()` appends always |

The vendor byte is stored as the raw observed value (not an enum) so the
system stays vendor-agnostic in storage. Only the protocol-correctness
mapping above is hardcoded.

## Build / deploy / debug

```bash
# Build the FAP (with .venv active)
cd iohc_flipper && ../.venv/bin/ufbt

# Deploy + launch (user must back out of running app first)
../.venv/bin/ufbt launch

# Inspect frame captures
.venv/bin/python tools/flipper_cli.py 'storage list /ext/apps_data/iohc_flipper'
.venv/bin/python tools/flipper_cli.py 'storage read /ext/apps_data/iohc_flipper/frames_NNN.csv'

# Snapshot the Flipper screen
.venv/bin/python tools/flipper_screen.py snapshot /tmp/flipper.png

# Push binaries via RPC (useful for patching devices.bin)
.venv/bin/python -c "
from flipperzero_protobuf.flipper_proto import FlipperProto
p = FlipperProto(serial_port='/dev/cu.usbmodemflip_Qu3r1')
p.rpc_app_exit()
p.rpc_write('/ext/apps_data/iohc_flipper/devices.bin', open('/tmp/devices_new.bin','rb').read())
p.rpc_app_start('/ext/apps/Sub-GHz/iohc_flipper.fap', '')
"
```

## File formats

### `identity.bin` (21 bytes)
```
[0..2]   src_addr        (MSB first on wire)
[3..18]  install_key     (AES-128)
[19..20] seq             (BE)
```

### `devices.bin` v2 (50 bytes per entry, after 6-byte header)
```
header:  magic 'IHCD' [4]  version=2 [1]  count [1]
entry:   name[25]  motor_addr[3]  src[3]  install_key[16]  seq_be[2]  vendor[1]
```

v1 entries (49 bytes, no `vendor`) auto-migrate to v2 on load with
`vendor = 0x43 (Somfy)` default — see comment in `device_book.c`.

## Conventions

- **Vendor-agnostic by design**: store raw bytes, no enums. UI labels
  ("Somfy" / "Velux" / "?") are presentation-only.
- **TX always broadcasts dst for button frames** (`00 00 3F`). Isolation
  comes from HMAC, not addressing.
- **6 retransmissions per button press** (1 LPM + 5 plain at ~14 ms gaps).
  Matched real Smoove behavior — 4 was too few for marginal RSSI.
- **Per-press LED**: blue flash on TX start, green on success, red on
  failure. Driven by the TX function's return bool.
- **Per-session log**: every parsed frame written to a new `frames_<tick>.csv`
  + `.bin` in `apps_data/iohc_flipper/`. Survives app restart.

## When the user asks for "next phase"

Check PROGRESS.md "RESUME HERE" section + the Roadmap. Don't start
anything new without explicit confirmation. Current state as of last
session: Phase 6 unlocked (Velux pairing validated). Next candidates:

- Phase 7 (encrypted on-device storage) — adds threat-model protection
- Phase 8 (Unleashed catalog submission) — requires icon + screenshots
- Phase 9 (SX1262 add-on) — better link budget; user needs to acquire hardware
- Phase 10 (HA bridge) — long-term goal; Flipper retires after handover

Ask which one.

## Reference projects (in `refs/`)

- `iohomecontrol/` — original RE work
- `iown-homecontrol/` — Velocet's umbrella project (cmd reference)
- `iown-homecontrol-esp32sx1276/` — rspaargaren's ESP32 port (canonical cmd 0x30 doc)
- `rtl_433/` — RF demodulation reference (sync trick provenance)
- `unleashed-firmware/` — host firmware tree

`refs/` is gitignored — it's heavy (700+ MB) and not ours.

## Memory

See `~/.claude/projects/-Users-samyjacquet-Claude-flipper/memory/MEMORY.md`
for cross-session memos:
- `project_flipper_temporary.md` — Flipper retires when HA bridge ships
- `project_phase6_velux_unlock.md` — the three Velux protocol differences

## Communication style with this user

- Cite sources for protocol decisions (manuals, github lines, captures).
- Explain timing budget before touching ISR/register-init code.
- Don't claim "it works" until the user has physically tested on hardware.
- Capture-first, code-second when investigating a frame format.
- Keep terse — user reads diffs, not paragraphs.
