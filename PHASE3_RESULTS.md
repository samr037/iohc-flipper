# Phase 3 — install-key extraction from a single PROG capture

**Status**: ✓ Achieved 2026-05-12 16:58, single CC1101 channel, no SX1262,
no second remote.

## TL;DR

Holding **PROG** on the back of a paired Somfy Smoove Origin IO causes the
remote to broadcast a `cmdid 0x30` frame containing its **AES-128 install
key, encrypted on-air with a publicly-known transfer key**. With the source
address and the constant `transfer_key` from rspaargaren's open-source impl,
we decrypt the on-air key offline.

```
Smoove Origin IO source addr A58E29
install key (verified)       BC78370AAB8AC433E41B7F5B0B581887
```

## Frame layout — cmdid 0x30 (pairing announce)

Source: `refs/iohomecontrol/include/iohcPacket.h:171-176`.

| offset | size | field | example value |
|---|---|---|---|
| 0 | 1 | CtrlB1 | `FC` (end=1 start=1 1W=1 msglen=28) |
| 1 | 1 | CtrlB2 | `00` |
| 2 | 3 | dst | `00 00 3F` (broadcast) |
| 5 | 3 | src | `A5 8E 29` |
| 8 | 1 | cmdid | `30` |
| 9 | 16 | **enc_key** | `59 DC 71 80 A6 21 40 CB FE C5 BA 57 D1 A5 8E 29` |
| 25 | 1 | man_id | `02` |
| 26 | 1 | data | `01` |
| 27 | 2 | sequence (MSB-first) | `1F 10` |
| 29 | 2 | CRC-16/KERMIT | `23 BB` |

Note: 0x30 does **not** carry a per-frame HMAC. Authentication of the
pairing transaction relies on the receiver being put into "accept" mode by
a physical gesture, and on the transfer-key encryption preventing simple
replay outside that window.

## Decryption recipe

Source: `refs/iohomecontrol/src/iohcCryptoHelpers.cpp:174-215` and `:47`.

1. **Transfer key** (hard-coded, public constant shared across all
   io-homecontrol implementations):
   ```
   34 c3 46 6e d8 8f 4e 8e 16 aa 47 39 49 88 43 73
   ```
2. **IV** built from the source address (3 bytes) repeated 5× plus
   `src[0]`:
   ```
   iv = src[0..2] · src[0..2] · src[0..2] · src[0..2] · src[0..2] · src[0]
        = A5 8E 29  A5 8E 29  A5 8E 29  A5 8E 29  A5 8E 29  A5
   ```
3. **AES-128-CFB128 decrypt** the on-air `enc_key` with `transfer_key` and
   this IV → cleartext install key.

A reference implementation lives at `tools/iohc_key_decrypt.py`.

## Verification (mathematical proof of correctness)

We compute the 6-byte HMAC of an unrelated captured `cmdid 0x39` frame
using the derived install key, and compare it against the HMAC the remote
transmitted. Match → key is correct.

HMAC algorithm (source: `iohcCryptoHelpers.cpp:104-167`):

1. Build a 16-byte IV from `frame_data = [cmd, data]`, `sequence`, and a
   custom 2-byte running checksum:
   - `iv[0..len(fd)-1]` = frame_data bytes (max 8)
   - `iv[len(fd)..7]` = pad with `0x55`
   - `iv[8..9]` = running checksum produced by `computeChecksum()` over
     each frame_data byte
   - `iv[10..11]` = sequence MSB-first
   - `iv[12..15]` = `0x55 0x55 0x55 0x55`
2. HMAC = `AES-128-ECB-encrypt(install_key, IV)[0..5]` (first 6 bytes).

For the captured `0x39` frame at seq `0x1F0F`:

```
frame_data = 39 00
sequence   = 1F 0F
IV         = 39 00 55 55 55 55 55 55 00 E4 1F 0F 55 55 55 55
expected   = A6 3F 55 25 7E 76
computed   = A6 3F 55 25 7E 76    ✓ MATCH
```

## What this means for the project

- **Phase 4 (1W TX) is now unblocked.** With the install key in hand, we
  can construct valid frames the motor at address `0001BF` will accept:
  - sequence number > 0x1F1F (the highest we've observed)
  - HMAC computed with the install key over `[cmd, data]` + seq
  - Standard 1W frame layout per SUMMARY.md §1
- **No SX1262 add-on needed** for Phase 4 — pure TX from the CC1101.
- The Velux remote (`AA4E44`) needs the same PROG-equivalent capture to
  extract its install key. The Velux gear button likely emits a similar
  `0x30` frame; we did not capture one in this session but we now know
  what to look for.

## Operational notes

- The install key persists across battery changes on the remote
  (typical for io-homecontrol: it's stored in the receiver and on the
  emitter; rotating it requires a re-pairing event).
- The sequence counter is per-emitter, persistent, monotonically
  increasing. To impersonate a remote we must send seq ≥ the highest the
  receiver has accepted. Receivers typically tolerate up-to-some-window
  skip-ahead but reject older counters.
- Knowing the install key gives **control authority** equivalent to
  holding the physical Smoove remote. Treat as a credential.

## Legality

The user owns the hardware and the installation. This work is
interoperability research under EU Directive 2009/24/EC and equivalent
provisions. The transfer key, frame layout, HMAC algorithm, and decryption
recipe are all already public in open-source repos (Velocet, rspaargaren,
CyrilOpenSource, rtl_433). No cryptanalysis was performed; we applied
documented algorithms to a captured frame on the user's own hardware.

## Files produced

- `docs/phase3_first_pairing_attempt.csv` — raw capture (119 frames).
- `tools/iohc_key_decrypt.py` — reproducible key derivation + HMAC
  verification.
- This document.

## Keys (in plain text — local-only, treat as credentials)

```
device: Somfy Smoove Origin IO  (src A58E29)
install key: BC78370AAB8AC433E41B7F5B0B581887   (HMAC-verified)

device: Velux BG-RC011-01       (src AA4E44)
install key: A2B1A89FB8C373BC7569DCF82334AD11   (HMAC-verified)
```

## Velux vs Somfy: emission differences

When putting the remote in "share install" mode:

- **Somfy Smoove Origin IO** — `PROG` button broadcasts a single `0x30`
  frame to dst `00003F` (broadcast). Anyone listening gets the key.
- **Velux BG-RC011-01** — `rings` button (overlapping circles icon on the
  back) emits **N directed `0x30` frames, one per paired device the
  remote knows about**, each with its own dst address and sequence
  number. In our capture the remote sent to `0000BF`, `0000FF`, and
  `00037F` — the three Velux windows in the installation.

Same enc_key payload across all three Velux frames (the remote has one
install key; it tells each paired window about it). Any single frame is
enough to decrypt.

Velux's per-window dst means the broadcast picture is leakier: a sniffer
also learns the **full topology** of paired devices.
