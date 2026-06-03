#!/usr/bin/env python3
"""Secure ALPR host receiver.

Receives STM32 ALPR packets over TCP, verifies the HMAC-SHA256 tag over the
AES-256-CBC ciphertext, decrypts the payload, optionally runs host-side CNN OCR
on an image payload, and writes JSONL logs.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import hmac
import json
import socket
import struct
from pathlib import Path
from typing import Any


MAGIC = 0x52504C41
VERSION = 1
KEY = b"ALPR-ELE529-SECURE-AES256-KEY!01"
HEADER = struct.Struct("<IHHIIIHH16s32s")
COMPACT_MAGIC = 0x414C5052
COMPACT_HEADER = struct.Struct("<IBBHIII16s")
AES_BLOCK_SIZE = 16
AUTH_TAG_SIZE = 32
MAX_PLAINTEXT_SIZE = 5120
MAX_CIPHERTEXT_SIZE = MAX_PLAINTEXT_SIZE + AES_BLOCK_SIZE

S_BOX = (
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16,
)
INV_S_BOX = tuple(S_BOX.index(i) for i in range(256))
RCON = (0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36, 0x6C, 0xD8, 0xAB, 0x4D)


def recv_exact(conn: socket.socket, length: int) -> bytes:
    chunks: list[bytes] = []
    remaining = length
    while remaining:
        chunk = conn.recv(remaining)
        if not chunk:
            raise ConnectionError("connection closed before packet was complete")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def xtime(value: int) -> int:
    return ((value << 1) ^ (0x1B if value & 0x80 else 0x00)) & 0xFF


def gf_mul(a: int, b: int) -> int:
    result = 0
    for _ in range(8):
        if b & 1:
            result ^= a
        a = xtime(a)
        b >>= 1
    return result


def key_expansion(key: bytes) -> list[int]:
    if len(key) != 32:
        raise ValueError("AES-256 key must be 32 bytes")

    round_key = list(key) + [0] * (240 - 32)
    i = 8
    while i < 60:
        temp = round_key[(i - 1) * 4 : i * 4]
        if i % 8 == 0:
            temp = [S_BOX[temp[1]] ^ RCON[i // 8], S_BOX[temp[2]], S_BOX[temp[3]], S_BOX[temp[0]]]
        elif i % 8 == 4:
            temp = [S_BOX[b] for b in temp]
        for j in range(4):
            round_key[i * 4 + j] = round_key[(i - 8) * 4 + j] ^ temp[j]
        i += 1
    return round_key


def add_round_key(state: list[int], round_key: list[int], round_no: int) -> None:
    start = round_no * AES_BLOCK_SIZE
    for i in range(AES_BLOCK_SIZE):
        state[i] ^= round_key[start + i]


def inv_sub_bytes(state: list[int]) -> None:
    for i, value in enumerate(state):
        state[i] = INV_S_BOX[value]


def inv_shift_rows(state: list[int]) -> None:
    state[1], state[5], state[9], state[13] = state[13], state[1], state[5], state[9]
    state[2], state[6], state[10], state[14] = state[10], state[14], state[2], state[6]
    state[3], state[7], state[11], state[15] = state[7], state[11], state[15], state[3]


def inv_mix_columns(state: list[int]) -> None:
    for col in range(4):
        base = col * 4
        a0, a1, a2, a3 = state[base : base + 4]
        state[base + 0] = gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9)
        state[base + 1] = gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13)
        state[base + 2] = gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11)
        state[base + 3] = gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14)


def aes256_decrypt_block(block: bytes, round_key: list[int]) -> bytes:
    if len(block) != AES_BLOCK_SIZE:
        raise ValueError("AES block must be 16 bytes")

    state = list(block)
    add_round_key(state, round_key, 14)

    for round_no in range(13, 0, -1):
        inv_shift_rows(state)
        inv_sub_bytes(state)
        add_round_key(state, round_key, round_no)
        inv_mix_columns(state)

    inv_shift_rows(state)
    inv_sub_bytes(state)
    add_round_key(state, round_key, 0)
    return bytes(state)


def aes256_cbc_decrypt(ciphertext: bytes, key: bytes, iv: bytes) -> bytes:
    if len(iv) != AES_BLOCK_SIZE or len(ciphertext) % AES_BLOCK_SIZE:
        raise ValueError("invalid AES-CBC input length")

    round_key = key_expansion(key)
    previous = iv
    plaintext = bytearray()

    for offset in range(0, len(ciphertext), AES_BLOCK_SIZE):
        block = ciphertext[offset : offset + AES_BLOCK_SIZE]
        plain_block = aes256_decrypt_block(block, round_key)
        plaintext.extend(a ^ b for a, b in zip(plain_block, previous))
        previous = block

    if not plaintext:
        raise ValueError("empty plaintext")
    pad_len = plaintext[-1]
    if pad_len < 1 or pad_len > AES_BLOCK_SIZE:
        raise ValueError("invalid PKCS#7 padding")
    if plaintext[-pad_len:] != bytes([pad_len]) * pad_len:
        raise ValueError("invalid PKCS#7 padding bytes")
    return bytes(plaintext[:-pad_len])


class CNNOCREngine:
    def __init__(self, model_path: str | None):
        self.model_path = Path(model_path) if model_path else None
        self.status = "disabled"
        self.session = None
        self.cv2 = None
        self.np = None
        self.charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabdefghnqrt"

        if not self.model_path:
            return
        try:
            import cv2  # type: ignore
            import numpy as np  # type: ignore
            import onnxruntime as ort  # type: ignore

            self.cv2 = cv2
            self.np = np
            self.session = ort.InferenceSession(str(self.model_path))
            self.status = "ready"
        except Exception as exc:  # pragma: no cover - optional host dependency path
            self.status = f"unavailable: {exc}"

    def recognize_b64(self, image_b64: str | None) -> tuple[str | None, str]:
        if not image_b64:
            return None, "no_image_payload"
        if self.session is None or self.cv2 is None or self.np is None:
            return None, self.status

        image_bytes = base64.b64decode(image_b64)
        np_buf = self.np.frombuffer(image_bytes, dtype=self.np.uint8)
        image = self.cv2.imdecode(np_buf, self.cv2.IMREAD_COLOR)
        if image is None:
            return None, "image_decode_failed"

        chars = self._segment_chars(image)
        if not chars:
            return "", "no_characters_found"

        input_name = self.session.get_inputs()[0].name
        text = ""
        for char_image in chars:
            tensor = char_image.astype("float32") / 255.0
            tensor = tensor.reshape(1, 1, 28, 28)
            output = self.session.run(None, {input_name: tensor})[0]
            index = int(output.argmax(axis=1)[0])
            text += self.charset[index].upper()
        return text, "ok"

    def _segment_chars(self, image: Any) -> list[Any]:
        cv2 = self.cv2
        np = self.np
        assert cv2 is not None and np is not None

        image = cv2.resize(image, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        blur = cv2.GaussianBlur(gray, (5, 5), 0)
        _, thresh = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        boxes = []
        for contour in contours:
            x, y, w, h = cv2.boundingRect(contour)
            aspect_ratio = w / float(h)
            if 0.15 < aspect_ratio < 0.9 and h > 20:
                boxes.append((x, y, w, h))
        boxes.sort(key=lambda item: item[0])

        chars = []
        for x, y, w, h in boxes:
            crop = thresh[y : y + h, x : x + w]
            if crop.size == 0:
                continue
            height, width = crop.shape
            scale = 20.0 / max(height, width)
            new_w = max(1, int(width * scale))
            new_h = max(1, int(height * scale))
            scaled = cv2.resize(crop, (new_w, new_h), interpolation=cv2.INTER_AREA)
            canvas = np.zeros((28, 28), dtype=np.uint8)
            start_x = (28 - new_w) // 2
            start_y = (28 - new_h) // 2
            canvas[start_y : start_y + new_h, start_x : start_x + new_w] = scaled
            chars.append(canvas)
        return chars


def decode_authenticated_payload(
    *,
    frame_id: int,
    timestamp_ms: int,
    plaintext_len: int,
    ciphertext: bytes,
    iv: bytes,
    auth_tag: bytes,
    packet_format: str,
    allow_legacy_sha256: bool,
) -> dict[str, Any]:
    if plaintext_len > MAX_PLAINTEXT_SIZE:
        raise ValueError(f"plaintext too large: {plaintext_len}")
    if len(ciphertext) > MAX_CIPHERTEXT_SIZE:
        raise ValueError(f"ciphertext too large: {len(ciphertext)}")
    if len(auth_tag) != AUTH_TAG_SIZE:
        raise ValueError(f"invalid auth tag length: {len(auth_tag)}")

    expected_tag = hmac.new(KEY, ciphertext, hashlib.sha256).digest()
    auth_algorithm = "hmac-sha256-ciphertext"
    auth_ok = hmac.compare_digest(expected_tag, auth_tag)

    if not auth_ok and not allow_legacy_sha256:
        raise ValueError("hmac-sha256 authentication failed")

    plaintext = aes256_cbc_decrypt(ciphertext, KEY, iv)
    if len(plaintext) != plaintext_len:
        raise ValueError(f"plaintext length mismatch: {len(plaintext)} != {plaintext_len}")

    if not auth_ok:
        auth_algorithm = "legacy-sha256-plaintext"
        auth_ok = hmac.compare_digest(hashlib.sha256(plaintext).digest(), auth_tag)
        if not auth_ok:
            raise ValueError("hmac-sha256 authentication failed; legacy sha256 check failed")

    try:
        payload: Any = json.loads(plaintext.decode("utf-8"))
    except json.JSONDecodeError:
        payload = {"raw": plaintext.decode("utf-8", errors="replace")}

    return {
        "frame_id": frame_id,
        "timestamp_ms": timestamp_ms,
        "integrity_ok": auth_ok,
        "auth_algorithm": auth_algorithm,
        "packet_format": packet_format,
        "payload": payload,
    }


def read_packet(conn: socket.socket, *, allow_legacy_sha256: bool = False) -> dict[str, Any]:
    prefix = recv_exact(conn, 8)
    magic = struct.unpack_from("<I", prefix)[0]
    canonical_version, canonical_header_size = struct.unpack_from("<HH", prefix, 4)

    if magic == MAGIC and canonical_version == VERSION:
        if canonical_header_size < HEADER.size:
            raise ValueError(f"invalid header size: {canonical_header_size}")

        header = prefix + recv_exact(conn, canonical_header_size - len(prefix))
        (
            _magic,
            _version,
            _header_size,
            frame_id,
            timestamp_ms,
            plaintext_len,
            ciphertext_len,
            _reserved,
            iv,
            auth_tag,
        ) = HEADER.unpack(header[: HEADER.size])

        if ciphertext_len > MAX_CIPHERTEXT_SIZE:
            raise ValueError(f"ciphertext too large: {ciphertext_len}")

        ciphertext = recv_exact(conn, ciphertext_len)
        return decode_authenticated_payload(
            frame_id=frame_id,
            timestamp_ms=timestamp_ms,
            plaintext_len=plaintext_len,
            ciphertext=ciphertext,
            iv=iv,
            auth_tag=auth_tag,
            packet_format="canonical-tag-in-header",
            allow_legacy_sha256=allow_legacy_sha256,
        )

    compact_version = prefix[4]
    compact_header_size = prefix[5]
    if magic in (MAGIC, COMPACT_MAGIC) and compact_version == VERSION:
        actual_header_size = max(compact_header_size, COMPACT_HEADER.size)
        header = prefix + recv_exact(conn, actual_header_size - len(prefix))
        (
            _magic,
            _version,
            _header_size,
            ciphertext_len,
            frame_id,
            timestamp_ms,
            plaintext_len,
            iv,
        ) = COMPACT_HEADER.unpack(header[: COMPACT_HEADER.size])

        if ciphertext_len > MAX_CIPHERTEXT_SIZE:
            raise ValueError(f"ciphertext too large: {ciphertext_len}")

        ciphertext = recv_exact(conn, ciphertext_len)
        auth_tag = recv_exact(conn, AUTH_TAG_SIZE)
        return decode_authenticated_payload(
            frame_id=frame_id,
            timestamp_ms=timestamp_ms,
            plaintext_len=plaintext_len,
            ciphertext=ciphertext,
            iv=iv,
            auth_tag=auth_tag,
            packet_format="compact-tag-after-ciphertext",
            allow_legacy_sha256=allow_legacy_sha256,
        )

    raise ValueError(f"bad magic/version: magic=0x{magic:08x}")


def append_log(log_path: Path, event: dict[str, Any]) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(event, ensure_ascii=False, sort_keys=True) + "\n")


def serve(args: argparse.Namespace) -> None:
    ocr = CNNOCREngine(args.ocr_model)
    log_path = Path(args.log)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(args.backlog)
        print(f"secure ALPR receiver listening on {args.host}:{args.port}")
        print(f"CNN OCR status: {ocr.status}")

        while True:
            conn, peer = server.accept()
            with conn:
                received_at = dt.datetime.now(dt.timezone.utc).isoformat()
                try:
                    packet = read_packet(conn, allow_legacy_sha256=args.allow_legacy_sha256)
                    payload = packet["payload"]
                    image_b64 = payload.get("plate_image_b64") if isinstance(payload, dict) else None
                    cnn_text, cnn_status = ocr.recognize_b64(image_b64)
                    event = {
                        "received_at": received_at,
                        "peer": f"{peer[0]}:{peer[1]}",
                        **packet,
                        "cnn_ocr_text": cnn_text,
                        "cnn_ocr_status": cnn_status,
                    }
                    append_log(log_path, event)
                    plate = payload.get("plate") if isinstance(payload, dict) else None
                    print(
                        f"frame={packet['frame_id']} integrity={packet['integrity_ok']} "
                        f"edge_plate={plate} cnn_ocr={cnn_text} status={cnn_status}"
                    )
                except Exception as exc:
                    event = {
                        "received_at": received_at,
                        "peer": f"{peer[0]}:{peer[1]}",
                        "error": str(exc),
                    }
                    append_log(log_path, event)
                    print(f"packet error from {peer[0]}:{peer[1]}: {exc}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Secure ALPR TCP receiver")
    parser.add_argument("--host", default="0.0.0.0", help="bind address")
    parser.add_argument("--port", type=int, default=9000, help="TCP port")
    parser.add_argument("--log", default="host_app/alpr_events.jsonl", help="JSONL log path")
    parser.add_argument("--ocr-model", default=None, help="optional TinyOCR ONNX model path")
    parser.add_argument("--backlog", type=int, default=4, help="listen backlog")
    parser.add_argument(
        "--allow-legacy-sha256",
        action="store_true",
        help="accept pre-HMAC packets that store SHA-256(plaintext) in the tag field",
    )
    return parser.parse_args()


if __name__ == "__main__":
    serve(parse_args())
