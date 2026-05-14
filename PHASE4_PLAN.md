# Phase 4 — 1W transmission

Per the brief: design + timing budget must be reviewed before any TX C
code lands. This doc proposes the architecture and the worst-case
execution paths.

## Goal

Construct and transmit valid io-homecontrol 1W frames from the Flipper's
internal CC1101, indistinguishable on-air from the real remote's
emissions, such that a paired motor accepts the command and moves.

First concrete TX operations (deliberately scoped narrow):
1. UP / DOWN / STOP to the Somfy shutter at `0001BF`, src impersonating
   `A58E29`, signed with key `BC78370AAB8AC433E41B7F5B0B581887`.
2. OPEN / STOP / CLOSE to a Velux window (e.g. `00037F`), src
   `AA4E44`, signed with key `A2B1A89FB8C373BC7569DCF82334AD11`.

## TX architecture

### Same FIFO mode, same sync, opposite direction

We keep the chip in normal packet mode with:
- `SYNC1/SYNC0 = 0x57FD` (auto-prepended on TX by the chip's packet engine)
- `MDMCFG2.SYNC_MODE = 0b110` (16/16 with carrier sense — affects RX, irrelevant on TX but compatible)
- `PKTCTRL0 = 0x00` (FIFO, fixed length, no whitening, no HW CRC — we
  software-compute CRC-16/KERMIT into the iohc frame)

CC1101 in TX mode automatically prepends:
- Preamble bytes (count = `MDMCFG1.NUM_PREAMBLE` × 1B). We pick
  `NUM_PREAMBLE = 0b111 = 24 bytes` (≥ Velocet's 256-bit recommendation).
- Sync word (16 bits = `01010111 11111101`).

After sync, the chip emits whatever bytes we loaded into the TX FIFO.

### Bit-stream alignment (the tricky part — mirror of Phase 1)

Recall from `TIMING_BUDGET.md` and `SUMMARY.md`:
- iohc bytes on the wire are UART 8N1 (10 bits per byte: start=0, 8 data
  LSB-first, stop=1).
- The 16-bit sync `0x57FD` represents:
  - 4 preamble bits (last bits of the preamble's alternating stream)
  - the full UART-wrapped iohc `0xFF` (10 bits: `0 11111111 1`)
  - 2 more bits: the UART start (`0`) and first data bit (`1`) of the
    UART-wrapped iohc `0x33`.

So **after the chip finishes transmitting sync**, we still owe the
receiver:
- the remaining 8 bits of the wrapped `0x33` (d1..d7 + stop) = `1 0 0 1 1 0 0 1`
- then UART-wrapped iohc bytes for each frame byte (Ctrl B1, Ctrl B2,
  dst, src, cmd, payload, seq, MAC, CRC) — 10 bits each
- (no trailing — CC1101 stops transmitting when FIFO empties + TX done)

Translating that to FIFO bytes:
- **FIFO byte 0 = `0x99`** (the trailing 8 bits of wrapped `0x33` =
  `10011001` MSB-first). This is the same `0x99` that rtl_433 hunts for.
- **FIFO byte 1+ = UART-encoded iohc payload, bit-packed MSB-first**.

The UART encoder takes the array of iohc bytes (max 32 incl. CRC) and
emits a bit stream where each iohc byte becomes 10 bits: `0`, b0, b1,
…, b7, `1`. Then the bit stream is packed into 8-bit FIFO bytes
MSB-first.

For a 32-byte iohc frame, the encoded bit stream is 320 bits. Adding the
8-byte residue (`0x99`) up front gives 8 + 320 = 328 bits = 41 bytes.
**Always ≤ 41 bytes**, well under the 64-byte CC1101 TX FIFO. Single
FIFO load per frame; no SPI mid-TX needed.

### Frame factory (pure C, in `src/iohc/`)

```
input:  src[3], dst[3], cmd, payload[N], one_way, seq16, install_key[16]
step 1: build the raw iohc byte array
        [CtrlB1 (with msglen)] [CtrlB2] [dst] [src] [cmd] [payload]
        for 1W: + [seq HI][seq LO] (MSB-first)
                + [MAC[6]]      ← computed via AES-128-ECB
step 2: compute CRC-16/KERMIT over [CtrlB1 ... last MAC byte]
        append [CRC LO][CRC HI]   ← LSB sent first on the wire
step 3: UART-encode each byte into 10 on-air bits
        emit start=0, then byte bits LSB-first, then stop=1
step 4: prepend the 8-bit `0x99` residue
step 5: MSB-first bit-pack into a uint8_t[42] buffer + total bit length
output: fifo_buf, fifo_len_bytes
```

The HMAC is `AES-128-ECB(install_key, IV)[0..5]` where `IV` is built per
`iohcCryptoHelpers.cpp:104-138` from `[cmd, data]` (frame_data) plus the
seq. STM32WB55 has hardware AES; we'll use the SDK's `furi_hal_crypto_*`
or directly drive the AES peripheral. For Phase 4 simplicity we'll
start with a small software AES implementation and migrate to HW AES
in Phase 5 if perf demands.

### CC1101 TX driver

New entry point: `iohc_radio_tx_frame(buf, len)`. Sequence:

1. Acquire SPI.
2. Strobe `SIDLE` to leave RX mode.
3. Strobe `SFTX` to flush any stale TX FIFO content.
4. Burst-write `[CC1101_FIFO|BURST, buf[0..len-1]]` via SPI.
5. Strobe `STX` to enter TX mode (chip emits preamble + sync + FIFO bytes).
6. Wait for TX done. Two options:
   - Poll `MARCSTATE` until back to IDLE (~10 ms TX). Simple but blocking.
   - Re-purpose GDO0 with `IOCFG0 = 0x06` (asserts on sync sent in TX,
     de-asserts on packet end). Then wait for falling edge via EXTI.
   We'll use the **polling option for Phase 4** — simpler and the TX path
   doesn't share an ISR with the RX path during this phase.
7. Strobe `SRX` to return to RX listening mode.
8. Release SPI.

### Retransmission scheduling

Real remotes emit each frame **4 times in succession** (Phase 2 captures
showed this consistently). Inter-frame gap: ~24 ms in captures. The
first frame uses LPM bit in Ctrl B2 (wake bit); the 3 retransmissions
have LPM = 0.

We mirror this:
- Frame 1: ctrl_b2 bit 5 = 1 (LPM)
- Frames 2–4: ctrl_b2 bit 5 = 0
- 24 ms gap between frames
- Each frame TX = ~10 ms airtime, leaving 14 ms gap

For some commands (per the Phase 2 Somfy captures), the remote sends a
*sequence* of distinct bursts (UP press = 5 bursts × 4 reps = 20 frames).
We start with **single-burst × 4 reps** and only expand if the receiver
doesn't accept it.

## Timing budget

| Op | Cost | Margin |
|---|---|---|
| Build iohc frame (CRC + HMAC) | ~500 µs (software AES) | < 24 ms gap, fine |
| UART-encode + bit-pack | ~100 µs | fine |
| SPI FIFO write (42 bytes @ 4 MHz) | ~85 µs | fine |
| TX airtime (42 bytes × 8 bits / 38400 bps) | ~8.7 ms | dominates |
| MARCSTATE poll loop | ~2 ms typical | fine |
| RX↔TX state changes (SIDLE/SFTX/STX/SRX) | ~500 µs total | fine |
| **Total per frame** | **~12 ms** | |
| 4 retransmissions w/ 24 ms gaps | ~96 ms total airtime | |

ETSI 868.0–868.6 MHz sub-band (g1) allows continuous TX at ≤ 25 mW
ERP. We're on 868.95 in sub-band g3 which is **1 % duty cycle** limited.
4 × 12 ms = ~48 ms TX in any 100 ms is briefly above 1 %, but rolling
1-hour average is ~50 ms/hr ≪ 36 s/hr limit. Compliant.

PA setting: CC1101 PATABLE `0xC0` → +10 dBm at 868 MHz. Same level as
the real remote (we'll measure RSSI on the receiver side to confirm).

## Risks & mitigations

1. **Seq counter race**: any seq we send sticks at the receiver as the
   new "last seen for A58E29 / AA4E44". The real remote's NVRAM counter
   keeps incrementing independently; if our forged seq is far above the
   real remote's next-emit, the real remote gets locked out until it
   catches up.

   *Mitigation*: pick seq = `(last_observed + 1)` only. Phase 4 stores
   the last observed seq per (src, dst) tuple in app state. Acceptable
   because the real remote is also incrementing from there.

2. **Receiver tracks seq per (src, dst)**: we don't know exactly. May be
   per (src) only. If per (src, dst), targeting Velux window `00037F`
   doesn't affect Somfy shutter `0001BF`'s view. If per (src), our TX
   advances the counter for both. Need to test.

3. **HMAC mismatch**: if `compute_checksum` or the IV layout has a corner
   case we missed, the receiver will silently drop. *Mitigation*:
   Phase 3 already verifies HMAC computation against captured frames in
   `iohc_key_decrypt.py:hmac_1w`. Reuse that exact algorithm.

4. **TX during RX → missed frames**: while we're TXing, the RX is offline.
   No issue for one-shot TX, but our sniffer loses ~50 ms of frames per
   button press. Acceptable.

5. **Cross-vendor accidental control**: sending a forged Somfy command
   could affect any device paired to A58E29, including the (now-removed)
   Velux. Since we un-paired earlier, this is fine. *Stay aware* if we
   ever re-introduce cross-pairing.

6. **PA boot-up calibration**: CC1101 needs a calibration cycle between
   RX and TX. We use `MCSM0 = 0x18` (auto-cal on IDLE → RX/TX), which
   adds ~720 µs. Already in the timing budget.

## UI

Add a "TX" sub-menu accessible from the main sniffer screen (long-press
Right or a dedicated entry). Three hardcoded entries to start:

```
TX UP    → Somfy 0001BF (seq next, key Somfy)
TX DOWN  → Somfy 0001BF
TX STOP  → Somfy 0001BF
```

Confirmation prompt before each TX. Logs the transmission to the same
CSV as captures, with a `TX` flag column (to be added — backward
compatible default `0`).

Velux TX entries deferred to "Phase 4.5" once Somfy works end-to-end.

## What's NOT in Phase 4

- HW AES on STM32WB55 (Phase 5 or later optimization).
- Per-key encrypted storage on Flipper NVS — we hardcode keys for now.
- 2W ACKs from receiver. We TX 1W only and assume success.
- The 5-burst structure Somfy uses for some commands. Start with the
  simpler single-burst × 4 reps.
- FHSS. Not needed for 1W TX.

## Files to be created / modified

- `src/iohc/crc.{h,c}` — already exists, reused
- `src/iohc/frame_build.{h,c}` — **new**: frame factory + UART encoder + bit packer
- `src/iohc/aes_sw.{h,c}` — **new**: small AES-128 (or use a vendored impl)
- `src/iohc/hmac_1w.{h,c}` — **new**: HMAC per iohc spec
- `src/radio/radio_cc1101.{h,c}` — extend with `iohc_radio_tx_frame()`
- `src/ui/view_tx.{h,c}` — **new**: TX menu
- `src/iohc_app.c` — wire in TX view and dispatch
- `src/state/seq_track.{h,c}` — **new**: per-(src,dst) last-seq tracker

About 600 lines of C added.

## Acceptance criteria

1. Pressing "TX UP" on the Flipper makes the Somfy shutter at `0001BF`
   move up.
2. Pressing "TX DOWN" then "TX STOP" stops it mid-travel.
3. The real Smoove Origin IO still works after Flipper TX (no seq
   lock-out).
4. The sniffer captures the Flipper's own emissions and parses them as
   if they were the real remote (proves on-air identity).

Awaiting review before implementation.
