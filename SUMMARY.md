# io-homecontrol Protocol Summary

Source-cited synthesis of the Velocet protocol documentation, rspaargaren's
working ESP32 + SX1276 implementation, the CyrilOpenSource fork, and the
rtl_433 passive decoder. Path conventions:

- `Velocet/` = `refs/iown-homecontrol/`
- `rspaargaren/` = `refs/iohomecontrol/`
- `Cyril/` = `refs/iown-homecontrol-esp32sx1276/`
- `rtl_433/` = `refs/rtl_433/src/devices/somfy_iohc.c`

Citations are `(repo/path:line)`. Unsourced claims are flagged in Section 4.

---

## 1. Frame structure

### 1.1 On-air byte map

The frame as it sits on the air (after the radio strips UART start/stop bits
and bit-swaps each byte LSB-first — the SX1276 does this in hardware via
`PACKETCONFIG2.IOHOME_ON`; CC1101 cannot, see §3.3). Fields listed in
transmission order.

| # | Field | Size | Notes / source |
|---|---|---|---|
| 0 | Preamble | N×`0x55` | rspaargaren uses 52 bytes (~13.5 ms) for the first packet of a burst `(rspaargaren/include/board-config.h:127)`, with explicit long/short variants `(rspaargaren/src/iohcRadio.cpp:24-25)`. Velocet states "256 bit" minimum `(Velocet/docs/radio.md:32)` but notes "multiple variants of the preamble length" `(Velocet/docs/radio.md:74)`. |
| 1 | Sync word | 2 B | **`FF 33`** (MSB-first on the wire). `(Velocet/docs/radio.md:74)`, `(rtl_433/somfy_iohc.c:25)`, `(rspaargaren/include/board-config.h:129-130)`. rtl_433 searches the UART-encoded form `0x57 fd 99` (24-bit pattern = trailing `0x55` of preamble + `FF 33` encoded) `(rtl_433/somfy_iohc.c:96)`. |
| 2 | Ctrl B1 / length | 1 B | Low 5 bits = MsgLen, top 3 bits are flags. See §1.2. `(Velocet/docs/radio.md:82-86)`, `(rtl_433/somfy_iohc.c:115-122)`, `(rspaargaren/include/iohcPacket.h:37-42)`. |
| 3 | Ctrl B2 | 1 B | Extended flags + protocol version. See §1.3. `(Velocet/docs/linklayer.md:232-253)`, `(rspaargaren/include/iohcPacket.h:44-52)`. |
| 4 | Destination addr | 3 B | Byte 0 first on the wire. `(Velocet/docs/linklayer.md:263)`, `(rspaargaren/include/iohcPacket.h:35,68)`, `(rtl_433/somfy_iohc.c:144)`. |
| 5 | Source addr | 3 B | Same orientation. `(rspaargaren/include/iohcPacket.h:69)`, `(rtl_433/somfy_iohc.c:145)`. |
| 6 | CMDid | 1 B | Receiver dispatches on this byte. `(Velocet/docs/radio.md:95)`, `(rtl_433/somfy_iohc.c:149)`, `(rspaargaren/include/iohcPacket.h:70)`. |
| 7 | Payload / params | variable | `dataLen = MsgLen − 8 − (1W?8:0)`. `(Velocet/docs/linklayer.md:298-300)`, `(Velocet/docs/radio.md:98)`, `(rtl_433/somfy_iohc.c:156-164)`. |
| 8a | Sequence number | 2 B | **1W only** (Ctrl B1 bit 5 = 1). Anti-replay counter. `(Velocet/docs/linklayer.md:305-312)`, `(rtl_433/somfy_iohc.c:162)`, `(rspaargaren/include/iohcPacket.h:103,113)`. |
| 8b | MAC | 6 B | **1W only.** Truncated AES-128 output. `(Velocet/docs/linklayer.md:309-312,635-640)`, `(rtl_433/somfy_iohc.c:163)`. |
| 9 | CRC-16 (KERMIT) | 2 B | Poly `0x8408` reflected (= `0x1021` non-reflected), init 0, no final XOR, **LSB sent first**. Covers Ctrl B1 through end of payload/MAC. `(Velocet/docs/radio.md:76,164-181)`, `(rspaargaren/include/iohcCryptoHelpers.h:33)`, `(rspaargaren/src/iohcCryptoHelpers.cpp:57-77)`, `(rtl_433/somfy_iohc.c:32,170)`. |

