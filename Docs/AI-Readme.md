# LED_Strip_Controller_L476

*Guide for AI coding assistants (Cursor, Claude, Grok, etc.) and human collaborators.*

Prior G0B0 work lives in the separate repo [LED-Strip-Controller](https://github.com/n9xmj/LED-Strip-Controller). This tree is the **STM32G474** migration and forward development baseline.

---

## Project status (snapshot)

| Area | Status |
|------|--------|
| USART + DMA LED driver (`App/led_strip_control.*`) | Ported and running on L476 |
| Test hardware (4 strips) | Wired per layout below; debug menu patterns verified |
| GitHub | [n9xmj/LED_Strip_Controller_L476](https://github.com/n9xmj/LED_Strip_Controller_L476) |
| I2S audio out (`App/i2s_audio_out.*`, SAI2.B → MAX98357) | Core API + debug menu sine tests (`i` submenu) |
| RTOS / audio in / TFT / gesture sensor | Not started |

**UART strip map (CubeMX / `platform.h`):**

| Strip | Protocol | LEDs | MCU peripheral |
|-------|----------|------|----------------|
| [1] | WS2812B | 21 (1 + 8 + 12 ring) | USART1 |
| [2] | SK6812 RGBW | 10 (line) | USART3 |
| [3] | SK6812 RGBW | 10 (line) | UART4 |
| [4] | SK6812 RGBW | 10 (line) | LPUART1 |

Debug console: **USART2** (ST-Link VCP). LED test submenu: **`t`**. I2S audio test submenu: **`i`**.

---

## Project overview

Long-term goals for this hobby firmware project:

- **Core (largely done):** Drive **WS2812B** and **SK6812** RGB(W) LEDs using the STM32 USART line-encoding trick (TX invert, 7-bit, ~2.4 Mbaud, DMA).
- **Audio-reactive lighting:** Detect audio (I2S or analog mic) and synchronize LED effects.
- **Gesture / “theremin” interaction:** Hand gestures (e.g. VL53L5CX) to control lighting and possibly sound.
- **Status UI:** Small TFT over SPI (ST7789-class).
- **DSP:** CMSIS-DSP for audio processing (scope TBD).
- More ideas may be added over time.

**Author context:** Strong bare-metal STM32 experience (especially low/mid range). Less experience with RTOS, DSP, audio chains, and TFT displays. Moderate electrical-engineering background. Git/GitHub familiarity is basic—offer concise guidance when relevant.

---

## Development environment

- **MCU board:** STM32 Nucleo-64 **L476RG** + external test PCB (hardware evolves as needed).
- **STM32CubeMX** — pin/peripheral configuration.
- **STM32CubeIDE** — edit, build, flash, debug (GCC).
- **Tera Term** — serial console, debug menu interaction.
- Bench test equipment as needed.
- **Reference PDFs** in `Docs/` (WS2812, SK6812, ST7789, INMP441, Nucleo L476, etc.).

**Planned / available hardware:**

- WS2812B and SK6812 LED strips  
- INMP441 I2S microphone  
- Electret microphone + preamp (see `Analog Electret Microphone Preamp.md`)  
- MAX98357 I2S amplifier, analog audio amplifier  
- TFT displays (ST7789 or similar)  
- VL53L5CX time-of-flight (e.g. VL53L5CX-SATEL breakout)  
- IR remote receiver module (demodulated 38 kHz; NEC-style)—wishlist only

---

## TODO checklist

Items may be done **out of order**. Toggle boxes as work completes.

- [x] **1.** Get migrated project (G0B0 → L476) up and running as the work baseline.
- [x] **2.** Convert project notes to Markdown (`Docs/AI-Readme.md`) for clarity and AI agents.
- [ ] **3.** Add an RTOS (FreeRTOS + CMSIS-OS wrappers; ST middleware).
- [ ] **3a.** Configure project for efficient RTOS use (tasks, priorities, driver interaction).
- [ ] **4.** I2S microphone input (STM32 SAI or I2S + DMA).
- [ ] **4a.** Analog microphone via ADC + DMA.
- [x] **5.** I2S audio output for testing (e.g. SAI2 + DMA) — `i2s_audio_out` API; sine bench tests in debug menu **`i`**.
- [ ] **5a.** Audio output via on-chip DAC + DMA.
- [ ] **6.** Gesture sensor: VL53L5CX (VL53L5CX-SATEL board).
- [ ] **7.** Small TFT LCD on SPI (e.g. SPI2); consider open-source drivers (e.g. Bodmer / community ST7789 code).
- [ ] **8.** CMSIS-DSP audio processing (scope TBD).
- [x] **9.** Debug menu bench tests — *ongoing*; expand as each feature lands (LED submenu under **`t`** today).

---

## TODO wishlist (low priority / experiments)

Items here are **optional**—try when curiosity or bench time allows. Not ordered; no schedule implied.

- [ ] **IR remote control receiver** — integrate a demodulated IR module (e.g. VS1838B / TSOP38xxx class: VCC, GND, digital OUT). Decode NEC (or similar) in firmware; map keys to LED patterns, menu actions, or audio-test triggers. See [IR receiver — L476 IP options](#ir-receiver--l476-ip-options) below.

---

### IR receiver — L476 IP options

The STM32L476 has **no dedicated IR peripheral**. A 3-pin IR receiver module already demodulates the 38 kHz carrier; the MCU only decodes **logic-level pulse widths** (NEC / RC5 / Sony, etc.) in software.

| Approach | On-chip IP | Fit for this project |
|----------|------------|----------------------|
| **Timer input capture (preferred)** | **TIM1, TIM2, TIM3, TIM4, TIM5, TIM8, TIM15, or TIM16** — channel in *Input Capture* mode on the IR OUT pin | Hardware timestamps edges; ISR or DMA reads periods. Prescaler → **1 µs/tick** is enough for NEC (~9 ms / 4.5 ms frames). **TIM16** is already enabled in Cube but is only a timebase today—could be reconfigured to **TIM16 CH1** IC if a suitable pin is free. **TIM2–5** are full-featured and common choices if pins conflict with LEDs/audio. |
| **EXTI + µs timebase** | **EXTI** on GPIO (both edges) + **TIM6** or **TIM2** as free-running counter | Works well; slightly more CPU in the EXTI handler. **TIM7** is reserved for `v_delay_us()` (`DELAY_US_TIMER_HANDLE` in `platform.h`)—do not share it with IR. |
| **LPTIM1 / LPTIM2** | Pulse-counter / capture modes | Possible for simple pulse counting; awkward for full NEC frames (long gaps, start burst). Low priority vs GP timers. |
| **Not a good fit** | USART, SAI, SPI, ADC, DAC | Wrong layer—IR OUT is digital timing, not serial samples or analog. |

**Practical notes**

- Pick a **5 V-tolerant** GPIO if the module runs at 5 V (many IR modules do); Nucleo **5V** pin vs 3.3 V logic thresholds.
- IR OUT is often **active low** (idle high); configure EXTI/IC polarity accordingly.
- Cube: enable chosen timer **Input Capture**, NVIC for CC interrupt (or poll in a low-priority task later).
- Software: a small `ir_remote` / `ir_nec_decode` module (edge buffer or capture FIFO → frame decode); keep it separate from `led_strip_control` like audio paths.

---

## Coding style preferences

- Hobby project; not safety-critical. Good practice over strict MISRA.
- **Allman** brace style.
- **Simplified Hungarian** throughout `App/`:
  - **Public functions:** `{hung}_{module}_{verb}` — e.g. `x_led_strip_create()`, `v_i2s_audio_out_stop()`.
  - **File-private functions:** same pattern (`x_led_strip_registry_add()`, `v_i2s_audio_out_produce_half()`, …).
  - **Parameters and locals:** Hungarian only (`p_x_handle`, `u16_frames`, `b_created`) — no module prefix in the identifier.
  - **Struct/union members:** Hungarian only (`u16_strip_length`, `b_initialized`) — context comes from the struct type/instance (`led_strip_handle_t`), not `u16_led_strip_length`.
  - **Types/enums/macros:** `led_strip_err_t`, `LED_STRIP_ERR_OK` (module + role) are unchanged.
- Balance descriptive names with brevity; treat identifiers **> 40 characters** as suspect.
- **CubeMX / `Core/` (critical):** Treat `Core/` as **STM32CubeMX-owned**. Do **not** edit files under `Core/` (including `Core/Src/sai.c`, `main.c`, `stm32l4xx_it.c`, etc.) unless the user **explicitly** asks for a Core change in that task. **Never** change generated lines outside `/* USER CODE BEGIN … */` / `/* USER CODE END … */` without permission. Peripheral fixes (clocks, pins, DMA, SAI) belong in **`LED_Strip_Controller_L476.ioc`** → **Generate Code**, then verify the generated init. Application logic stays in **`App/`**. AI agents: if a fix would touch `Core/`, **stop and tell the user what to set in CubeMX** instead.
- **HAL** by default; register access and **LL** are fine when clearer or faster.
- **GNU C11** (`-std=gnu11`): C11 plus GCC extensions; portability to other toolchains is not a goal.
- **Doxygen**-style API comments; `//` or `/* */` for non-obvious logic and section breaks.

**LED driver reference:** `App/Inc/led_strip_control.h`, `App/Src/led_strip_control.c`.

---

## Test board LED layout

Physical index map for demo patterns and future effects. The ring diagram is a **top-view graphic**—view in a **monospaced** font.

### Strip [1] — WS2812B RGB (21 LEDs: center + 8 + 12)

Serial indices **0** = center, **1–8** = middle ring, **9–20** = outer ring.

```
           15*
       14*       *16
    13*     5*      *17
         4*    6*
   12*  3*  0*  7*   *18       [1] WS2812B RGB
         2*    8*
    11*     1*      *19
       10*       *20
            9*
```

### Strips [2]–[4] — SK6812 RGBW (10 LEDs each)

Horizontal lines, index **0** = left, **9** = right (as wired on the test board).

```
*0 *  *  *  *  *  *  *  *  *9  [2] SK6812 RGBW

*0 *  *  *  *  *  *  *  *  *9  [3] SK6812 RGBW

*0 *  *  *  *  *  *  *  *  *9  [4] SK6812 RGBW
```

---

## Architecture notes

### LED driver and RTOS

`led_strip_control` should stay **RTOS-agnostic**: no FreeRTOS includes, no task/semaphore APIs inside the driver. It already fits bare metal (HAL UART/DMA, optional `malloc`, completion via `v_led_strip_uart_tx_complete()` / `v_led_strip_uart_error()` from `HAL_UART_*Callback` in `app_main.c`).

Public LED API examples: `x_led_strip_create()`, `x_led_strip_update()`, `x_led_strip_destroy()`, `v_led_strip_uart_tx_complete()`.

RTOS-specific behavior belongs in **`App/`** (or a thin adapter), for example:

- A task or timer drives animation; it calls `x_led_strip_update()` and waits on a binary semaphore signaled from `HAL_UART_TxCpltCallback` (instead of spinning on `b_transfer_in_progress`).
- One mutex per strip if multiple tasks could touch the same handle.
- Replace `b_wait_transfer_idle()` polling in `x_led_strip_destroy()` at the **call site** by passing `u32_timeout_ms == 0` and handling teardown from a task that already uses RTOS waits—or add an optional completion callback later without pulling in RTOS inside the `.c` file.

Use `#ifdef` / a single project switch (e.g. `USE_FREERTOS` in `platform.h`) only in application glue, not in the encoder/DMA core, unless a future hook is truly shared.

**When to add FreeRTOS:** defer until the first feature that needs **real concurrency** (continuous I2S/ADC audio capture + LED refresh is the usual trigger). The current super-loop + blocking debug menu is sufficient for bring-up and static LED tests.

### Audio modules — naming (namespace)

The project will have **parallel analog and digital** audio paths. **Files and types** use the path segment (`i2s_`, `dac_`, …). **Public functions** use **Hungarian prefix**, then **path**, then role:

`{hungarian}_{path}_audio_out_{verb}()` — e.g. `x_i2s_audio_out_init()`, `v_i2s_audio_out_start()`, `u32_i2s_audio_out_get_stream_time_ms()`, `b_i2s_audio_out_is_idle()`.

Parameters and locals keep Hungarian as elsewhere (`p_cfg`, `pfn_fill`, `u16_frames`, …). Types: `i2s_audio_out_config_t`, `i2s_audio_out_err_t`, `i2s_audio_out_fill_fn_t`.

| Path | Module (files) | Example public functions |
|------|------------------|---------------------------|
| LED strips (USART/DMA) | `led_strip_control.c` / `.h` | `x_led_strip_create()`, `x_led_strip_update()`, `v_led_strip_uart_tx_complete()` |
| I2S / SAI (MAX98357, INMP441) | `i2s_audio_out.c` / `.h` | `x_i2s_audio_out_init()`, `x_i2s_audio_out_start()` |
| Analog out (STM32 DAC, etc.) | `dac_audio_out` (TBD) | `x_dac_audio_out_init()`, … |

Do **not** use generic names like `audio_out_init()` or `led_strip_create()` (no Hungarian / no path segment) in `App/` — they collide once multiple paths exist.

**I2S playback (`i2s_audio_out`):** SAI2.B → MAX98357 (`I2S_AUDIO_OUT_SAI_HANDLE` in `platform.h`). Ping-pong circular DMA; fill callback supplies 16-bit PCM (mono or stereo-interleaved); module packs 24-bit stereo wire words. Public API: `x_i2s_audio_out_init`, `x_i2s_audio_out_start`, `v_i2s_audio_out_stop`, `b_i2s_audio_out_is_idle`, `u32_i2s_audio_out_get_chunks_completed`, `u32_i2s_audio_out_get_stream_time_ms`, `u32_i2s_audio_out_get_sample_rate_hz`, `v_i2s_audio_out_callback_signal_eof`. Bench sine tones: `i2s_test_tone` (`x_i2s_test_tone_run_sine_until_key`), debug menu **`i`**. HAL forwards in `app_main.c`: `HAL_SAI_TxHalfCpltCallback` / `TxCpltCallback` / `ErrorCallback` → `v_i2s_audio_out_sai_*`. Init only when idle; EOF via `I2S_AUDIO_OUT_FILL_EOF` or `v_i2s_audio_out_callback_signal_eof()`; drain plays silence halves then stops DMA.

**SAI2 clock (amp) — fix in CubeMX, not hand-edited `Core/`:** PLLSAI2 → **8 MHz** SAI2 kernel (`RCC.SAI2Freq_Value=8000000`). I2S **24-bit stereo** → **64-bit** frame. With **No Divider = Disabled** (`SAI_MASTERDIVIDER_DISABLE`, NODIV=1), **Fs = SAI_CK / (MCKDIV × 64)**. Cube’s **Real Audio Frequency** line shows **31.25 kHz** only when **MCKDIV = 4**; **MCKDIV = 1** yields **125 kHz** LRCLK (**8 µs** period). Selecting **Audio Frequency = 32 kHz** alone lets L476 HAL recompute the wrong divider — scope **125 kHz** / **800 kHz** BCLK (32× ratio) is a symptom.

**CubeMX steps (SAI2 block B master TX, after regen verify `Core/Src/sai.c`):**

1. **Connectivity → SAI2 → Block B** — keep Master TX, I2S, 24-bit, 2 slots (unchanged).
2. **Parameter Settings** — set **Audio Frequency** to **Master Clock Divider** / **MCKDIV** (wording varies by Cube version; *not* the fixed “32 kHz” enum only).
3. Set **Master Clock Divider (MCKDIV) = 4** (because 8 MHz ÷ (4 × 64) = 31.25 kHz).
4. Leave **No Divider** as **Disabled** (current `.ioc`: `SAI2.NoDivider-SAI_B_Master=SAI_MASTERDIVIDER_DISABLE`) so frame length stays 64.
5. Confirm Cube still reports **Real Audio Frequency ≈ 31.25 kHz** (and ~−2.34 % error vs 32 kHz nominal).
6. **Generate Code** — generated `MX_SAI2_Init()` should contain `SAI_AUDIO_FREQUENCY_MCKDIV` and `.Init.Mckdiv = 4` (exact lines may vary). Repeat for **SAI1 block A** (mic) if you use it.
7. Bench: **LRCLK ~32 µs** period, **BCLK ~2 MHz** (64× LRCLK). MAX98357: **SD** high (~3.3 V).

**RCC (unchanged):** `SAI2` clock source = **PLLSAI2** (`RCC.SAI2CLockSelection=PLLSAI2`).

**Analog playback:** spec later; keep the same pattern (`<path>_audio_out_*`).

HAL callbacks in `app_main.c` forward to `i2s_audio_out_*` / `i2s_audio_in_*` helpers, not mixed with `dac_*`.

---

## Document history

| File | Notes |
|------|--------|
| `AI-Readme.md` | Living project guide for agents and contributors; amend as the design evolves. |
