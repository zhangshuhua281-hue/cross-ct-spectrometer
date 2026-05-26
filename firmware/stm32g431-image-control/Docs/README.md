# image-control

STM32G431RBTx HAL base project for Keil5.

## Directory layout

- `App`: application logic, processing pipeline, group statistics
- `BSP`: board-level drivers and pin abstraction
- `Core`: startup, interrupt handlers, clock config, HAL MSP
- `Drivers`: local CMSIS and HAL drivers copied from STM32CubeG4
- `MDK-ARM`: Keil5 project files
- `Docs`: project notes

## Current baseline

- Device target: `STM32G431RBTx`
- Framework: `STM32CubeG4 HAL`
- Clock: HSI -> PLL -> 170 MHz
- Input UART: `USART2`, `PA2(TX)` / `PA3(RX)` linked to the STM32F103 TCD1304 capture board
- Display link UART: `USART3`, `PB10(TX)` / `PB11(RX)` to ESP32-S3 upper display
- `PA2` is reserved for `USART2_TX` in the default acquisition chain and must not be reused as a debug GPIO
- Handshake GPIO:
  - `PA5`: `DATA_RDY` output to ESP32 `GPIO18`
  - `PA0`: `WAKE` input from ESP32 `GPIO21`
- Processing scope for current firmware:
  - dark level estimation from the first 16 ADC points using median
  - skip the first 20 points
  - `Intensity = dark_adc - ADC`
  - 9-point median filter
  - 151-point 3rd-order Savitzky-Golay smoothing
  - clamp smoothed output to `>= 0`
  - max normalization on the smoothed curve
  - lightweight per-group accumulation and QC summary
- Not included yet:
  - white-reference normalization
  - area normalization
  - wavelength calibration
  - PC-side plots and reports
  - full-resolution multi-replicate history retention

## Embedded mapping to the PC workflow

- The G431 firmware keeps the main per-frame processing chain from `scripts/process_v3_tcd1304.py`.
- To stay within MCU RAM limits, it does not keep every full-resolution curve for every group.
- The firmware processes one frame at a time, then keeps only:
  - frame summary values
  - a downsampled normalized curve for current group QC accumulation
  - lightweight group-level statistics

## Serial protocol assumptions

- Input source on `USART2` is the recovered STM32F103 capture firmware.
- G431 requests one frame by sending command `0xA1`.
- F103 replies with one full 12-bit frame:
  - `3648` pixels
  - `2` bytes per pixel
  - little-endian (`low byte` then `high byte`)
  - total payload `7296` bytes
- Pixel axis used by the G431 pipeline:
  - first pixel index is treated as `0`
  - first `20` pixels are skipped by the processing chain
- Current group mapping in this firmware:
  - every pulled F103 frame is tagged as `group 1`
  - `replicate_id` is auto-incremented locally on G431 for QC accumulation
- ESP32 output content on `USART3`:
  - `PKT BEGIN SEQ=<n>`
  - `FRAME ...`
  - `GROUP ...`
  - `CURVE_BEGIN ...`
  - repeated `CURVE <pixel> <q15>`
  - `CURVE_END`
  - `PKT END`
- Handshake behavior:
  - every completed frame is converted into one display packet
  - `DATA_RDY` stays high while a packet is pending
  - if `WAKE` is low, the latest packet is held until ESP32 is ready
  - if `WAKE` is high, the packet is sent immediately
  - only one packet buffer is kept on the G431 side, so acquisition pauses until the pending packet is drained

## Next recommended changes

- Add DMA or interrupt-based UART receive on `USART2` if the F103 frame rate becomes too high for polling.
- Replace the temporary fixed `group 1` tag with a real group/replicate source if the upstream board can provide metadata.
- Keep wavelength calibration as a later stage after your new calibration data is ready.
