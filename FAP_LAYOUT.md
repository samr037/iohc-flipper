# FAP layout proposal — `iohc_flipper`

Proposal for the directory structure and `application.fam` manifest of the
io-homecontrol Flipper application. **Awaiting review — no code written.**

## Target path

Per the brief, the FAP lives under the Flipper firmware tree at
`applications_user/iohc_flipper/`. For our dev workflow we'll mirror that
structure under `somfy-io/iohc_flipper/` and build with `ufbt`, which doesn't
require the whole firmware tree to be present.

## Directory layout

```
iohc_flipper/
├── application.fam              # ufbt manifest (entry point, deps, icon)
├── icons/
│   └── iohc_10x10.png           # menu icon
├── images/
│   └── splash_128x64.png        # optional banner
├── README.md                    # build + flash + usage notes
├── src/
│   ├── iohc_app.c               # app entry, view dispatcher setup, scenes
│   ├── iohc_app.h               # app context struct, shared types
│   ├── radio/
│   │   ├── radio.h              # abstract radio interface (RX/TX, channel, RSSI)
│   │   ├── radio_cc1101.c       # CC1101 backend (Phase 1–4)
│   │   ├── radio_cc1101_regs.h  # CC1101 register values for iohc PHY
│   │   └── radio_sx1262.c       # SX1262 backend (Phase 5 — stub for now)
│   ├── phy/
│   │   ├── phy_rx_isr.c         # GDO0/GDO2 ISR, FIFO drain, frame assembly
│   │   ├── phy_tx.c             # frame TX driver
│   │   └── phy_queue.h          # ISR → task message queue
│   ├── iohc/
│   │   ├── frame.h              # on-air byte map: preamble, sync, length,
│   │   │                        #   addr, header, cmdid, payload, 3 CRCs
│   │   ├── frame_parse.c        # decode L1/L2/L3 (Phase 2)
│   │   ├── frame_build.c        # encode (Phase 4)
│   │   ├── crc.c                # the 3 CRC schemes
│   │   ├── crc.h
│   │   ├── aes.c                # STM32WB55 HW AES wrapper (Phase 4)
│   │   ├── aes.h
│   │   ├── addr.c               # address handling, endianness helpers
│   │   └── cmdid.h              # known CMDids (UP/DOWN/STOP/POS/PAIR/...)
│   ├── pairing/
│   │   ├── pairing.c            # pairing handshake state machine (Phase 3)
│   │   └── key_store.c          # encrypted NVS for installation keys
│   ├── state/
│   │   ├── proto_fsm.c          # protocol state machine task
│   │   └── device_db.c          # discovered devices, last-known state
│   ├── ui/
│   │   ├── view_main.c          # main menu / device list
│   │   ├── view_sniffer.c       # live frame counter + last hex
│   │   ├── view_devices.c       # discovered device list
│   │   ├── view_pair.c          # pairing wizard
│   │   └── scene_manager.c      # view dispatcher glue
│   └── log/
│       ├── log.c                # SD-card frame logger
│       └── log.h
└── docs/
    ├── PHY.md                   # CC1101 register choices, citations
    ├── FRAMES.md                # frame layout, citations to refs/
    └── TIMING.md                # ISR timing budgets
```

### Why this split

- **`radio/` vs `phy/`** — `radio/` is the chip driver (registers, SPI,
  power state). `phy/` is the iohc PHY logic (sync detection, framing,
  ISR plumbing). Same iohc PHY code should drive both CC1101 and SX1262;
  the radio backend is swappable.
- **`iohc/`** — pure protocol logic. No hardware. Testable on host with
  captured byte arrays once we have unit-test infrastructure (later).
- **`state/`** — FreeRTOS task that consumes parsed frames and drives a
  state machine. Kept separate from UI so the protocol runs regardless
  of which view is on screen.
- **`pairing/`** — isolates the sensitive code path; key handling and
  pairing state stay in one place. Phase 3 only.
- **`ui/` + `log/`** — leaves, no protocol dependencies.

### What lives in ISR vs task

- **ISR (GDO0/GDO2)**: read RX FIFO, push raw bytes into queue, capture
  RSSI snapshot. Nothing else. `FURI_CRITICAL_ENTER/EXIT` only around
  the SPI burst read.
- **Task `proto_fsm`**: pulls bytes from queue, assembles frames, runs
  CRC checks, dispatches to parser, updates device DB, emits log events.
- **Task `ui`**: standard Flipper view dispatcher, subscribes to device
  DB change events.

### What's NOT in the layout (deliberately)

- No software AES. The brief mandates the STM32WB55 HW AES accelerator
  via `aes.c` wrappers.
- No build-time test harness. Unit tests for the parser can live under
  `test/` later — out of scope for the FAP itself.
- No `Makefile`. `ufbt` builds from `application.fam`.

## `application.fam` draft

```python
App(
    appid="iohc_flipper",
    name="io-homecontrol",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="iohc_app_main",
    requires=[
        "gui",
        "storage",
        "notification",
    ],
    stack_size=4 * 1024,
    fap_category="Sub-GHz",
    fap_icon="icons/iohc_10x10.png",
    fap_icon_assets="icons",
    fap_author="@samyjacquet",
    fap_version="0.1",
    fap_description="io-homecontrol sniffer/controller for Velux & Somfy IO",
    fap_weburl="https://github.com/samyjacquet/iohc_flipper",
    sources=[
        "src/*.c",
        "src/radio/*.c",
        "src/phy/*.c",
        "src/iohc/*.c",
        "src/pairing/*.c",
        "src/state/*.c",
        "src/ui/*.c",
        "src/log/*.c",
    ],
)
```

### Notes on the manifest

- `apptype=EXTERNAL` produces a loadable `.fap`, not an in-tree app. This
  lets us iterate without rebuilding the whole firmware. Required for the
  `ufbt` workflow.
- `stack_size=4 * 1024` is a starting guess. Sniffer phase can probably
  drop to 2 KB; pairing/state machine may need more. Will tune once we
  measure with `furi_thread_get_stack_space`.
- `requires=["gui","storage","notification"]` — minimum set. We'll
  probably need `subghz` later for sharing the radio with the broader
  Flipper subghz subsystem, but **not** during Phase 1–2 since we
  manage the radio directly.
- No `furi_hal_subghz_*` dependency declared explicitly — the FAP links
  against `furi_hal` headers automatically.
- `fap_category="Sub-GHz"` puts the app in the Sub-GHz section of the
  Flipper apps menu. Alternative: `"Tools"`.

## Open questions before scaffolding

1. **Repo layout outside the FAP.** Keep `somfy-io/iohc_flipper/` as a
   self-contained ufbt project (recommended), or symlink it into a full
   Flipper firmware checkout at `applications_user/`? Self-contained is
   simpler; firmware-tree symlink lets us debug HAL changes side-by-side.
2. **SDK channel.** ufbt fetches an SDK by Flipper firmware channel. We
   need to point it at **Unleashed 089e** specifically since you're running
   that. There's a `UFBT_TOOLCHAIN_PATH` / `--channel` mechanism — needs
   confirmation by looking at the Unleashed firmware source (clone in
   progress).
3. **Icon.** Need a 10×10 monochrome icon. Placeholder for now or do you
   want me to draft one?
4. **Storage path on SD.** Logs go to `/ext/apps_data/iohc_flipper/` by
   convention. Files: `frames.csv`, `frames.bin`, `keys.enc`. OK?
5. **Naming.** `iohc_flipper` vs just `iohc` for the appid. Shorter is
   nicer in the menu but more likely to collide.

Stop here. Review and tell me what to adjust before I create any of these
files.
