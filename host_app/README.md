# Secure ALPR Host Receiver

Run the host receiver before starting the STM32 demo:

```bash
python3 host_app/secure_alpr_receiver.py --host 0.0.0.0 --port 9000
```

The receiver performs TCP receive, AES-256-CBC decrypt, SHA-256 integrity
verification, optional CNN OCR with an ONNX TinyOCR model, and JSONL logging to
`host_app/alpr_events.jsonl`.
