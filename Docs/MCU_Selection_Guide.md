# STM32 Nucleo Board Selection Guide
**For LED_Strip_Controller_G474 Project**

This guide summarizes MCU/Nucleo options based on current project needs and the 128 KB RAM constraint on the G474RE. It is designed to be printed on a single sheet for quick reference when checking your board collection or placing Mouser/DigiKey orders.

## Project Lineage & Current State
- **G0B0** → **L476** → **G474RE** (current baseline)
- Key requirements:
  - 5+ UART/USART channels (USART line-encoding trick for WS2812/SK6812 LED strips)
  - Strong audio (SAI/I2S output; future input)
  - DSP features (CORDIC + FMAC heavily used in current synth engine)
  - USB device support (CDC/HID/MIDI composite in wishlist)
  - Headroom for: large DMA buffers (LED + audio), RTOS, pattern storage, polyphonic synthesis, future gesture/TFT/mic
- Current board: **Nucleo-G474RE (STM32G474RE)**
  - Cortex-M4 @ 170 MHz
  - 128 KB SRAM / 512 KB Flash
  - Excellent analog + hardware CORDIC/FMAC (ideal for efficient sine synthesis)
  - Good UART count + SAI + USB FS
  - **Main weakness**: Only 128 KB RAM

## RAM Concern
128 KB is comfortable today (~15 KB BSS in recent builds), but will tighten with:
- Multiple ping-pong DMA buffers (I2S audio + future input)
- RTOS task stacks + objects
- USB stack (especially composite CDC + MIDI)
- Pattern/animation buffers or live frame data
- DSP state (FFT, filters, multi-voice synth)
- Future TFT framebuffers or sensor history

**Trigger for upgrade**: When adding RTOS + any two of the above features.

## Comparison of Strong Nucleo Options

| Board                  | MCU              | CPU                  | RAM          | Flash   | Key Strengths for This Project                          | Key Trade-offs                                      | Notes / Availability |
|------------------------|------------------|----------------------|--------------|---------|---------------------------------------------------------|-----------------------------------------------------|----------------------|
| **Nucleo-G474RE** (current) | STM32G474RE     | M4 @ 170 MHz        | 128 KB      | 512 KB | CORDIC + FMAC (current synth engine), excellent analog, many UARTs, SAI, USB FS | Low RAM headroom for RTOS + buffers                 | Best balance today. Widely stocked at Mouser. |
| **Nucleo-H723ZG**     | STM32H723ZG     | M7 @ 550 MHz        | 564 KB      | 1 MB   | Massive RAM, very fast M7, multiple SAI, USB FS+HS, plenty of UARTs (4 USART + 4 UART) | No hardware CORDIC/FMAC; higher power/complexity    | Good upgrade path. Mouser/DigiKey stock. |
| **Nucleo-H743ZI2**    | STM32H743ZI     | M7 @ 480 MHz        | 1 MB        | 2 MB   | Even more RAM, excellent peripherals (SAI, USB HS+FS, Ethernet), very mature ecosystem | No CORDIC/FMAC; biggest migration effort            | **Strongly recommended** if moving to H7. Very popular for high-end hobby projects. |
| **Nucleo-F767ZI**     | STM32F767ZI     | M7 @ 216 MHz        | 512 KB      | 2 MB   | 4× RAM vs G474, solid UARTs + SAI + USB, cheaper than H7 | Older family, less future-proof than H7             | Good "middle step" if you want more RAM without jumping to 550 MHz. |

### Other Notes on Alternatives
- Lower families (U5, L5, G0 variants) generally do not provide enough RAM or performance headroom.
- H5 series (newer) can be interesting but H7 is more proven for audio/DSP/RTOS workloads today.
- All listed Nucleo-64/144 boards are hobbyist-friendly with integrated ST-Link and standard connectors.

## Migration Considerations (G474 → H7)
- **What ports easily**: LED USART+DMA trick, I2S/SAI audio core, debug menu, logging, jobs system, note_player logic.
- **What changes**:
  - CORDIC/FMAC → replace with software CORDIC, lookup table + interpolation, or fast `arm_sin_f32` / CMSIS-DSP on M7 (very practical due to speed).
  - Clock tree and peripheral init (use CubeMX to regenerate).
  - USB (big win: HS support makes composite CDC + MIDI easier and faster).
  - Pin remapping (Nucleo-144 vs 64 form factor).
- **Effort**: Non-trivial (you've done 3 migrations already). Use a dedicated branch.
- **Benefits on H7**: Comfortable RTOS + multiple concurrent tasks, high-bandwidth USB pattern uploads, headroom for polyphonic synth / audio-reactive processing, future-proofing.
- **When to do it**: Only when RAM pressure appears or you want the performance jump for musical features.

## Current Recommendation (as of this writing)
**Stick with the Nucleo-G474RE for now.**

- You have already invested in three migrations.
- The G474 is an excellent fit for the current scope (LED strips via clever USART trick + CORDIC synth + audio output).
- RAM is not yet a blocker.
- The H723ZG (or better H743ZI2) is the clear next step **if/when** you hit the RAM wall or decide to go all-in on RTOS + USB composite + rich audio processing.

### Quick Decision Checklist (print & check off)
- [ ] Current RAM usage (check latest .map or add runtime stats)
- [ ] Planning to add RTOS soon?
- [ ] Need USB HS or composite device (CDC + MIDI)?
- [ ] Want to keep hardware CORDIC for tone generation?
- [ ] Ready for another migration effort?
- [ ] Boards already on hand? (check your collection against the table above)

## Ordering / Stock Notes (Mouser / DigiKey)
- Search terms: `NUCLEO-G474RE`, `NUCLEO-H723ZG`, `NUCLEO-H743ZI2`, `NUCLEO-F767ZI`
- All are regularly stocked by Mouser (you are a regular customer).
- Nucleo-144 boards (H7 options) give more expansion connectors (Zio + Morpho), which can be useful for future gesture/TFT/mic add-ons.

**This document is a living reference.** Update it when you evaluate new boards or measure actual RAM usage under load.

---

*Generated from project context (LED strip controller with audio, CORDIC synth, player-piano experiments, USB wishlist, and future RTOS/DSP/audio-in plans).*