# Project Progress Report #3 Draft

## Embedded Bring-Up Progress

This iteration focuses on turning the STM32N6570-DK hardware peripherals into a runnable edge pipeline and cleaning the project structure according to the driver/board-abstraction feedback. The previous codebase kept low-level HAL handles and `MX_*_Init()` functions in `main.c`; Progress #3 moves those responsibilities into BSP driver `Open` functions and keeps `main.c` as a thin board/application bootstrap.

## Implemented Code Changes

- Added a FreeRTOS ALPR task layer with CameraCapture, Detection, OCR, CryptoTransmit, EthInput, EthLink, Display and Watchdog tasks.
- Added fixed-depth CMSIS-RTOS2 queues for Camera to Detection, Detection to OCR, and OCR to CryptoTransmit communication.
- Added an overlay mutex and frame-ready semaphore so the DCMIPP ISR can signal the camera task without sharing unprotected global state.
- Added `bsp_camera` for CSI/DCMIPP camera reset, frame buffers, pipe start/stop/suspend/resume and HAL frame callback forwarding.
- Added `bsp_eth` for LAN8742 PHY reset, MDIO register access, auto-negotiation, link state detection and loopback test support.
- Added `ethernetif` to bridge STM32N6 HAL ETH with LwIP, including RX interrupt signalling, DMA RX buffer allocation/link callbacks and pbuf handoff.
- Added `lwip_init` for TCP/IP stack startup, netif registration, DHCP/static-IP configuration and periodic link/DHCP management.
- Added `bsp_lcd` for LTDC framebuffer setup, RGB565 drawing helpers, backlight/reset GPIO handling and double-buffer support.
- Added `bsp_board`, `bsp_storage`, `bsp_security`, `bsp_uart` and `bsp_watchdog` so low-level HAL handles live inside their device driver layer instead of `main.c`.
- Replaced the empty default FreeRTOS task with a real `AppStartup` task that starts the ALPR task graph and exits.
- Added `security_layer` with SHA-256 integrity and AES-256-CBC payload encryption before network transmission.
- Added the host TCP receiver in `host_app/secure_alpr_receiver.py` for receive, decrypt, SHA-256 verification, optional CNN OCR and JSONL logging.
- Moved RGB565 color constants from `app_tasks.c` into the LCD BSP header.
- Enabled DCMIPP and ETH1 IRQ routing to `HAL_DCMIPP_IRQHandler()` and `HAL_ETH_IRQHandler()`.

## Hardware Path Brought Up

Camera path:
`MB1854B Camera -> CSI -> DCMIPP Pipe0/Pipe1 -> DMA framebuffer -> CameraCaptureTask -> FrameQueue`

Ethernet path:
`ETH1 MAC -> LAN8742 PHY -> HAL ETH DMA -> ethernetif -> LwIP tcpip_thread -> DHCP/netif`

Runtime control path:
`IWDG -> bsp_watchdog -> WatchdogTask -> HAL_IWDG_Refresh()`

Secure transport path:
`OCR result -> security_layer SHA-256 -> AES-256-CBC packet -> LwIP TCP -> host_app receiver -> decrypt/verify/log`

## Important Fixes

The STM32N6570-DK camera is connected through CSI, so the camera BSP now starts capture with `HAL_DCMIPP_CSI_PIPE_Start()` instead of the parallel `HAL_DCMIPP_PIPE_Start()` API.

The STM32N6 HAL ETH driver requires RX allocation and link callbacks. `HAL_ETH_RxAllocateCallback()` now provides aligned DMA buffers, and `HAL_ETH_RxLinkCallback()` links received frames so `HAL_ETH_ReadData()` can pass them to LwIP.

`main.c` no longer owns `ETH_HandleTypeDef`, `DCMIPP_HandleTypeDef`, `LTDC_HandleTypeDef`, `HASH_HandleTypeDef`, `RNG_HandleTypeDef`, `PKA_HandleTypeDef`, `SD_HandleTypeDef`, `XSPI_HandleTypeDef`, `CACHEAXI_HandleTypeDef`, `IWDG_HandleTypeDef` or the related low-level init functions. Those are now opened from BSP/device-driver modules.

## Verification

Built the active `Appli/Debug` project with the STM32CubeIDE GNU Arm toolchain. The ELF, BIN and listing outputs are generated successfully. Remaining warnings are from generated/third-party code and bare-metal `nosys` stubs.

Host-side checks:
- `python3 -m py_compile host_app/secure_alpr_receiver.py`
- AES-256 decrypt validation against a known NIST test vector

## Remaining Work

- Test DHCP by pinging the board from the host PC.
- Verify live camera frame-ready interrupts on the target board.
- Replace placeholder Detection/OCR sections with final STM32Cube.AI post-processing or route cropped-plate image payloads to the host CNN OCR path.
- Record final end-to-end demo video and include host log output in the final report.
