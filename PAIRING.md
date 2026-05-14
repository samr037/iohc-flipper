# Pairing procedures (Phase 3 reference)

Goal: trigger an on-air pairing handshake so the Phase 3 sniffer can
observe the installation-key exchange between an io receiver (motor /
gateway) and a new emitter.

## Somfy Smoove Origin IO — copy from existing paired remote

Source: Remote Control Express programming manual for Smoove Origin IO
(saved via WebFetch on 2026-05-12).

With both the new Smoove Origin IO and an already-paired remote near the
target motor:

1. On the **already-paired** remote: hold the **PROG** button (back of the
   remote) until the shutter does a short fore-aft jog. This puts the
   motor into "accept new emitter" mode on-air.
2. On the **new Smoove Origin IO**: hold the **PROG** button on its back
   until the shutter jogs again. Key exchange happens over the air in
   this window.
3. Verify by pressing UP / DOWN on the new remote.

Requires: a second already-paired remote, or willingness to use the same
remote to itself (one remote can't trigger its own pairing — needs two
distinct emitters or a control point on the motor).

## Velux BG-RC011-01

Different from the touchscreen KLR 200. The BG-RC011-01 has three buttons
and a small **gear icon button** on the back accessible with a stylus.

### Pairing options identified so far

1. **Via existing paired remote** (typical io remote method): on the
   already-paired Velux remote hold its pairing button, then on the
   BG-RC011-01 hold the gear button. Some Velux remotes use a long-press
   of the centre button instead. *Procedure not yet confirmed for this
   specific model.*
2. **Via Somfy TaHoma / Somfy Connect box** (per Somfy forum):
   - On the BG-RC011-01: hold the **gear button on the back for 2 s**
     using a stylus.
   - In TaHoma: configuration → add equipment → choose "Smoove Origin io"
     (the system treats it generically as an io emitter).
   - The box performs the discovery and adds the remote.
   - **This issues an on-air pairing transaction we could capture.**
3. **Via VELUX ACTIVE with NETATMO**: the back of the remote says
   "Ready for VELUX ACTIVE with NETATMO" — so if a Velux Active gateway
   is on site, it can adopt this remote, again triggering an on-air
   pairing exchange.

### What we don't know yet
- Whether the BG-RC011-01 can pair directly to a Velux motor without
  going through a gateway (some Velux io remotes can, some can't).
- The exact button gesture on the motor head — Velux motors often have
  a recessed test button accessible only with the cover removed.

## Phase 3 test plan (provisional)

1. Bring up the Phase 2 parser cleanly (done as of 2026-05-12).
2. Make sure logging captures every on-air frame at high reliability.
3. **Pick the easiest pairing flow** based on what hardware is at hand:
   - If a second Somfy remote exists → Somfy PROG-PROG (option above).
   - If TaHoma exists → BG-RC011-01 gear-button + TaHoma "add equipment".
   - If Velux Active exists → BG-RC011-01 gear-button + Active app.
4. **Only one attempt** is reasonable per pairing — the seq counter on
   either side advances and re-pairing churns receiver state. So:
   - Confirm the sniffer is solid first.
   - Pair, capture, store the binary log.
   - Parse offline at leisure.
5. Inspect the captured frames for the key-exchange transaction.
   Expected: a 2W exchange (so we may miss it without SX1262 — see open
   question below), or a known special CMDid for "set key" / "install
   key" handshakes.

## Open question — Phase 5 vs Phase 3 ordering

iohc pairing uses 2W mode (the receiver acknowledges and sends the key
back to the emitter). 2W operates with FHSS across 3 channels at 2.7 ms
per channel — the internal CC1101 (single radio) **cannot keep up** with
the channel hopping needed to receive 2W traffic during the handshake.

This means **strict Phase 3 may need the SX1262 add-on after all** —
contrary to the Phase 1–4 = CC1101-only assumption in BRIEF.md.

Possible workarounds:
- Camp on a single channel (868.95 MHz) during the pairing and hope the
  handshake includes a frame on that channel. Some iohc impls start the
  handshake on the default channel before FHSS kicks in.
- Capture only the emitter-side frames and reconstruct the key from
  what's transmitted in plaintext (probably nothing — the key is the
  whole point of the handshake).
- Defer Phase 3 until SX1262 hardware is available.

This needs a deliberate decision before triggering a one-shot pairing.