### 1.2 Control Byte 1 ("length byte") bit layout

| Bit | 7 | 6 | 5 | 4-0 |
|---|---|---|---|---|
| Name | EndFrame | StartFrame | ProtocolMode (1=1W, 0=2W) | MsgLen |

Sources: `(Velocet/docs/linklayer.md:210-228)`, `(rspaargaren/include/iohcPacket.h:37-42)`, `(rtl_433/somfy_iohc.c:127-129)`.

- **Bits 7,6 = "Order"**: 4-state frame-relationship enum `(Velocet/docs/linklayer.md:216-222)`.
- **Bit 5 = ProtocolMode**: `1 = 1W` → 2-byte sequence + 6-byte MAC appended; `0 = 2W` `(Velocet/docs/linklayer.md:224-226)`, `(rtl_433/somfy_iohc.c:157-164)`.
- **Bits 4..0 = MsgLen**: bytes from (excl.) this byte up to and including the byte before the 2-byte CRC. CRC is **not** counted. Velocet: "excluding this first byte and 2 trailing CRC bytes" `(Velocet/docs/radio.md:82)`; rtl_433 confirms: `len = msg_len + 3` `(rtl_433/somfy_iohc.c:120-125)`.
  - Min total 11 B (MsgLen ≥ 8) `(Velocet/docs/linklayer.md:74-77)`, `(rtl_433/somfy_iohc.c:122-124)`.
  - Max total 32 B (MsgLen = `0x1F` = 31) `(Velocet/docs/linklayer.md:76-77)`, `(rspaargaren/include/iohcPacket.h:30)`.

### 1.3 Control Byte 2 bit layout

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1-0 |
|---|---|---|---|---|---|---|---|
| Name | UseBeacon | Routed | LowPowerMode | Ack | ? | ? | Protocol Version |

`(Velocet/docs/linklayer.md:232-234)`. rspaargaren's bitfield labels them `Beacon/Routed/LPM/Unk3/Unk2/Prio/Version:2` `(rspaargaren/include/iohcPacket.h:44-52)`. rtl_433 agrees with Velocet `(rtl_433/somfy_iohc.c:131-139)`.

### 1.4 Addresses

3 bytes each, source and dest. Reserved values:
- `00 00 3F` broadcast `(Velocet/docs/linklayer.md:270)`, `(Velocet/src/iown_mac.cpp:13)`
- `FF FF FF` broadcast `(Velocet/docs/linklayer.md:272)`
- `FF FF FE` gateway-originated P2P/broadcast source `(Velocet/docs/linklayer.md:276)`

Endianness: byte 0 transmitted first. rtl_433's comment "little endian presumably" `(rtl_433/somfy_iohc.c:27-28)` is misleading — its own code assembles `(b[N+0]<<16)|(b[N+1]<<8)|b[N+2]` which puts byte 0 in the MSB position of the printed integer, identical to rspaargaren's literal byte order `(rspaargaren/src/iohcPacket.cpp:62-66)`.

### 1.5 1W authenticator suffix

When Ctrl B1 bit 5 = 1:

| Offset from CMDid | Field | Size |
|---|---|---|
| `1 + dataLen` | Sequence number | 2 B |
| `1 + dataLen + 2` | MAC (truncated AES-128) | 6 B |

`(Velocet/docs/linklayer.md:305-312)`, `(rtl_433/somfy_iohc.c:160-163)`, `(rspaargaren/include/iohcPacket.h:103-104,113-114)`.

### 1.6 The three integrity layers

The brief mentions "3 distinct CRC schemes". Reconciling:

