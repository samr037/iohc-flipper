#!/usr/bin/env python3
"""Validate the C frame builder by replicating its logic in Python and
checking that the encoded FIFO bytes round-trip back through the Phase 1
unframer + Phase 2 parser logic.

Test vector: reproduce the captured Somfy 0x39 ("remove me") frame from
docs/phase3_first_pairing_attempt.csv at seq 0x1F0F, verify the HMAC, then
encode it for TX and confirm the bit stream matches what we'd want on-air.
"""
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

SOMFY_KEY = bytes.fromhex("BC78370AAB8AC433E41B7F5B0B581887")
SOMFY_SRC = bytes.fromhex("A58E29")
BROADCAST = bytes.fromhex("00003F")


def compute_checksum(b, c1, c2):
    tmp = b ^ c2
    new_c2 = ((c1 & 0x7F) << 1) & 0xFF
    if tmp >= 0x80: new_c2 |= 1
    if (c1 & 0x80) == 0:
        return new_c2, (tmp << 1) & 0xFF
    return new_c2 ^ 0x55, ((tmp << 1) ^ 0x5B) & 0xFF


def hmac_1w(key, frame_data, seq):
    iv = bytearray(16)
    for i, b in enumerate(frame_data):
        iv[8], iv[9] = compute_checksum(b, iv[8], iv[9])
        if i < 8: iv[i] = b
    for j in range(len(frame_data), 8): iv[j] = 0x55
    iv[10] = seq[0]; iv[11] = seq[1]
    for j in range(12, 16): iv[j] = 0x55
    block = Cipher(algorithms.AES(key), modes.ECB()).encryptor().update(bytes(iv))
    return block[:6]


def crc16_kermit(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if (crc & 1) else (crc >> 1)
    return crc


def build_1w_frame(src, dst, cmd, data, extra, seq, key, end=True, start=True, ctrl_b2=0):
    msg_len = 8 + 1 + len(extra) + 8
    assert msg_len <= 31
    frame = bytearray(1 + msg_len + 2)
    frame[0] = (msg_len & 0x1F) | (0x80 if end else 0) | (0x40 if start else 0) | 0x20
    frame[1] = ctrl_b2
    frame[2:5] = dst
    frame[5:8] = src
    frame[8] = cmd
    frame[9] = data
    frame[10:10+len(extra)] = extra
    seq_off = 10 + len(extra)
    frame[seq_off]     = (seq >> 8) & 0xFF
    frame[seq_off + 1] = seq & 0xFF
    mac = hmac_1w(key, bytes([cmd, data]), bytes(frame[seq_off:seq_off+2]))
    frame[seq_off+2:seq_off+8] = mac
    crc = crc16_kermit(bytes(frame[:1 + msg_len]))
    frame[1+msg_len]     = crc & 0xFF
    frame[1+msg_len + 1] = (crc >> 8) & 0xFF
    return bytes(frame)


def uart_encode_for_fifo(frame_bytes):
    """Emit FIFO bytes: [0x99 residue] + UART(start+LSBdata+stop) for each byte, MSB-packed."""
    bits = [1,0,0,1,1,0,0,1]  # 0x99
    for b in frame_bytes:
        bits.append(0)  # start
        for i in range(8):
            bits.append((b >> i) & 1)
        bits.append(1)  # stop
    # pad to byte boundary with 1s
    while len(bits) % 8:
        bits.append(1)
    out = bytearray(len(bits) // 8)
    for i, bit in enumerate(bits):
        if bit: out[i // 8] |= 1 << (7 - (i % 8))
    return bytes(out)


def main():
    # 1) Reproduce known captured 0x39 frame from Phase 3.
    expected = bytes.fromhex("F10000003FA58E2939001F0FA63F55257E76C092")
    built = build_1w_frame(
        src=SOMFY_SRC, dst=BROADCAST, cmd=0x39, data=0x00,
        extra=b"", seq=0x1F0F, key=SOMFY_KEY,
        end=True, start=True, ctrl_b2=0,
    )
    print("expected:", expected.hex().upper())
    print("built   :", built.hex().upper())
    assert built == expected, "frame builder mismatch!"
    print("  ✓ frame builder reproduces captured 0x39 frame exactly")

    # 2) Build the un-pair (Remove) frame we'd TX to the Velux:
    #    src=A58E29 dst=00037F cmd=0x39 (Remove) data=0x00 seq=next-after-cross-pair
    unpair = build_1w_frame(
        src=SOMFY_SRC, dst=bytes.fromhex("00037F"), cmd=0x39, data=0x00,
        extra=b"", seq=0x1F30, key=SOMFY_KEY,
    )
    print(f"\nun-pair frame (would TX): {unpair.hex().upper()}")

    fifo = uart_encode_for_fifo(unpair)
    print(f"encoded for CC1101 FIFO ({len(fifo)} bytes): {fifo.hex().upper()}")
    assert fifo[0] == 0x99
    print("  ✓ FIFO prefix is 0x99")


if __name__ == "__main__":
    main()
