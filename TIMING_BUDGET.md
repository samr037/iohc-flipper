# Phase 1 — RX architecture & timing budget

Per the working agreement in `BRIEF.md`: this document must be reviewed
before any ISR / register-init C code is written.

## Hardware constraint discovered

The Flipper Zero **only exposes CC1101 GDO0** (on PA1, alt fn TIM2 CH2).
GDO2 is not wired to the STM32. Sources:

- `refs/unleashed-firmware/targets/f7/furi_hal/furi_hal_resources.h:130-131`
  defines `CC1101_G0_GPIO_Port = GPIOA`, `CC1101_G0_Pin = LL_GPIO_PIN_1`.
- No `CC1101_G2_*` macros exist anywhere in the firmware tree.
- The Flipper stock RAW subghz reader uses PA1 with `GpioAltFn1TIM2` for
  TIM2 input-capture timing extraction
  (`furi_hal_subghz.c:523, 772`).

This invalidates the SUMMARY.md §3.3 plan that used GDO2 for sync-detect.
We have one pin for everything.

## Three candidate RX architectures

### A. Pure async serial + per-bit sampling (the obvious-but-bad plan)

- CC1101 in async serial mode, `PKTCTRL0 = 0x32`. GDO0 streams raw demodulated
  bits at the data rate; no clock recovery from the chip.
- Sample GDO0 at 38400 Hz (or 2× for oversample) using a STM32 timer ISR
  to drive a soft bit clock + sync hunter + 8N1 unframer.
- **Bit period = 1 / 38400 = 26.04 µs.** That's the available time between
  ISR samples at 64 MHz CPU = **1667 cycles per ISR window.**
- **Cost per ISR**: GPIO read (~5c) + shift register update (~5c) +
  sync-pattern compare (~10c) + state machine dispatch (~15c) + ISR entry/exit
  (~50c with FreeRTOS preemption) ≈ 85 cycles. Headroom is fine on paper.
- **Risk**: clock recovery. Without a chip-provided clock, our sample timer
  drifts vs. the transmitter's actual bit rate (the CC1101 doesn't lock to
  the TX clock in async mode — it just streams demod output). Over 32 bytes
  (max frame = 320 bits = 8.3 ms), even 100 ppm drift = 833 ns ≈ 3% of a bit.
  Edge cases will accumulate sample errors. Doable but fragile.
- **Verdict**: rejected as primary. Fallback only.

### B. CC1101 sync serial mode (the SX1276 design transplant)

- CC1101 outputs serial data on one pin, recovered serial clock on another.
- **Cannot use on Flipper**: GDO2 is not wired. Both pins required for sync mode.
- **Verdict**: rejected, hardware-blocked.

### C. CC1101 normal/FIFO mode with sync-encoding trick (the right answer)

This is the proposed design. Walk-through:

#### Encoding insight

iohc on-air bytes use UART 8N1 framing: each byte = start(0) + 8 data
bits LSB-first + stop(1) = **10 on-air bits per iohc byte**.

The iohc sync `FF 33` (Velocet/radio.md:74) when UART-wrapped becomes:

```
preamble end:  ... 0 1 0 1   (any tail of alternating 0x55 stream)
wrapped 0xFF:  0 1 1 1 1 1 1 1 1 1   (start + 8 ones + stop)
wrapped 0x33:  0 1 1 0 0 1 1 0 0 1   (start + 0x33 LSB-first + stop)
```

Concatenated, the last 24 bits before the data payload are:

```
0 1 0 1   0 1 1 1 1 1 1 1 1 1   0 1 1 0 0 1 1 0 0 1
\_57_/    \____ FD ____/    \____ 99 ____/
```

→ on-air bytes `0x57 0xFD 0x99`, which is exactly what `rtl_433` hunts
for at `refs/rtl_433/src/devices/somfy_iohc.c:96`. Cross-validated.

#### CC1101 config

- `MDMCFG2.SYNC_MODE = 110` (16-bit sync + carrier sense): the chip will
  trigger on a 16-bit on-air bit pattern.
- `SYNC1 = 0x57`, `SYNC0 = 0xFD`: the chip locks onto the preamble-end +
  wrapped-0xFF boundary.
- `PKTCTRL1.PQT = 0`: disable preamble quality threshold so we don't need
  CC1101 to recognize iohc's preamble as "preamble" — it'll sync-hunt the
  whole stream.
