# Project Progress Report #3 Draft

## Embedded Bring-Up Progress

This iteration focuses on turning the STM32N6570-DK hardware peripherals into a runnable edge pipeline. The previous codebase had CubeMX-generated initialization for DCMIPP/CSI, ETH1, LTDC, RNG, PKA, HASH, SDMMC and IWDG, but the application layer did not yet start camera capture, feed frames into FreeRTOS tasks, or connect Ethernet to LwIP. Progress #3 adds that missing embedded backbone.

## Implemented Code Changes

- Added a FreeRTOS ALPR task layer with CameraCapture, Detection, OCR, CryptoTransmit, EthInput, EthLink, Display and Watchdog tasks.
- Added fixed-depth CMSIS-RTOS2 queues for Camera to Detection, Detection to OCR, and OCR to CryptoTransmit communication.
- Added an overlay mutex and frame-ready semaphore so the DCMIPP ISR can signal the camera task without sharing unprotected global state.
- Added `bsp_camera` for CSI/DCMIPP camera reset, frame buffers, pipe start/stop/suspend/resume and HAL frame callback forwarding.
- Added `bsp_eth` for LAN8742 PHY reset, MDIO register access, auto-negotiation, link state detection and loopback test support.
- Added `ethernetif` to bridge STM32N6 HAL ETH with LwIP, including RX interrupt signalling, DMA RX buffer allocation/link callbacks and pbuf handoff.
- Added `lwip_init` for TCP/IP stack startup, netif registration, DHCP/static-IP configuration and periodic link/DHCP management.
- Added `bsp_lcd` for LTDC framebuffer setup, RGB565 drawing helpers, backlight/reset GPIO handling and double-buffer support.
- Wired `MX_FREERTOS_Init()` through a bootstrap default task so LwIP initialization runs after `osKernelStart()`.
- Enabled DCMIPP and ETH1 IRQ routing to `HAL_DCMIPP_IRQHandler()` and `HAL_ETH_IRQHandler()`.

## Hardware Path Brought Up

Camera path:
`MB1854B Camera -> CSI -> DCMIPP Pipe0/Pipe1 -> DMA framebuffer -> CameraCaptureTask -> FrameQueue`

Ethernet path:
`ETH1 MAC -> LAN8742 PHY -> HAL ETH DMA -> ethernetif -> LwIP tcpip_thread -> DHCP/netif`

Runtime control path:
`IWDG -> WatchdogTask -> HAL_IWDG_Refresh()`

## Important Fixes

The STM32N6570-DK camera is connected through CSI, so the camera BSP now starts capture with `HAL_DCMIPP_CSI_PIPE_Start()` instead of the parallel `HAL_DCMIPP_PIPE_Start()` API.

The STM32N6 HAL ETH driver requires RX allocation and link callbacks. `HAL_ETH_RxAllocateCallback()` now provides aligned DMA buffers, and `HAL_ETH_RxLinkCallback()` links received frames so `HAL_ETH_ReadData()` can pass them to LwIP.

## Verification

Performed syntax-level cross-compilation checks with `arm-none-eabi-gcc -fsyntax-only` for the new application files in both `Appli/Core` and `ELE529_project/Appli/Core`. The files parse successfully; the only warning is the expected TrustZone `cmse_nonsecure_call` warning when compiling outside the full CubeIDE `-mcmse` build configuration.

## Remaining Work

- Enable full CubeIDE build with the LwIP source files included in the managed project.
- Test DHCP by pinging the board from the host PC.
- Verify live camera frame-ready interrupts on the target board.
- Replace placeholder Detection/OCR sections with STM32Cube.AI generated YOLO inference and host-side OCR transfer.
- Complete AES/HMAC payload encryption and host receiver integration.
