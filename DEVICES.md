# Hardware inventory

## Remotes on hand

### Somfy Smoove Origin IO
- Single-channel 1W io-homecontrol emitter, battery-powered wall remote.
- 4 buttons on the front: UP, MY (centre), DOWN, plus a hidden PROG button on the back.
- Source address observed on-air: **`A58E29`**.
- Pairs with Somfy IO motors (roller shutters, awnings, blinds) at 868 MHz.

### Velux BG-RC011-01
Photo: `docs/velux_remote_bg-rc011.png`.

- Model BG-RC011-01, "Made in Denmark by VELUX", marked "Ready for VELUX
  ACTIVE with NETATMO".
- **3-button** single-channel io-homecontrol remote: UP (▲), centre (●,
  acts as STOP/OK), DOWN (▼). **No touchscreen** — that was an earlier
  mistake from confusing this model with the KLR 200.
- A small gear/pairing button is accessible on the back via a stylus or
  pin (per Somfy forum discussion of this exact model).
- Source address observed on-air: **`AA4E44`**.
- io-homecontrol logo + CE mark on the back.

## Implications for the FAP

- Both remotes are **1W emitters**. Phase 1–4 only need the internal CC1101
  (no SX1262 add-on required until Phase 5).
- Two **different vendors** on the same protocol — captures so far confirm
  the wire format is shared (same Ctrl B1 semantics, addresses, CMDids,
  CRC-16/KERMIT). Command vocabulary at payload offset 2 is identical:
  `0x00` UP/OPEN, `0xC8` DOWN/CLOSE, `0xD2` STOP/MY. Vendor differs at
  payload offset 1: `0x43` Somfy, `0x61` Velux.
- Phase 3 (pairing/key extraction) needs a receiver in pairing mode.
  See PAIRING.md for current options.
