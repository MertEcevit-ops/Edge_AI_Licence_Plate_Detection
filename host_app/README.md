# Secure ALPR Host Receiver

Run the host receiver before starting the STM32 demo:

```bash
python3 host_app/secure_alpr_receiver.py --host 0.0.0.0 --port 9000
```

The receiver performs TCP receive, HMAC-SHA256 authentication over the
AES-256-CBC ciphertext, decrypt, optional CNN OCR with an ONNX TinyOCR model,
and JSONL logging to `host_app/alpr_events.jsonl`.

Firmware payloads can include `plate_image_b64`, a base64-encoded 96x32 PGM
crop of the detected plate. When `--ocr-model` is supplied, the receiver decodes
that image and runs the host-side OCR path.
