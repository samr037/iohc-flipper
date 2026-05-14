#!/usr/bin/env python3
"""Decrypt an io-homecontrol install key from a captured cmdid 0x30 frame.

Algorithm sources:
- enc_key field layout: rspaargaren/include/iohcPacket.h:171-176
- transfer_key constant: rspaargaren/src/iohcCryptoHelpers.cpp:47
- IV construction + AES-CFB128: rspaargaren/src/iohcCryptoHelpers.cpp:174-215
- HMAC verification path: rspaargaren/src/iohcCryptoHelpers.cpp:92-167

Usage:
    iohc_key_decrypt.py <hex_frame> [--verify-with hmac_frame_hex]

Where <hex_frame> is the full on-air bytes of a 0x30 frame as captured
(beginning with the CtrlB1 byte; 31 bytes long, CRC-OK).
"""
import sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TRANSFER_KEY = bytes.fromhex("34c3466ed88f4e8e16aa473949884373")


def derive_install_key(zero30_frame: bytes) -> tuple[bytes, bytes]:
    """Return (source_addr, install_key) for a captured 0x30 frame."""
    if len(zero30_frame) < 29 or zero30_frame[8] != 0x30:
        raise ValueError("not a 0x30 pairing frame")
    src = zero30_frame[5:8]
    enc_key = zero30_frame[9:25]
    iv = bytearray(16)
    for i in range(0, 13, 3):
        iv[i:i+3] = src
    iv[15] = src[0]
    cipher = Cipher(algorithms.AES(TRANSFER_KEY), modes.CFB(bytes(iv)))
    install_key = cipher.decryptor().update(enc_key)
    return src, install_key


def _compute_checksum(frame_byte: int, c1: int, c2: int) -> tuple[int, int]:
    tmp = frame_byte ^ c2
    new_c2 = ((c1 & 0x7F) << 1) & 0xFF
    if tmp >= 0x80:
        new_c2 |= 1
    if (c1 & 0x80) == 0:
        return new_c2, (tmp << 1) & 0xFF
    return new_c2 ^ 0x55, ((tmp << 1) ^ 0x5B) & 0xFF


def hmac_1w(install_key: bytes, frame_data: bytes, sequence: bytes) -> bytes:
    """Compute the 6-byte 1W HMAC. Use for cmdid 0x2E / 0x39 / button frames."""
    iv = bytearray(16)
    for i, b in enumerate(frame_data):
        iv[8], iv[9] = _compute_checksum(b, iv[8], iv[9])
        if i < 8:
            iv[i] = b
    for j in range(len(frame_data), 8):
        iv[j] = 0x55
    iv[10] = sequence[0]
    iv[11] = sequence[1]
    for j in range(12, 16):
        iv[j] = 0x55
    block = Cipher(algorithms.AES(install_key), modes.ECB()).encryptor().update(bytes(iv))
    return block[:6]


def _verify_with_hmac_frame(install_key: bytes, hmac_frame: bytes) -> bool:
    """Verify the install key by recomputing the HMAC of a 0x2E/0x39-shaped frame.

    Frame layout (msglen 17, total 20 bytes):
      [0]CtrlB1 [1]CtrlB2 [2-4]dst [5-7]src [8]cmd [9]data [10-11]seq
      [12-17]hmac [18-19]crc
    """
    cmd = hmac_frame[8]
    data = hmac_frame[9]
    seq = hmac_frame[10:12]
    on_air_hmac = hmac_frame[12:18]
    computed = hmac_1w(install_key, bytes([cmd, data]), seq)
    return computed == on_air_hmac


def main(argv: list[str]) -> int:
    if len(argv) < 1:
        print(__doc__, file=sys.stderr)
        return 1
    frame = bytes.fromhex(argv[0])
    src, key = derive_install_key(frame)
    print(f"source address: {src.hex().upper()}")
    print(f"install key   : {key.hex().upper()}")
    if "--verify-with" in argv:
        i = argv.index("--verify-with")
        vframe = bytes.fromhex(argv[i + 1])
        ok = _verify_with_hmac_frame(key, vframe)
        print(f"verification  : {'OK' if ok else 'FAILED'}")
        return 0 if ok else 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
