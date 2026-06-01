# Edge AI Licence Plate Detection

STM32N6570-DK edge AI licence plate detection pipeline with FreeRTOS, DCMIPP camera capture, STM32Cube.AI/ATON inference, encrypted LwIP transport, and a Python host receiver for decrypt/verify/log plus optional CNN OCR.

## Implemented Pipeline

1. `main.c` is a thin bootstrap: HAL, board open, X-CUBE-AI init, external memory init, and FreeRTOS start.
2. BSP drivers own low-level HAL handles and peripheral open/close logic for camera, Ethernet, LCD, security, storage, UART, and watchdog.
3. `CameraCaptureTask` receives DCMIPP frame-ready events and forwards 320x320 RGB888 AI frames.
4. `DetectionTask` runs the generated `alpr2` STM32Cube.AI/ATON YOLO model, parses the `[1,5,2100]` output, applies confidence filtering and NMS, and publishes plate ROIs.
5. `OCRTask` prepares host OCR handoff metadata. OCR remains host-side by design.
6. `CryptoTransmitTask` crops each ROI to a 96x32 PGM image, base64-encodes it into JSON, seals it with AES-256-CBC plus SHA-256 integrity metadata, and sends it over TCP.
7. `host_app/secure_alpr_receiver.py` receives packets, decrypts, verifies SHA-256, optionally runs TinyOCR ONNX on `plate_image_b64`, and appends JSONL logs.

## Host Receiver

```bash
python3 host_app/secure_alpr_receiver.py --host 0.0.0.0 --port 9000
```

Optional OCR model:

```bash
python3 host_app/secure_alpr_receiver.py \
  --host 0.0.0.0 \
  --port 9000 \
  --ocr-model path/to/tiny_ocr_model.onnx
```

Default firmware target host is `192.168.1.10:9000`. Override at compile time with `ALPR_HOST_IP_ADDR0..3` and `ALPR_HOST_TCP_PORT`.

## Security Packet

The transport packet uses:

- AES-256-CBC payload encryption with random IV from STM32 RNG.
- SHA-256 digest from STM32 HASH for plaintext integrity verification.
- Versioned binary header with frame ID, timestamp, plaintext length, IV, digest, and ciphertext length.

The current key is a demo pre-shared key shared by firmware and host receiver. Replace it before any real deployment.

## Build

The active STM32CubeIDE/GNU Arm build directory is `Appli/Debug`:

```bash
make -C Appli/Debug
```

Generated model and runtime files live under `Appli/X-CUBE-AI/App`; the active network alias points at `alpr2`, the int8 deployment model.
