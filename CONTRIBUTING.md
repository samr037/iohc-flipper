# Contributing to iohc-flipper

Thanks for the interest! A few ground rules to keep the project moving
quickly without breaking the hardware we control.

## Scope

This is an **interop research / personal-use** project. Issues and PRs are
welcome for:

- Bug fixes in the FAP code
- Additional vendor support (capture a real remote, document the bytes, open
  an issue with the captures attached)
- Documentation improvements
- Hardware compatibility (Flipper firmware variants, SX1262 add-on, etc.)

Out of scope:

- Anything that helps target hardware you don't own
- Distribution of captured install_keys, MACs, or other crypto material from
  installations that aren't yours

## Captures and crypto material

Never commit real install_keys. Use the `redacted` placeholder for the
16-byte enc_key field of cmd 0x30 frames when sharing captures.

If you want to demonstrate a frame structure, redact the enc_key + MAC
fields and keep only the structural bytes (Ctrl B1/B2, dst, src, cmd,
payload positions, seq, CRC).

## Adding a new vendor

The system is vendor-agnostic at the wire level — both `vendor` (button
frame payload[1]) and `man_id` (cmd 0x30 byte[25]) are stored per-device
as raw observed bytes. If your motor uses different values:

1. Capture a button frame from your remote (any UP/DOWN/STOP press).
2. Capture a rings/PROG-equivalent press (the cmd 0x30 frame containing
   the install_key broadcast).
3. Open an issue with both captures attached (redacted as above).
4. We'll add the new (vendor, man_id) mapping to
   `iohc_man_id_default_for_vendor()` in `iohc_flipper/src/state/device_book.c`
   so it auto-fills correctly on save. Other code paths are already
   data-driven from the per-device byte.

## Code style

- C: ANSI-friendly, no GNU extensions where avoidable, BSD-style braces.
- Comments where the WHY isn't obvious. Don't restate the WHAT.
- Cite sources for protocol decisions (links to the upstream RE project,
  page references for manuals, or "captured here" with a sample frame).
- Don't introduce vendor-specific branches in TX code. Read raw bytes from
  the device book.

## Hardware testing

- Every change that touches RF (radio init, frame builder, HMAC, TX path)
  must be physically tested against real Somfy and real Velux motors
  before being merged.
- The repo has the `flipper_cli.py` and `flipper_screen.py` helpers in
  `tools/` to make remote testing easy.

## License

MIT — see [LICENSE](LICENSE). Contributions are made under the same.
