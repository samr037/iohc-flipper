# PROGRESS

Per the brief working agreement: read this first every session. Update at
the end of every session.

---

## RESUME HERE (state at 2026-05-13 18:13)

Read this section first if returning after context compaction.

**Project state**: Phases 1–5.6 implemented. Sniffer + parser + 1W TX +
per-shutter identity model + full UI all coded & deployed. Phase 5.6
not yet validated on hardware (user needs to re-pair each shutter with
its new per-device identity).

**The Velux situation (revised 2026-05-13 18:33)**:
- "user-A" shutter entry is actually **Velux**, not Somfy
- Current TX path hardcodes vendor `0x43` (Somfy); Velux needs `0x61`
- **Velux pairing is NOT blocked**. User recalled successfully pairing a
  Somfy remote to a Velux shutter yesterday — which retroactively
  explains the Phase 3 cross-pair incident (we *did* successfully add
  Somfy emitter A58E29 to a Velux window's paired list).
- **Trigger for Velux accept-new-emitter mode is the GEAR button** —
  pressing it both starts the identify cycle AND opens a ~30 s window
  during which the Velux motor adopts any emitter that broadcasts a
  valid `0x30` install-key frame.
- So Velux pairs via the *same flow* as Somfy: press gear on Velux
  remote near the target window → Flipper "Pair Flipper here" within
  ~30 s. Phase 5.6 model (per-shutter identity + broadcast) applies
  identically.
- User has 3 Velux remotes, 1 Somfy remote (`user-C` shutter), and the
  Velux windows each have multiple motors per physical unit (window +
  shutter + blind/awning — see "Velux scan results" section below).

**Two distinct next actions**:

1. **Validate Phase 5.6 with Somfy first** (Somfy works end-to-end):
   - Re-pair user-C: hold PROG on real Smoove → Flipper → Saved shutters →
     user-C → Pair Flipper here. Then test user-C → UP. Should isolate.
   - This proves the per-shutter-identity model is correct.

2. **Velux TX is achievable** — just needs Phase 6a (vendor byte per
   device, ~30 min code). Pairing uses the gear button (puts window in
   accept mode for ~30 s), then Flipper Pair Flipper here just like
   Somfy. No special hardware needed.

**Last user action**: noticed "user-A" is Velux + asked if gear-button
scanning was safe + can UP be used instead. We answered: gear is safe
(just identify cycle), UP works but loses NodeID labels. Then asked
about Velux pairing hardware availability — awaiting answer.

**Hardware state**:
- Flipper Qu3r (Unleashed 089e). Global identity `96D4B6` currently paired
  with user-C (motor NodeID `0001BF`). Broadcast from global identity moves
  user-C.
- Saved shutters in device book (with per-shutter identities generated
  by Phase 5.6, NOT YET PAIRED on motors — need fresh PROG/equivalent
  followed by "Pair Flipper here" per entry):
  - **`Shutter user-A_1`** (VELUX, not Somfy) — motor NodeID `0000BF`,
    per-device src `B0C4C1`, key `223BE3786A5B05F4325903E6E30CC61C`, seq `0x0000`.
    User-A's room has TWO Velux windows total; only one address captured so far.
  - **`Shutter user-C`** (Somfy) — motor NodeID `0001BF`, per-device src
    `67A315`, key `3EAE2DF35C803F98B024C2C90714F4EC`, seq `0x0000`.
- 1 Somfy Smoove Origin IO controlling user-C (src `A58E29`, key Phase 3).
- 1 Somfy Smoove (2nd) controlling another shutter (src `CF5526` observed).
- **3 Velux remotes**, one per window (User-A has 2 windows, User-B has 1).
  Sniffed via UP presses 2026-05-13 18:16 (see `docs/phase6_three_velux_remotes.csv`):
  - **user-A-window-1** remote src `AA4E44` (= the BG-RC011-01 we extracted
    the install key for in Phase 3 → `A2B1A89FB8C373BC7569DCF82334AD11`)
  - **user-A-window-2** remote src `67EDA5` (install key NOT extracted yet —
    would require capturing its rings/gear button + 0x30 frame)
  - **user-B-window-1** remote src `39F90D` (install key NOT extracted yet)
  - Window NodeIDs not captured (UP broadcasts don't expose them). Would
    need gear-button scan per remote to get the per-window directed addrs.
- Saved shutter `user-A-window-1` (motor `0000BF`) was renamed 2026-05-13
  18:16 from `Shutter user-A_1`. Note: which physical window is at NodeID
  `0000BF` is unverified — may need user to compare via gear-button identify
  cycle to disambiguate from the second User-A window.
- **Gear-button scan of user-A-window-2's remote 2026-05-13 18:22** shows
  remote `67EDA5` is paired with **3 NodeIDs**: `0000BF`, `0000FF`, `00037F`
  (see `/tmp/user-A2_gear.csv`).
- **Corrected interpretation (user confirmed)**: this is **ONE physical
  Velux window with 3 motors** (Velux INTEGRA windows often bundle a
  window-opening motor + roller-shutter motor + internal-blind motor on
  the same product). The 3 NodeIDs are the 3 actuators on that window.
  - The "type" low byte differs between the 3: `BF`, `FF`, `7F` — likely
    encoding the actuator type (window / shutter / blind).
  - This means both User-A remotes (AA4E44 and 67EDA5) probably control
    the same single physical window with its 3 motors, OR each remote
    controls its respective window which also has 3 motors each but the
    NodeIDs happen to be identical patterns.
  - Implication: each saved "Velux shutter" entry actually maps to 1 of
    3 motors per physical window. To control "open the window" vs "lower
    the blind" vs "close the roller shutter" we'd need separate entries
    per motor — but since face-button UP/DOWN/STOP is broadcast, real
    remotes presumably move ALL of them together based on context (or
    use directed frames for individual control, which we can't do without
    being paired-as-master).

- **Gear-button scan of user-B-window-1's remote 2026-05-13 18:27**: remote
  `39F90D` is paired with **2 NodeIDs**: `0000BF`, `0000FF` (`/tmp/user-B_gear.csv`).
  Notably the SAME first two NodeIDs as User-A's, but no `00037F`. This
  **definitively proves NodeIDs are Velux product-type codes, not
  device-unique addresses**:
  - `0000BF` = window opening motor (common to all Velux INTEGRA windows)
  - `0000FF` = roller shutter motor (common to all that have one)
  - `00037F` = optional blind/awning motor (User-A has it, User-B doesn't)
  - Identical type codes across different physical windows = pure
    "product category" addressing. Disambiguation is 100% via paired-
    emitter authentication (src + install_key).
  - **This is the protocol-level confirmation that broadcast + per-shutter
    identity (Phase 5.6) is the ONLY viable control model.** Directed dst
    addressing is meaningful only when sender's `(src, dst)` matches the
    receiver's per-emitter slot record — which we don't know for Flipper
    pairings (would need 2W ack capture).

**Backup snapshots on desktop (treat as credentials):**
- `docs/flipper_identity.bin` — global identity 21 bytes (Phase 5)
- `docs/flipper_devices.bin` — per-shutter identities 104 bytes (Phase 5.6)
- Refresh these after any pairing operation since seq counters advance.

**To resume work**: read the "Phase 5.6 detailed plan" section below.
Start with step 1 (storage format), do not break existing single-identity
code paths until 5.6 is fully wired (so we can ship at any time).

## Session 1 — 2026-05-12

### What works

- Project structure under `somfy-io/` set up.
- References cloned under `refs/`:
  - `iown-homecontrol/` (Velocet, protocol spec)
  - `iohomecontrol/` (rspaargaren, ESP32+SX1276 impl)
  - `iown-homecontrol-esp32sx1276/` (Cyril, fork)
  - `rtl_433/src/devices/somfy_iohc.c` (decoder, sparse)
  - `unleashed-firmware/` (80M, full SDK source)
- Research output in `SUMMARY.md`: cited frame structure, three-CRC
  reconciliation, CC1101 register set computed from rspaargaren's
  SX1276 config (FREQ=0x216BD1 for 868.95 MHz, MDMCFG3=0x83, MDMCFG4=0x6A,
  DEVIATN=0x34), 10 open questions.
- FAP scaffold under `iohc_flipper/`: directory tree, `application.fam`,
  README, placeholder icon. No C code yet (per brief).
- Pairing procedures documented in `PAIRING.md` for both Smoove Origin IO
  (PROG-PROG handshake) and Velux KLR 200 (pad-to-pad copy).

### What's known to fail / risk

- **No RTL-SDR for cross-validation.** Phase 1 sniffer cannot be
  independently verified against `rtl_433 -R somfy_iohc`. Mitigation:
  trust the protocol spec and verify by getting decoded fields that
  correlate with the buttons we press.
- **CC1101 has no `IOHOME_ON`.** SX1276's hardware 8N1 UART stripping is
  unavailable on the Flipper's CC1101. Phase 1 will require an in-firmware
  soft framer fed by GDO0 in async serial mode. Adds ISR complexity vs.
  the FIFO-drain pattern used by most Flipper subghz apps.
- **Motor receivers cannot be power-cycled.** Phase 3 must be triggered
  via remote-only pairing flows (Somfy PROG-PROG; Velux pad-to-pad).
  Re-pairing is one-shot — sniffer must be reliable before triggering.

### What's next

Phase 1 implementation done; awaiting physical verification.

Implementation summary (2026-05-12, second sub-session):
- Discovered Flipper only wires CC1101 GDO0 (not GDO2). Documented in
  `TIMING_BUDGET.md` with redesigned architecture.
- **Architecture pivot**: instead of bit-bang sampling (option A from
  SUMMARY.md), we use CC1101 normal FIFO mode with sync register set
  to `0x57FD` — exactly the on-air bit pattern of preamble-tail +
  UART-wrapped 0xFF. The chip does bit recovery and FIFO buffering;
  we do byte-aligned 8N1 unframing in software at trivial cost.
- Wrote `src/radio/radio_cc1101.{c,h}` + `radio_cc1101_regs.h` with
  raw SPI access (the `cc1101.h` driver is not exported to FAPs;
  only register defines + SPI bus handle are public API).
- Wrote `src/iohc/unframer.{c,h}` — pure C 10-bit-to-8-bit UART
  unwrapper with framing-error detection, length-byte-driven
  termination. No hardware deps; host-testable.
- Wrote `src/phy/phy_rx.{c,h}` — FreeRTOS task polling FIFO at 1 ms,
  GDO0 sync-detect via EXTI rising-edge ISR, bounded frame queue.
- Wrote `src/log/log.{c,h}` — appends to `/ext/apps_data/iohc_flipper/
  frames_<tick>.{csv,bin}` per session.
- Wrote `src/ui/view_sniffer.{c,h}` — single view with frame count,
  errors, RSSI, last frame hex (12 bytes truncated).
- Wrote `src/iohc_app.c` — entry, dispatcher, consumer thread.
- **Built clean** under Unleashed SDK `unlshd-089` (API 87.8 matches
  device). Uploaded via `ufbt launch`. FAP runs, shows the sniffer
  view with `frm:0 err:0`. Awaiting first real frame to verify the
  RX path.

### Next step: physical verification (USER ACTION)

Press the **Smoove Origin IO** UP or DOWN button near the Flipper.
Expected: `frm:` counter increments, `rssi:` shows a negative dBm
near the noise floor (-80..-50), hex line shows a 11-32 byte frame
beginning with a sane length byte (low nibble 0x08..0x1F).

If frames appear:
- Inspect a few hex values — first byte should be Ctrl B1 with bit
  5 = 1 (1W mode) for a Somfy emitter button press.
- Press the Velux KLR 200 control too — both should sniff.
- We then move to Phase 2 (frame decoding / parser).

If no frames appear:
- Likely culprits in order: (a) CC1101 sync hunt requires more
  preamble than PQT=0 allows, (b) AGC settings wrong for short
  bursts, (c) MDMCFG2 sync mode bits wrong, (d) the sync-encoding
  trick has an off-by-one I didn't catch.
- Diagnostic: enable `furi_log` in `phy_rx_task` to dump raw FIFO
  bytes; we can then verify on a desktop whether sync ever triggers.

### Open questions accumulating (see SUMMARY.md §4)

Top-3 to resolve early:
1. Async serial vs sync serial on CC1101 — measure on hardware.
2. CC1101 RX bandwidth — sweep 135 / 203 / 270 kHz.
3. Long-preamble TX length — capture an actual remote first to measure.

---

## Phase 2 — frame decoding (DONE 2026-05-12)

`src/iohc/crc.{c,h}` (CRC-16/KERMIT) + `frame_parse.{c,h}` produce structured
`IohcParsedFrame` from raw bytes. UI + CSV log carry parsed fields.
Verified on 119 frames across Somfy + Velux: 100% CRC OK.
Captures saved under `docs/phase2_*.csv`.

## Phase 3 — install-key extraction (DONE 2026-05-12)

Both Somfy + Velux keys extracted from a single PROG / rings-button capture.
Algorithm: AES-128-CFB with the public `transfer_key` constant + IV built
from source address. Keys mathematically verified via HMAC of unrelated
captured frames matching on-air HMAC byte-for-byte.

- Somfy `A58E29`: `BC78370AAB8AC433E41B7F5B0B581887`
- Velux `AA4E44`: `A2B1A89FB8C373BC7569DCF82334AD11`

See `PHASE3_RESULTS.md` and `tools/iohc_key_decrypt.py`.

## Phase 4 — 1W TX (DONE 2026-05-12)

Forged frames from the Flipper successfully move the Somfy shutter at
`0001BF`. Implementation:

- `src/iohc/aes.{c,h}` — minimal AES-128 ECB encrypt
- `src/iohc/hmac_1w.{c,h}` — IV builder + HMAC per iohcCryptoHelpers
- `src/iohc/frame_build.{c,h}` — frame factory: CRC + HMAC + UART encoder
- `src/radio/radio_cc1101.c` — `iohc_radio_tx_frame()` with full TX path
- `src/state/tx_state.{c,h}` — per-(src) seq tracker, updated from RX
- `src/state/tx_runner.{c,h}` — 4-rep burst sender
- `src/ui/view_tx_menu.{c,h}` — TX submenu (UP/DOWN/STOP)

Three load-bearing bugs hit and fixed during Phase 4 implementation:

1. **No chip-ready wait before SPI transactions.** Canonical Flipper cc1101
   driver waits for MISO low before every SPI op; I omitted this. Caused
   silent corruption of chip state during TX. Added `cc1101_wait_ready()`
   gating every SPI access.
2. **CCA blocking STX.** `MCSM1.CCA_MODE = 0b11` ("don't TX if channel busy")
   was set to 0x30 in our register table. Because the iohc channel always
   has ambient traffic, every STX was being denied; chip went to RX_RST
   instead of TX. Changed `MCSM1 = 0x00` (CCA always allows TX).
3. **`furi_hal_spi_bus_trx` silently truncates large bursts.** Trying to write
   33 FIFO bytes via single `bus_trx` only landed 15 bytes in the chip
   (consistently). Switched to per-byte single-access writes — slower but
   reliable.
4. **HMAC frame_data was wrong for button commands.** For Pair/Remove
   (cmd=0x2E/0x39) the HMAC `frame_data` is `[cmd, data]` = 2 bytes.
   For button commands (cmd=0x00 with the p0x00_14 struct) it's 7 bytes:
   `[cmd, origin, acei, main[0], main[1], fp1, fp2]`. Updated frame builder
   to feed `cmd + (everything before seq)` as `frame_data` — works for both.
   Verified against a captured UP frame byte-for-byte.

### Phase 4 limitations (carry into Phase 5)

- ~~PA table write breaks RX.~~ **Fixed.** Root cause was two-fold: (a) doing
  the PA write in the same SPI session as register writes + calibration
  (canonical Unleashed pattern uses separate SPI acquire/release per phase);
  (b) `furi_delay_ms(1)` after `SCAL` was insufficient — calibration can
  take longer. Replaced with `cc1101_wait_status_state(IDLE)` polling SNOP
  until chip reports IDLE. FAP now ships at `PATABLE[0] = 0xC0` = +10 dBm,
  range dramatically improved.

---

## Phase 5 — Flipper as independent paired emitter (DONE 2026-05-13)

**Identity model:**
- File `/ext/apps_data/iohc_flipper/identity.bin` (21 bytes)
- Generated randomly on first run via `furi_hal_random_fill_buf`
- Format: src(3) + install_key(16) + seq(2 MSB-first)
- Persists across launches, backed up to `docs/flipper_identity.bin`

**User's current Flipper identity:**
```
src_addr   : 96 D4 B6
install_key: 07D1D7B720B6B5BAC1C51841DCFE3A5A
seq        : (advances on each TX; periodically saved to disk)
```

**Pair-broadcast sequence:** When user selects "Pair to motor" from main
menu, FAP TXes 4× `0x39` announce + 4× `0x30` install-key broadcast.
The motor in PROG-accept mode (held PROG on its real Somfy remote) stores
Flipper's src in its accepted-emitters list.

**Files:**
- `src/state/identity.{c,h}` — load/generate/save + next_seq
- `src/iohc/key_encrypt.{c,h}` — AES-128 wrap install_key with transfer_key
- `src/iohc/frame_build.c` — `iohc_frame_build_pair_0x30` (msglen=28, no HMAC, includes encrypted key)
- `src/state/tx_runner.c` — `iohc_tx_send_pair()` and `iohc_tx_send_button()`

## Phase 5.5 — Full UI overhaul (DONE 2026-05-13)

Replaced minimal sniffer-only UI with a proper view dispatcher hierarchy.

**Main menu** (`src/ui/view_main_menu.{c,h}`):
- Sniff & capture
- Saved shutters
- All shutters
- Pair to motor
- Live sniffer
- Identity

**Capture wizard** (`src/ui/view_capture.{c,h}`):
- Listens for directed (non-broadcast) dst addresses
- Stores up to 8 distinct addresses per session
- LEFT/RIGHT cycle through captured entries; OK saves current
- Hint UX: shows "Only broadcast seen — Velux: gear on back, Somfy: any button"
  when no directed dst found yet

**Device book** (`src/state/device_book.{c,h}`):
- File `/ext/apps_data/iohc_flipper/devices.txt`
- Format: `name,XXXXXX\n` per line, max 16 entries
- Each: 24-byte name + 3-byte motor addr

**Device list + actions** (`src/ui/view_device_list.{c,h}`, `view_device_actions.{c,h}`):
- Submenu of saved shutters
- Per-shutter actions: UP / DOWN / STOP / Pair Flipper here / Delete
- TX operations don't navigate away (stay on actions menu for multiple presses)
- Delete shows DialogEx confirmation: "Delete X? Pairing on motor stays."

**Identity view** (`src/ui/view_identity.{c,h}`):
- Shows Flipper's src + install_key + seq
- OK = Backup to `identity-YYYYMMDD-HHMMSS.bin` on SD
- Green LED + toast on success, red on failure

**Other UX:**
- Per-view back-navigation via `view_set_previous_callback` (proper hierarchy)
- LED blink (blue) on every TX, green/red on backup success/fail
- Removed misleading `◀ Back` hints (Back is a dedicated physical button)
- Text input for naming uses Flipper's built-in `text_input` module

## Phase 5 ongoing investigation — DIRECTED TX BUG

**Symptom**: TX from "Saved shutters → user-C → UP" doesn't move the motor.
TX from "All shutters → UP" (broadcast) moves user-C correctly.

**Confirmed facts** (from `/tmp/live.csv` capture 2026-05-13):
- user-C's motor address = `0001BF`
- Flipper IS paired with motor `0001BF` (broadcast works)
- Real Smoove sends 2 directed bursts (long-form msglen=24) + 3 broadcast bursts per UP/DOWN press
- Directed long-form layout: `01 43 main[2] 80 D3 00 00` (extra 7 bytes, fp1=0x80, fp2=0xD3 burst 1; fp2=0xC8 burst 5)
- HMAC algorithm verified correct via Python reproduction of captured 0x39 + UP directed frames

**Attempted fixes (none worked)**:
1. Long-form (msglen=24) instead of short-form (msglen=22) for directed TX
2. Correct `fp1=0x80, fp2=0xD3` cargo-culted from captures
3. `LPM=0` on directed first frame (matching real Smoove's directed bursts)

**Working theory**: Motors don't action button presses from directed
`cmd=0x00` frames. The directed long-form in Smoove captures is likely for
positioning/multi-emitter coordination, not button triggers. Motors only
act on **broadcast** for UP/DOWN/STOP. dst filtering for buttons isn't a
thing in this protocol.

**Implication**: Per-shutter control via single Flipper identity + dst
filtering is fundamentally not supportable. Requires **Phase 5.6: per-shutter
identities** (Option B).

## Phase 5.6 (DEPLOYED 2026-05-13 18:03) — Per-shutter identities

Implementation done. Build deployed. NOT YET FULLY VALIDATED — pending
user re-pair each shutter using its per-device identity, then test
isolation (user-C → UP moves only user-C, user-A_1 → UP moves only user-A_1).

Migration ran automatically: old devices.txt (with user-C+user-A_1 entries)
upgraded to devices.bin (with fresh identities for each). Old global-
identity pairings are intact; new per-device identities need fresh pair
runs.

### Detailed plan

### Architectural rationale

iohc is structurally equivalent to Somfy RTS at the addressing level:
**each emitter (or channel of a multi-channel remote) has its own ID,
each motor stores a list of accepted emitter IDs**. Multi-channel
iohc remotes (e.g. Velux KLF150, Somfy TaHoma) implement N channels
as N internal source addresses on a single physical device. Authentication
is per-(src, install_key) pair. On-air `dst` for non-broadcast frames is
a per-emitter slot number that we (Flipper) don't know for our paired
motor (the motor never told us — would require 2W ack).

Therefore: **per-shutter control requires per-shutter Flipper identities,
all transmissions go to the broadcast dst `00 00 3F`**. Motors filter on
src field against their paired-emitters list.

### Step-by-step implementation

#### 1. Storage format migration

Replace text `/ext/apps_data/iohc_flipper/devices.txt`
(format `name,addr_hex\n`) with binary `devices.bin`:

```
struct file_entry {
    uint8_t name_len;          // 1 byte
    char    name[name_len];    // variable, max 24
    uint8_t motor_addr[3];     // 3 bytes — observed dst, label only (broadcast TX is used)
    uint8_t identity_src[3];   // 3 bytes — Flipper identity for THIS shutter
    uint8_t install_key[16];   // 16 bytes
    uint16_t seq;              // 2 bytes, MSB first
} __packed;
// Up to IOHC_DEVICE_BOOK_MAX entries.
```

Add migration path: on FAP startup, if `devices.bin` doesn't exist but
`devices.txt` does, read txt entries, generate fresh identities for each
(since old entries have no associated identity), write `devices.bin`,
keep `devices.txt` as backup. **The user's existing "user-C" entry would
need to be re-paired** since the old global identity is bound to the
motor, not the new per-device one.

#### 2. `device_book.{c,h}` API changes

```c
typedef struct {
    char name[IOHC_DEVICE_NAME_MAX + 1];
    uint8_t motor_addr[3];
    IohcIdentity identity;   // src + key + seq
} IohcDevice;
```

Operations:
- `iohc_device_book_add(name, motor_addr)` → auto-generates identity
- `iohc_device_book_set_identity(idx, identity)` → for migration / restore
- `iohc_device_book_next_seq(idx)` → increments device's seq, persists
- `iohc_device_book_save()` → writes whole book; called after any mutation

#### 3. TX path refactor (`tx_runner.{c,h}`)

Change signatures from `IohcIdentity*` to `IohcDevice*`:

```c
bool iohc_tx_send_button(IohcDevice* dev, uint8_t button_code);
bool iohc_tx_send_pair(IohcDevice* dev);
```

Internals always use `BROADCAST_DST`. The motor address is just a label.

Keep a free function for legacy "All shutters" using the existing
`app->identity`:
```c
bool iohc_tx_broadcast_button(IohcIdentity* id, uint8_t button_code);
```

#### 4. Pair flow

In `iohc_app.c::on_main_menu` case for `Pair`:
- Becomes "Pair the global identity" (only useful for All-shutters channel)

In the per-device action menu:
- `IohcDevActionPair` calls `iohc_tx_send_pair(dev)` (already correctly
  uses dev — just route to the new API)

User flow:
1. Hold PROG on real Somfy near shutter → motor enters accept mode
2. On Flipper: Saved shutters → user-C → Pair Flipper here
3. Flipper TXes `0x39` + `0x30` with user-C's *own* identity src
4. Motor stores user-C's identity → user-C now responds to broadcasts from
   that identity but not from the other shutters' identities

#### 5. Capture flow

In `iohc_app.c::on_capture_save`:
- When saving a new shutter, generate a fresh `IohcIdentity` for it
- Use `furi_hal_random_fill_buf` for src + key (same as `identity.c`)
- Initial seq = 0
- Store in the device's `identity` field

#### 6. UI updates

- **Identity view**: change to "Identities" — list of (name, src) pairs
  plus the global identity. Tap one to view details / backup.
- **Backup action**: zip the whole `devices.bin` + `identity.bin` into
  `backup-YYYYMMDD-HHMMSS.tar` (or just copy both with the same timestamp).
- **Device actions submenu**: header includes the device's identity src
  in addition to motor addr, so user sees "user-C (motor 0001BF, me 96D4B6)".

#### 7. "All shutters" semantics

Two options, both useful in different scenarios:

- **Mode A**: TX from the global identity (current behaviour) → moves all
  motors that have the global identity in their list. User explicitly
  pairs global with motors they want this to affect.
- **Mode B**: iterate all saved shutters, TX from each one sequentially.
  Slower (~100 ms × N) but works without a separate global pairing.

Implement Mode A first (no extra plumbing). Add Mode B as a settings
toggle later if user wants it.

### File-by-file change list

| File | Change |
|---|---|
| `src/state/device_book.h` | Add `identity` field to `IohcDevice` |
| `src/state/device_book.c` | Binary file I/O, migration from txt, generate identity on add |
| `src/state/tx_runner.h` | Functions take `IohcDevice*` instead of `IohcIdentity*` |
| `src/state/tx_runner.c` | Always use broadcast dst; pull src/key/seq from device |
| `src/ui/view_capture.c` | (no change; still emits dst to FAP for save) |
| `src/iohc_app.c` | Call new APIs in `on_device_action`, `on_capture_save` |
| `src/ui/view_identity.{c,h}` | Optional: rename to view_identities, show list |

Estimated LOC: ~300–400 lines net change.

### Test plan (with user's 2-shutter setup)

1. **Migrate**: launch FAP. Should detect old `devices.txt`, migrate to
   `devices.bin`, keep entries with fresh identities. Existing pairings
   are invalidated for the per-shutter model but the global identity
   pairing with user-C is preserved.
2. **Pair shutter 1**: in user-C's action menu, Pair Flipper here (after
   PROG on real remote 1). Motor 1 now has user-C's identity in its list.
3. **Pair shutter 2**: capture shutter 2's address, save as "kitchen",
   PROG on real remote 2, Pair Flipper here. Motor 2 has kitchen's
   identity.
4. **Verify isolation**: user-C → UP → only motor 1 moves. kitchen → UP →
   only motor 2 moves. Mutual independence.
5. **Verify "All shutters"** (Mode A): global identity is still paired
   with motor 1 (from earlier pairing). UP → only motor 1 moves.
6. **Re-pair global** with both motors → UP → both motors move.
7. **Real remotes** keep working independently throughout.

### Risks / open questions

- **Storage format migration**: doing it badly could brick the device
  book. Mitigation: keep `devices.txt` as fallback for one release.
- **Identity proliferation**: each shutter = 21 bytes identity + ~30
  bytes book entry. 16 shutters max = 816 bytes. Tiny.
- **Backup complexity**: need to back up `devices.bin` (now contains
  identities, not just labels). User must be told to back this up after
  pairing each shutter.
- **Mode B "All shutters"**: 4 sequential bursts × 4 reps × ~100 ms ≈
  1.6 s for 4 shutters. Acceptable but noticeable. UI could show
  per-shutter progress.

### Phases beyond 5.6

- **6a** — **Vendor field per device** (~30 min code):
  - Add `uint8_t vendor` to `IohcDevice` (Somfy=0x43, Velux=0x61).
  - Auto-detect at capture time from sniffed frame's payload byte 1.
  - `tx_runner.c` uses `device->vendor` instead of hardcoded `IOHC_VENDOR_SOMFY`.
  - Existing entries: default to 0x43 on load (backward-compatible).
  - Migration: when loading old `devices.bin`, set vendor=0x43 for all.
  - File format bump: bump `BIN_VERSION` to 2, add 1 byte per entry
    (entry size 49 → 50). Reader handles v1 and v2.

- **6b** — **Velux pairing flow** (NOT BLOCKED — corrected 2026-05-13):
  - Velux windows DO enter accept-new-emitter mode when the user presses
    the **gear button** on a paired Velux remote near them. The ~30 s
    identify cycle also opens the accept window for new emitters.
  - User has confirmed successfully pairing a Somfy remote to a Velux
    shutter yesterday (= Phase 3 cross-pair incident, now understood as
    intentional behaviour, not a bug).
  - The existing `iohc_tx_send_pair_dev` Just Works for Velux too (same
    0x39 + 0x30 sequence, vendor byte change only affects button frames
    not pair frames).
  - User flow for Velux: gear on Velux remote near target window → on
    Flipper, Saved shutters → entry → Pair Flipper here.

- **7**: Encrypted on-device storage (enclave-wrapped identities)
- **8**: Unleashed app catalog submission (icon, README, etc.)
- **9**: 2W with SX1262 add-on (motor acks, FHSS pairing)
- **10**: Home Assistant bridge via USB CDC or ESPSomfy port

### Live-scan workflow for Velux info (no commitment to TX, just maps remotes/windows)

User offered to live-sniff all 3 Velux remotes. Useful for documentation
even if we can't TX to them:

1. Flipper → Live sniffer (or Sniff & capture for labelled save)
2. Press each remote's **gear button** (back, small recessed) for 2–3 s
   with stylus/pin. Window does ~30 s identify cycle (safe, no reprog).
3. Each gear emission produces `cmd=0x2E` directed frames to each window
   that remote knows about → maps remote ↔ windows.
4. Pull log, identify each remote's src + each window's NodeID.

Already done previously: user-A's BG-RC011-01 (src `AA4E44`) was sniffed
during Phase 3 — windows seen `0000BF`, `0000FF`, `00037F`. But that's
3 windows for ONE remote — user-A supposedly only has 2 windows. So
the remote knows more than just his room? Worth re-scanning to confirm
which remote(s) own which window addresses.

## User test bench status (2026-05-13 17:15)

- Flipper Zero "Qu3r" running latest FAP build
- Paired with one Somfy shutter: user-C (addr `0001BF`, "user-C" in device book)
- Has **second Somfy remote + shutter** offered for testing (not yet captured)
- Velux side: BG-RC011-01 remote in hand, install key extracted in Phase 3
  but no Velux TX yet (Phase 6, separate vendor byte 0x61)

**Next physical step before Phase 5.6 code**:
- Validate broadcast = "all paired motors react" by pairing Flipper with
  the second Somfy motor, then testing "All shutters → UP" — both should
  move together. Confirms the broadcast model.

**Open / parked items:**
- Velux TX (Phase 6)
- 5-burst Somfy emission parity (eliminates seq catch-up)
- Encrypted on-device key storage (Phase 7)
- Unleashed app catalog submission (Phase 8: icon, README, screenshots, license)
- 2W support via SX1262 add-on
- ESPSomfy / Home Assistant bridge (notes only)

## Recent code-level findings worth remembering

1. **`furi_hal_spi_bus_trx` silently truncates large bursts**: writing >16
   bytes via single call only delivers ~15 bytes. Workaround: write FIFO
   bytes one at a time using single-access writes.

2. **CC1101 PA table write breaks RX if done in same SPI session as
   calibration**: must be in separate `furi_hal_spi_acquire`/release with
   `cc1101_wait_status_state(IDLE)` between SCAL and SRX.

3. **CC1101 status-register reads (MARCSTATE at 0x35) require BURST flag**
   to distinguish from STX strobe (also at 0x35). Use opcode `0xF5`
   (READ|BURST|0x35).

4. **MCSM1.CCA_MODE = 0b11 (default 0x30) blocks STX** when there's any
   ambient RF traffic. Must set CCA_MODE = 0b00 (MCSM1 = 0x00) to TX on
   busy iohc channel.

5. **CC1101 chip-ready wait**: must poll MISO low before each SPI
   transaction. Canonical Flipper cc1101 driver does this via
   `cc1101_wait_ready`. Skipping causes silent state corruption.

6. **HMAC frame_data length varies by cmd**: 0x2E/0x39 use 2 bytes
   `[cmd, data]`; cmd=0x00 button uses 7 bytes `[cmd, data, acei, main[2],
   fp1, fp2]` (short) or 9 bytes (long incl. data[2]). General rule:
   HMAC covers `cmd` + every payload byte before `seq`.

7. **iohc transfer_key constant**: `34c3466ed88f4e8e16aa473949884373` —
   public, in rspaargaren/iohcCryptoHelpers.cpp:47. Used to wrap install
   keys on the wire.

8. **iohc reserved addresses**: `00 00 3F` broadcast, `FF FF FF`
   broadcast, `FF FF FE` gateway. Avoid these when generating Flipper
   src addresses.

## Captured keys (DO NOT distribute)

```
device: Somfy Smoove Origin IO  (src A58E29)
install key: BC78370AAB8AC433E41B7F5B0B581887

device: Velux BG-RC011-01       (src AA4E44)
install key: A2B1A89FB8C373BC7569DCF82334AD11
```

## Reference paths

- Project root: `/Users/samyjacquet/Claude/flipper/somfy-io/`
- FAP source: `iohc_flipper/src/`
- Refs: `refs/iown-homecontrol/` (Velocet), `refs/iohomecontrol/` (rspaargaren),
  `refs/iown-homecontrol-esp32sx1276/`, `refs/rtl_433/`, `refs/unleashed-firmware/`
- Captures: `docs/phase{1,2,3}_*.csv|.png`
- Build: `cd iohc_flipper && ../.venv/bin/ufbt` (+`launch` to deploy)
- Tools: `tools/flipper_cli.py`, `flipper_screen.py`, `flipper_input.py`,
  `iohc_key_decrypt.py`, `iohc_frame_test.py`
- Flipper port: `/dev/cu.usbmodemflip_Qu3r1`
- Skill: `~/.claude/skills/flipper-zero/SKILL.md` (also in `claude-toolbox` repo)
- **Seq drift on real remote.** Real Smoove sends 5 bursts/press = seq+5;
  our Flipper sends 1 burst = seq+1. After Flipper TX, real remote needs
  one extra "catch-up" press for the motor to accept it again. Fix options:
  (a) send the full 5-burst pattern, (b) give the Flipper its own paired
  identity, (c) document the catch-up press.
- **No Velux TX yet.** Trivially ~30 lines: same flow with different vendor
  byte (0x61), different install key, and different dst addresses.
- **No on-device key storage.** Keys are hardcoded constants in
  `src/state/tx_state.h`. Phase 3 brief calls for encrypted NVS storage;
  deferred.