1. **Wire CRC-16/KERMIT** (poly `0x8408` reflected, init 0, LSB-first transmission). Covers Ctrl B1 → end of payload/MAC. Magic check: `crc(packet || crc_bytes) == 0`. `(Velocet/docs/radio.md:76,164-181)`, `(rspaargaren/src/iohcCryptoHelpers.cpp:57-77)`, `(rtl_433/somfy_iohc.c:170)`.
2. **AES-128 MAC** (6-byte truncated AES output). In 1W, embedded in frame; in 2W, exchanged out-of-band via `0x3C`/`0x3D` challenge/response. `(Velocet/docs/linklayer.md:466-491,635-684)`.
3. **In-IV byte checksum** (`computeChecksum`) — a custom 16-bit running checksum inserted into IV bytes 8-9 before AES, **not** a transmitted CRC. `(Velocet/docs/linklayer.md:480-491)`, `(rspaargaren/src/iohcCryptoHelpers.cpp:92-102)`.

### 1.7 Worked example

From Velocet docs `(Velocet/docs/radio.md:154,180-181)`:

```
FF 33 F8 00 00 00 3F 1A 38 0B 00 01 61 00 00 80 D8 05 00 02 A6 24 22 2E 8B A3 51 5F 52
```

- `FF 33` sync (not CRC'd)
- `F8` = Ctrl B1 = `0b11111000` → End=1, Start=1, Mode=1 (1W), MsgLen = `0x18` = 24
- `00` Ctrl B2
- `00 00 3F` dst (broadcast)
- `1A 38 0B` src
- `00` CMDid
- 13 bytes payload+seq+MAC
- `5F 52` CRC-16/KERMIT, LSB first → `0x525F`. Velocet's Python `compute_crc_8408(...)` returns `0x525F` and `0x0000` over the full frame `(Velocet/docs/radio.md:180-181)`.

---

## 2. Velocet vs rspaargaren discrepancies (cross-checked against rtl_433)

### 2.1 Sync word value
All three agree on `FF 33` (MSB first, 16-bit). `(Velocet/docs/radio.md:74)`, `(rspaargaren/include/board-config.h:129-130)`, `(rtl_433/somfy_iohc.c:25,96)`.

### 2.2 Length byte semantics
All three agree: MsgLen counts bytes after the length byte and **excludes** the 2-byte CRC. `(Velocet/docs/radio.md:82)`, `(rtl_433/somfy_iohc.c:120-125)`, `(rspaargaren/include/iohcPacket.h:38)`.

### 2.3 Address endianness
All three implement byte-0-first; rtl_433's "little endian" comment is wrong (its own code is consistent with byte 0 as MSB-when-printed). `(Velocet/docs/radio.md:107-131)`, `(rspaargaren/src/iohcPacket.cpp:62-66)`, `(rtl_433/somfy_iohc.c:144-145)`.

### 2.4 CRC polynomial / endianness
All three agree: poly `0x8408` reflected (= `0x1021` non-reflected), init 0, no final XOR, LSB sent first. `(Velocet/docs/radio.md:76,168-181)`, `(rspaargaren/include/iohcCryptoHelpers.h:33)`, `(rtl_433/somfy_iohc.c:170)`.

### 2.5 Preamble length — minor disagreement
- Velocet: "256 bit" min `(Velocet/docs/radio.md:32)` / "multiple variants" `(Velocet/docs/radio.md:74)`.
- rspaargaren: 52 bytes of `0x55` (~13.5 ms) `(rspaargaren/include/board-config.h:127)`; long-preamble first-packet variant `(rspaargaren/src/iohcRadio.cpp:24-25,346-348)`.
- Cyril: 64 bytes `(Cyril/include/board-config.h:83)`.

**Verdict**: Velocet's 256-bit figure is a minimum, not normative. Working impl uses ~50–64 bytes; first frame of a burst longer to wake LPM devices. Permissive protocol.

### 2.6 Channel assignment (1W vs 2W)
- Velocet: documents 3 channels but doesn't say which is used for which `(Velocet/docs/radio.md:50-54)`.
- rspaargaren: CH2 = 868.95 MHz marked "1W 2W" (default), CH1/CH3 marked "2W" `(rspaargaren/include/board-config.h:134-136)`. FHSS disabled by default (`MAX_FREQS=1`).
- Cyril: identical `(Cyril/include/board-config.h:90-95)`.

**Verdict**: agreement — CH2 is the default landing channel; CH1/CH3 used during 2W FHSS (2.7 ms slots, per `Velocet/docs/radio-cc1021.md:101-127`).

### 2.7 Line coding / "IoHome mode"
- Velocet: describes the line code (8N1 UART per byte, LSB-first within each byte) `(Velocet/docs/radio.md:72)` but doesn't name the SX1276 flag.
- rspaargaren: enables `RF_PACKETCONFIG2_IOHOME_ON = 0x20` `(rspaargaren/src/SX1276Helpers.cpp:147-150)`, `(rspaargaren/include/sx1276Regs-Fsk.h:838-840)`. Also sets undocumented `IOHOME_POWERFRAME = 0x10` "to avoid rx to newly detect the preamble during tx radio shutdown" `(rspaargaren/include/SX1276Helpers.h:43)`.
- rtl_433: implements 8N1 UART decoding in software via `extract_bytes_uart_8n1` `(rtl_433/somfy_iohc.c:110)`.

**Verdict**: all three describe the same wire format. SX1276 hides the UART framing in hardware; on CC1101 we must reproduce it in firmware. Critical for §3.

### 2.8 Sync size differs in TX vs RX (rspaargaren only)
rspaargaren switches `SYNCSIZE_2` for TX and `SYNCSIZE_3` for RX `(rspaargaren/src/SX1276Helpers.cpp:295,303)` — undocumented elsewhere. Likely a workaround for false sync-detect on SX1276, not a protocol statement. rtl_433 effectively does 24-bit sync (preamble `0x55` + `FF 33` encoded = `57 FD 99`) `(rtl_433/somfy_iohc.c:96)`, structurally equivalent.

### 2.9 Net assessment
**No substantive protocol disagreements.** Disagreements are presentation (rtl_433's misleading endianness comment) or implementation (preamble length, sync size at runtime, SX1276-specific bits). Follow rspaargaren for radio config and Velocet for protocol semantics — both are internally consistent with rtl_433 captures.

---

## 3. CC1101 register values (translated from SX1276 reference)

### 3.1 Source modulation parameters (rspaargaren)

| Parameter | Value | Source |
|---|---|---|
| Modulation | 2-FSK | `(rspaargaren/src/iohcRadio.cpp:141)` |
| Data rate | 38400 bps | `(rspaargaren/src/iohcRadio.cpp:139)`, `(Velocet/docs/radio.md:60)` |
| Deviation | 19200 Hz | `(rspaargaren/src/iohcRadio.cpp:138)`, `(Velocet/docs/radio.md:57)` |
| RX BW | 250 kHz with AFC | `(rspaargaren/src/iohcRadio.cpp:140)`, `(rspaargaren/src/SX1276Helpers.cpp:202-203)` |
| Carriers | 868.95 (default), 868.25, 869.85 MHz | `(rspaargaren/include/board-config.h:134-138)` |
| Sync word | `FF 33` (16-bit) | `(rspaargaren/include/board-config.h:129-130)` |
| Preamble | `0x55…0x55`, 52 B | `(rspaargaren/include/board-config.h:127)` |
| Packet length | Variable, 5-bit length prefix, max 32 B | `(Velocet/docs/radio.md:82)`, `(rspaargaren/include/iohcPacket.h:30)` |
| Line coding | 8N1 UART per byte, LSB-first within byte | `(Velocet/docs/radio.md:72)` |
| FEC | Off | not enabled `(rspaargaren/src/SX1276Helpers.cpp:142-146)` |
| Manchester | Off | `DCFREE_OFF` `(rspaargaren/src/SX1276Helpers.cpp:142-146)` |
| Whitening | Off | same |
| HW CRC | On in SX1276 PACKETCONFIG1 (CCITT), but software KERMIT CRC re-verifies | `(rspaargaren/src/SX1276Helpers.cpp:142-146)` vs `(rspaargaren/src/iohcRadio.cpp:710-718)` |
| Addr filter | Off | `(rspaargaren/src/SX1276Helpers.cpp:142-146)` |
| AGC / AFC | Auto-AGC + Auto-AFC | `(rspaargaren/src/SX1276Helpers.cpp:201)` |
| LNA | Boost ON, gain G1 | `(rspaargaren/src/SX1276Helpers.cpp:207)` |

### 3.2 CC1101 arithmetic (XOSC = 26 MHz)

**FREQ word**: `FREQ = round(f_carrier × 2^16 / f_xosc)`

| Channel | f (Hz) | FREQ | FREQ2 | FREQ1 | FREQ0 | Realized | Error |
|---|---|---|---|---|---|---|---|
| 1 | 868_250_000 | 0x2164EC | 0x21 | 0x64 | 0xEC | 868.249878 MHz | −122 Hz |
| **2 (default)** | **868_950_000** | **0x216BD1** | **0x21** | **0x6B** | **0xD1** | **868.950104 MHz** | **+104 Hz** |
| 3 | 869_850_000 | 0x2174AD | 0x21 | 0x74 | 0xAD | 869.849884 MHz | −116 Hz |

Worked: 868_950_000 × 65_536 / 26_000_000 = 2_190_289.0 → 0x216BD1.

**Data rate** (MDMCFG3 = DRATE_M, low nibble of MDMCFG4 = DRATE_E):
`DRATE = ((256 + M) × 2^E) × f_xosc / 2^28`
- For 38400: `M = 0x83 (131)`, `E = 0x0A` → 38383.5 bps (error +16.5 bps, 0.04%).
- **MDMCFG3 = 0x83**, **MDMCFG4 low nibble = 0x0A**.

**Deviation** (DEVIATN):
`f_dev = (f_xosc / 2^17) × (8 + M) × 2^E`
- For 19200: `M = 4`, `E = 3` → 19042.97 Hz (error −157 Hz, 0.8%).
- **DEVIATN = (E << 4) | M = 0x34**.

**Channel BW** (MDMCFG4 high nibble):
`BW = f_xosc / (8 × (4 + M) × 2^E)`
- rspaargaren uses 250 kHz on SX1276. Closest CC1101: `E=1, M=2` → 270.83 kHz, high nibble = 0x60.
- Tighter alternative: `E=2, M=0` → 203.1 kHz (high nibble 0x80) — slightly better sensitivity, slightly worse AFC tolerance.
- **MDMCFG4 = 0x60 | 0x0A = 0x6A** (chosen to match rspaargaren).

### 3.3 Proposed CC1101 register set (Phase 1 RX-only, 868.95 MHz, no FHSS)

> **Critical caveat**: CC1101 has **no equivalent** to SX1276's `IOHOME_ON`. Two viable workarounds:
> 1. **Async serial mode** (`PKTCTRL0 = 0x32`, GDO0 streams raw bits): reconstruct UART bytes in firmware on STM32WB55, mirroring what rtl_433 does at `(rtl_433/somfy_iohc.c:110)`.
> 2. **Synchronous serial mode** (`PKTCTRL0 = 0x12`, packet engine assembles "bytes"): faster but the 8N1 framing means on-air "byte N" is not aligned with FIFO byte N; requires bit-shift post-processing.
>
> **Phase 1 recommendation: async serial (option 1) + soft framer.** Validate against rtl_433 reference captures, then optimize for Phase 4 TX.

| Register | Addr | Value | Meaning |
|---|---|---|---|
| IOCFG2 | 0x00 | 0x06 | GDO2 = sync-detected, de-asserts at packet end / RX exit. ISR trigger. |
| IOCFG0 | 0x02 | 0x0D | GDO0 = serial data output (async mode). |
| FIFOTHR | 0x03 | 0x47 | ADC retention, FIFO_THR = 7 (33/32). |
| SYNC1 | 0x04 | 0xFF | High byte of sync. `(rspaargaren/include/board-config.h:129)` |
| SYNC0 | 0x05 | 0x33 | Low byte. `(rspaargaren/include/board-config.h:130)` |
| PKTLEN | 0x06 | 0xFF | Max length (soft-enforced to 32 B). |
| PKTCTRL1 | 0x07 | 0x00 | No addr check, no append status. |
| PKTCTRL0 | 0x08 | 0x32 | **Async serial**, no whitening, no CRC. (Use 0x12 for sync serial if soft framer too costly.) |
| ADDR | 0x09 | 0x00 | Addr filter disabled. |
| CHANNR | 0x0A | 0x00 | Channel 0 — we set FREQ directly per channel. |
| FSCTRL1 | 0x0B | 0x06 | IF freq ~152 kHz @ 26 MHz xosc. |
| FSCTRL0 | 0x0C | 0x00 | No freq offset. |
| FREQ2 | 0x0D | 0x21 | 868.95 MHz; see §3.2. |
| FREQ1 | 0x0E | 0x6B | |
| FREQ0 | 0x0F | 0xD1 | |
| MDMCFG4 | 0x10 | 0x6A | RX BW 270 kHz + DRATE_E=0xA. |
| MDMCFG3 | 0x11 | 0x83 | DRATE_M=131 → 38383 bps. |
| MDMCFG2 | 0x12 | 0x06 | 2-FSK, no Manchester, SYNC_MODE=110 (16/16 + CS). |
| MDMCFG1 | 0x13 | 0x22 | FEC off, TX-preamble 4 B (irrelevant for RX). |
| MDMCFG0 | 0x14 | 0xF8 | Channel spacing — unused (FREQ set directly). |
| DEVIATN | 0x15 | 0x34 | 19.04 kHz dev. |
| MCSM2 | 0x16 | 0x07 | RX_TIME=7 (no timeout). |
| MCSM1 | 0x17 | 0x30 | CCA_MODE=11, idle after RX/TX. |
| MCSM0 | 0x18 | 0x18 | Cal once IDLE→RX/TX, PO_TIMEOUT ~150 µs. |
| FOCCFG | 0x19 | 0x16 | Freq-offset comp: mid-band defaults. |
| BSCFG | 0x1A | 0x6C | TI default for our DR. |
| AGCCTRL2 | 0x1B | 0x43 | MAX_LNA_GAIN=000, MAGN_TARGET=33 dB. Matches rspaargaren "boost ON, G1". |
| AGCCTRL1 | 0x1C | 0x40 | AGC ref / CS rel threshold: mid. |
| AGCCTRL0 | 0x1D | 0x91 | Hysteresis L2, wait L1, filter L1. |
| FREND1 | 0x21 | 0x56 | TI 868 MHz sensitivity-optimized. |
| FREND0 | 0x22 | 0x10 | PA_POWER index 0 (RX-only Phase 1). |
| FSCAL3 | 0x23 | 0xE9 | TI recommended. |
| FSCAL2 | 0x24 | 0x2A | TI recommended. |
| FSCAL1 | 0x25 | 0x00 | TI recommended. |
| FSCAL0 | 0x26 | 0x1F | TI recommended. |
| TEST2 | 0x2C | 0x81 | TI 868 MHz sensitivity. |
| TEST1 | 0x2D | 0x35 | TI recommended. |
| TEST0 | 0x2E | 0x09 | VCO sel cal disabled. |

### 3.4 GDO mapping for ISR-driven RX

- **GDO0 (IOCFG0 = 0x0D)** in async serial mode emits raw demodulated bits; route to STM32WB55 EXTI; sample at data-rate in ISR; soft framer.
- **GDO2 (IOCFG2 = 0x06)** asserts on sync-detect, de-asserts at end-of-RX; use as "start framing" trigger.

Mirrors rspaargaren's ISR architecture `(rspaargaren/src/iohcRadio.cpp:65-102)` adapted to CC1101's 2 GDOs.

---

## 4. Open questions

1. **Best CC1101 line-coding strategy for iohc 8N1 UART** — async serial + soft framer vs. sync serial + bit-shift post-processing. Need to measure on Phase 1 hardware. See `(rspaargaren/src/SX1276Helpers.cpp:147-150)` and CC1101 datasheet "Asynchronous and synchronous serial operation".
2. **Exact CC1101 RX bandwidth** — picked 270.83 kHz to match rspaargaren's 250 kHz. A 135 kHz or 203 kHz setting may give better sensitivity. Worth sweeping empirically.
3. **Long-preamble byte count for Phase 4 TX** — rspaargaren's `LONG_PREAMBLE_MS = 1920` and `SHORT_PREAMBLE_MS = 40` are passed in suspicious "ms" units to a function that writes byte counts `(rspaargaren/src/iohcRadio.cpp:24-25,346-348)`. Real wake-LPM preamble length unverified; capture an actual remote in Phase 1 and measure.
4. **Sync-size 2 vs 3 in rspaargaren TX vs RX** `(rspaargaren/src/SX1276Helpers.cpp:295,303)` unexplained. Initial CC1101 config uses 16-bit; revisit if false locks dominate.
5. **Calibration Data triplets** at `(Velocet/docs/linklayer.md:697-700)` — three 3-byte values claimed to be CC1021 frequency words. Cross-checking them against our CC1101 FREQ word would validate the formula.
6. **rtl_433 "little endian" comment** `(rtl_433/somfy_iohc.c:27-28)` — its code is byte-0-first, the comment is wrong. Confirm with a capture where the source addr is known (printed on remote pairing label).
7. **Ctrl B2 bits 3 and 2 ("?")** `(Velocet/docs/linklayer.md:232-234)` undefined; rspaargaren labels them `Unk2/Unk3/Prio` `(rspaargaren/include/iohcPacket.h:44-52)`. Log all bits in Phase 2 and correlate with observed device behavior.
8. **`IOHOME_POWERFRAME = 0x10`** in SX1276 PACKETCONFIG2 `(rspaargaren/include/SX1276Helpers.h:43)`, `(rspaargaren/src/SX1276Helpers.cpp:147-150)` — undocumented by Semtech. Irrelevant for CC1101; flag for Phase 5 SX1262 work.
9. **SX1276 HW CRC (CCITT 0x1021) vs iohc software CRC (KERMIT 0x8408 reflected)** — rspaargaren leaves the SX1276 HW CRC on `(rspaargaren/src/SX1276Helpers.cpp:142-146)` but software-verifies KERMIT `(rspaargaren/src/iohcRadio.cpp:710-718)`. The HW CRC may be dropping malformed frames before software sees them, losing sniffer visibility. **For CC1101 Phase 1: disable HW CRC entirely; rely on software KERMIT verification only** (this is what `PKTCTRL0 = 0x32` does).
10. **Phase 5 FHSS retune budget** — Velocet quotes 2.7 ms/channel `(Velocet/docs/radio-cc1021.md:101)`. Out of scope for Phase 1 but flagged.

---

## Appendix: rtl_433 annotated frame

`(rtl_433/somfy_iohc.c:62-67)`:

```
ff33 f8 0000003f 17f52320 02ff01430105ff00 18c6 a34715cbe012 4f7f
^    ^  ^        ^        ^                ^    ^            ^ CRC
^    ^  ^        ^        ^                ^    ^ MAC
^    ^  ^        ^        ^                ^ counter (seq nr)
^    ^  ^        ^        ^ payload
^    ^  ^        ^ source
^    ^  ^ destination (incl. Ctrl B2 as first byte: 00)
^    ^ length of payload (Ctrl B1)
^ sync, not included in CRC
```

Total bytes after sync = `1 + 0x18 + 2 = 27` ✓ (matches MsgLen=24 + Ctrl B1 + 2 CRC).