- `PKTCTRL0 = 0x00` (normal mode, no whitening, no CRC, fixed packet length)
  or `0x02` (variable length, but we ignore the chip's idea of length).
- `PKTLEN = 0xFF` (max). We soft-stop on iohc length byte.
- `IOCFG0 = 0x06`: GDO0 asserts on **sync word received**, de-asserts at
  packet end. This is our only ISR.
- `FIFOTHR = 0x47`: FIFO threshold at 33/32 bytes — fires GDO0 on FIFO above
  threshold (also via IOCFG0 alternate). Actually we'd want **two**
  events on one pin; we can poll FIFO via SPI status from a task instead.

#### Software pipeline

```
[CC1101 hardware]
  ├─ AGC + FSK demod + bit slicer (analog)
  └─ Sync hunt for 0x57FD → assert GDO0 → fill FIFO with following bits

[EXTI on PA1 rising edge]  → very small ISR (~50 cycles):
  ├─ timestamp_us = DWT->CYCCNT / 64
  ├─ rssi_raw = read CC1101 RSSI status reg via SPI (skipped if mid-rx)
  └─ post 'sync_detected' message to phy task queue

[phy_rx_task (FreeRTOS, priority just below ISR-deferred)]:
  ├─ poll CC1101 RXBYTES via SPI every ~1 ms (or until FIFO threshold
  │    GDO0 falls)
  ├─ burst-read RX FIFO via SPI (~64 bytes per burst, ~50 µs each at 4 MHz SPI)
  ├─ feed bytes to soft_unframer():
  │     state machine consuming 10-bit groups from a bit stream
  │     emits an iohc byte when start=0, 8 data bits, stop=1 valid
  │     emits framing-error count otherwise
  └─ on iohc length byte known → schedule end-of-frame; on stop trigger
        publish frame {timestamp, freq, rssi, bytes[]} to log+ui queues
```

#### Why this is much easier than option A

- No per-bit timer ISR. CC1101 does bit-level clock recovery in hardware
  (the FSK demod's data slicer locks to incoming edges).
- We only react at packet boundaries (sync-detect) and on a slow poll
  loop (~1 kHz) for FIFO draining.
- Soft unframer runs on byte-aligned data at ~38400/10 ≈ 3840 iohc bytes/s,
  i.e., one byte every 260 µs — trivial CPU load.
- This is the standard Flipper subghz pattern, so it composes well with
  the existing furi_hal SPI driver.

#### Cost summary (option C)

| Item | Cost | Budget | Margin |
|---|---|---|---|
| EXTI ISR latency (worst-case w/ FreeRTOS) | ~5 µs | 26 µs (1 bit) | fine — we don't need bit-level response |
| ISR work (GDO0 sync-detected) | ~50 cycles ≈ 0.8 µs | — | fine |
| FIFO threshold poll cadence | 1 ms | 13.3 ms (FIFO full) | 13× headroom |
| SPI FIFO burst read (32 bytes @ 4 MHz) | ~70 µs | 1 ms | 14× headroom |
| Soft unframer per iohc byte | ~30 cycles ≈ 0.5 µs | 260 µs (next iohc byte) | 520× headroom |
| Worst-case frame end-to-end (32 B iohc) | ~8 ms RX + 0.5 ms processing | — | comfortable |

## Risks & mitigations

1. **False syncs from environmental 868 MHz noise.** PQT=0 means we trigger
   on any 16-bit match. *Mitigation*: software CRC-16/KERMIT validation
   discards frames; we'll see false-positive rate empirically.
2. **CC1101 sync detector may require ≥4 preamble bytes despite PQT=0.**
   *Mitigation*: read CC1101 datasheet sec 17.1.1; if so, raise PQT_VAL=0
   in PKTCTRL1 (different from PQT). If still fails, fall back to option A.
3. **iohc preamble byte (`0x55`) overlap with CC1101's expected preamble
   (`10101010`).** Both are alternating bits, so the chip's preamble
   detector should naturally lock. Should be a non-issue.
4. **AGC settling on short bursts.** rspaargaren's 52-byte preamble gives
   ~13.5 ms AGC lock-in budget — comfortable. Velocet's "256-bit minimum"
   = 67 µs, that's tight. If we see missed short bursts in testing,
   increase `AGCCTRL0.SETTLE` time.
5. **FreeRTOS critical sections from other tasks (Display, NFC) blocking
   our EXTI.** The Flipper SDK installs the EXTI ISR at priority
   `NVIC_PRIORITYGROUP_4` with sub-priority 5 by default. Our ISR latency
   stays within ~10 µs even worst-case — we don't care, since we're not
   bit-banging.
6. **CC1101 SPI bus shared with the Sub-GHz subsystem state machine and
   potentially with our own driver.** Use `furi_hal_subghz_*` only for
   initial bring-up and chip select arbitration, then take exclusive
   ownership via `furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz)`
   for our register writes. Standard FAP pattern.

## Design decision

**Adopt option C.** Falls back to option A only if (a) PQT=0 doesn't allow
sync hunt without a CC1101-pleasing preamble, or (b) AGC misses short
remote bursts.

## What I'll write next (after your review)

1. **`src/radio/radio_cc1101_regs.h`** — annotated register table with
   the values from SUMMARY.md §3.3, modified for sync = `0x57FD`,
   PQT=0, IOCFG0=`SYNC_RECEIVED`.
2. **`src/radio/radio_cc1101.c`** — bring-up sequence: reset, calibrate,
   write reg block, enter RX. Uses `furi_hal_spi_*` for register access.
3. **`src/phy/phy_rx_isr.c`** — minimal EXTI ISR on PA1 + FreeRTOS task
   loop for FIFO drain.
4. **`src/iohc/unframer.c`** — pure function: bit stream → iohc byte stream
   + framing-error tracking. Unit-testable on host.
5. Glue + minimal UI + SD logger.

No FreeRTOS tasks beyond `phy_rx_task` and the UI's view dispatcher.
No software AES, no parser (Phase 2), no TX (Phase 4).

## Open questions before I write code

1. **Is PQT=0 sufficient on CC1101?** Need to read datasheet sec 17.1.1.
   If the chip requires ≥1 preamble byte detection before sync hunt arms,
   we may need to fake it by setting MDMCFG1.NUM_PREAMBLE=0 and verifying
   behavior empirically.
2. **AGC settings.** SUMMARY.md §3.3 has values copied from rspaargaren's
   SX1276 (auto-AGC, LNA boost). Need to verify these CC1101 values are
   right for 868 MHz weak-signal RX. May need to sweep.
3. **Where to put log files**. Brief says `/ext/apps_data/iohc_flipper/`.
   I'll write `frames_YYYYMMDD_HHMMSS.bin` (raw, append-only) and
   `frames_YYYYMMDD_HHMMSS.csv` (one row per frame: `ts_us,freq,rssi,hex`).
   OK?
4. **UI**. Brief says "Live frame counter and last-frame hex preview on
   the LCD." Plan: single view, three rows: title, frame count + RSSI of
   last, last frame's hex (first 16 bytes truncated to fit 128 px wide).
   OK?

Awaiting review.
