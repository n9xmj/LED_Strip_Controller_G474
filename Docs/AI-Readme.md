# LED_Strip_Controller_G474

*Guide for AI coding assistants (Cursor, Claude, Grok, etc.) and human collaborators.*

**This is a living document.** Update it as the project evolves.

## Repository lineage (three related repos)

| Repo | MCU | Role | GitHub |
|------|-----|------|--------|
| Original | STM32G0B0 | Initial USART/DMA LED driver development | [n9xmj/LED-Strip-Controller](https://github.com/n9xmj/LED-Strip-Controller) |
| L476 port | STM32L476RG | Validated working baseline (LED patterns + I2S audio test tone) | [n9xmj/LED_Strip_Controller_L476](https://github.com/n9xmj/LED_Strip_Controller_L476) |
| **G474 port (this tree)** | **STM32G474RE** | **Current forward development baseline** (Nucleo-G474RE) | [n9xmj/LED_Strip_Controller_G474](https://github.com/n9xmj/LED_Strip_Controller_G474) |

Prior G0B0 work lives in the separate repo [LED-Strip-Controller](https://github.com/n9xmj/LED-Strip-Controller). This tree is the **STM32G474** migration and forward development baseline.

---

## Project status (snapshot)

| Area | Status |
|------|--------|
| USART + DMA LED driver (`App/led_strip_control.*`) | Ported + fully debugged (incl. UART5/LED_CHANNEL_5); debug menu `t` tests complete with clean TX completion (HAL force-cplt calls inside USER CODE blocks in `stm32g4xx_it.c` as regen-safe). All 5 strips software-ready. |
| Test hardware (4 strips + 5th expansion) | Wired per layout; 5th (UART5) ready in software/platform.h but no HW/debug-menu test hook yet. |
| GitHub | [n9xmj/LED_Strip_Controller_G474](https://github.com/n9xmj/LED_Strip_Controller_G474) |
| I2S audio out (`App/i2s_audio_out.*`, SAI1.A → MAX98357 clone) | 16-bit mono wire format (duplicated L/R slots for single-speaker hardware); ping-pong DMA; clean 440 Hz sine achieved on bench at actual Fs≈33.2 kHz (MCKDIV=20, FRL+1=32). Temp diagnostics removed. Present config kept. Future: reuse path to audition I2S mic (24b→16b compress OK). |
| Logging API (`App/logging-api`) | Legacy copy imported (build currently disabled in IDE); debug_config.h macros (LOGCT/LOGC etc. for tags+colors) to be integrated; will likely migrate/fold some content into platform.h or device_config.h. |
| CORDIC / FMAC + CMSIS-DSP | IPs enabled in CubeMX; prebuilt G4 DSP libs linked in project. Side project: use CORDIC SIN (Q31, phase accumulator) for LUTless sine synthesis in tone generator or effects. |
| RTOS / audio in (INMP441) / TFT / gesture sensor | Not started (I2S mic deferred after logging + CORDIC experiments) |

**UART strip map (CubeMX / `platform.h`):**

| Strip | Protocol | LEDs | MCU peripheral |
|-------|----------|------|----------------|
| [1] | WS2812B | 21 (1 + 8 + 12 ring) | USART1 |
| [2] | SK6812 RGBW | 10 (line) | USART3 |
| [3] | SK6812 RGBW | 10 (line) | UART4 |
| [4] | SK6812 RGBW | 10 (line) | LPUART1 |
| [5] | SK6812 RGBW | (TBD / expansion) | UART5 (LED_CHANNEL_5_UART_HANDLE) |

Debug console: **USART2** (ST-Link VCP). LED test submenu: **`t`**. I2S audio test submenu: **`i`**. (Note: no dedicated debug menu entry for strip 5 yet.)

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

- **MCU board:** STM32 Nucleo-64 **G474RE** (Nucleo-G474RE) + external test PCB (hardware evolves as needed).
- **STM32CubeMX** — pin/peripheral configuration.
- **STM32CubeIDE** — edit, build, flash, debug (GCC).
- **Tera Term** — serial console, debug menu interaction.
- Bench test equipment as needed.
- **Reference PDFs** in `Docs/` (WS2812, SK6812, ST7789, INMP441, Nucleo G474, Nucleo L476, STM32G4 RM, etc.).

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
- [x] **2a.** Create remote repo for G474 port under personal n9xmj account and initialize with .gitignore (Debug/Release/.settings excluded; Cube project files retained).
- [ ] **3.** Add an RTOS (FreeRTOS + CMSIS-OS wrappers; ST middleware).
- [ ] **3a.** Configure project for efficient RTOS use (tasks, priorities, driver interaction).
- [ ] **4.** I2S microphone input (STM32 SAI or I2S + DMA).
- [ ] **4a.** Analog microphone via ADC + DMA.
- [x] **5.** I2S audio output for testing (SAI1.A + DMA) — `i2s_audio_out` API; 16b mono duplicated-slots wire format for MAX98357 single-speaker; sine bench tests in debug menu **`i`** (clean 440 Hz tone achieved).
- [ ] **5a.** Audio output via on-chip DAC + DMA.
- [ ] **6.** Gesture sensor: VL53L5CX (VL53L5CX-SATEL board).
- [ ] **7.** Small TFT LCD on SPI (e.g. SPI2); consider open-source drivers (e.g. Bodmer / community ST7789 code).
- [ ] **8.** CMSIS-DSP audio processing (scope TBD).
- [x] **9.** Debug menu bench tests — *ongoing*; expand as each feature lands (LED submenu under **`t`** today).
- [ ] **10.** Integrate legacy logging API (App/logging-api copied in; build disabled for now). Preserve/adapt debug_config.h style macros (LOG/LOGC/LOGCT + TAG/COLOR per-class filtering + ANSI colors via LOGCT etc.). Likely fold debug_config content into platform.h / device_config.h over time. Route main app + driver logs through it (not just debug menu).
- [ ] **11.** CORDIC (and FMAC) experiment: use CORDIC SIN/COS (Q31, 14-cycle precision, phase-accumulator driven) for sine synthesis in place of software LUT or sinf(); natural fit for G4 trig accel. (FMAC for possible filtering later.) CMSIS-DSP prebuilts already linked.

---

## TODO wishlist (low priority / experiments)

Items here are **optional**—try when curiosity or bench time allows. Not ordered; no schedule implied.

- [ ] **IR remote control receiver** — integrate a demodulated IR module (e.g. VS1838B / TSOP38xxx class: VCC, GND, digital OUT). Decode NEC (or similar) in firmware; map keys to LED patterns, menu actions, or audio-test triggers. See [IR receiver — L476 IP options](#ir-receiver--l476-ip-options) below.

---

### IR receiver — L476 IP options (historical reference)

The STM32L476 has **no dedicated IR peripheral**. A 3-pin IR receiver module already demodulates the 38 kHz carrier; the MCU only decodes **logic-level pulse widths** (NEC / RC5 / Sony, etc.) in software.

> **Note for G474:** The G4 family has more timer options and potentially different low-power / EXTI behavior. Re-evaluate timer input capture choices against the current pinout and `LED_Strip_Controller_G474.ioc` when this feature is worked.

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
- **CubeMX / `Core/` (critical):** Treat `Core/` as **STM32CubeMX-owned**. Do **not** edit files under `Core/` (including `Core/Src/sai.c`, `main.c`, `stm32g4xx_it.c`, etc.) unless the user **explicitly** asks for a Core change in that task. **Never** change generated lines outside `/* USER CODE BEGIN … */` / `/* USER CODE END … */` without permission. 

  **Autogenerated code regeneration rule (mandatory):** The project must build cleanly and function correctly after a full "Generate Code" from the `.ioc` file with no manual edits outside USER CODE sections. All custom code, workarounds, diagnostics, or extensions in CubeMX-owned files **must** be placed strictly inside the `/* USER CODE BEGIN xxx */` … `/* USER CODE END xxx */` markers that CubeMX preserves. If a change cannot be expressed inside those markers, implement it via settings in the `.ioc` (preferred) or obtain explicit user approval for an exception. AI agents must verify after any Core edit that regeneration would not break the build. Peripheral configuration changes always go through the `.ioc` first.

  Application logic stays in **`App/`**. AI agents: if a fix would touch `Core/`, **stop and tell the user what to set in CubeMX** instead.
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

**I2S playback (`i2s_audio_out`):** SAI1 Block A (Master TX) → MAX98357 clone single-speaker breakout (`I2S_AUDIO_OUT_SAI_HANDLE` in `platform.h`). Ping-pong circular DMA (M2P, WORD-aligned); fill callback supplies 16-bit PCM (mono or stereo-interleaved); module packs to 32-bit wire words (sample | (sample<<16) duplicated for the two active 16b slots). Current config: MONO mode, 2 slots (0+1 active=0x3), FrameLength=32, 16b data, NoDivider=ENABLE, MCKDIV≈20 → actual Fs ≈ 33.203 kHz (better than nominal 32 kHz target; measured/derived from regs in `u32_i2s_audio_out_compute_fs_hz`). Public API and bench usage as before. HAL forwards in `app_main.c`. Drain logic + silence halves on stop/EOF. See `App/Inc/i2s_audio_out.h` future note for INMP441 mic reuse (24b→16b compress acceptable for initial audition via this path).

**SAI clock / wire format — fix in CubeMX, not hand-edited `Core/`:** All SAI1.A params (AudioMode=SAI_MODEMASTER_TX, DataSize=16, FrameLength=32, MonoStereoMode=MONO, SlotNumber=2, SlotActive=0x00000003, NoDivider=ENABLE, MckOutput=DISABLE) and DMA (circular, mem WORD, periph WORD, M2P) are controlled via the `.ioc`. After regen, verify `Core/Src/sai.c` and `Core/Src/stm32g4xx_it.c` (callbacks stay in USER CODE only). Drive strength on SAI pins (PA8 SCK_A, PA9 FS_A, PA10 SD_A AF14) was lowered (MED/LOW) as short-term mitigation for jumper-wire ringing; probe load can mask issues. Future: proper series R + small C or shorter wiring.

**RCC:** SAI1 kernel clock source and PLL config per current `.ioc` (G474RE specifics; MCKDIV effective 20 for ~33 kHz). `u32_i2s_audio_out_get_sample_rate_hz()` and tone generator report the live-derived Fs.

**Analog / other playback:** TBD later; keep the same pattern (`<path>_audio_out_*`).

HAL callbacks in `app_main.c` forward to `i2s_audio_out_*` helpers.

---

## Document history

| File | Notes |
|------|--------|
| `AI-Readme.md` | Living project guide for agents and contributors; amend as the design evolves. |
| 2026-06 (this session) | Created n9xmj/LED_Strip_Controller_G474 remote (public, personal account). Added explicit three-repo lineage table. Updated for G474RE as current baseline. .gitignore + initial commit. |
| 2026-06 (later) | LED driver bring-up completed (5th strip UART5 software support + force HAL_UART_TxCpltCallback in USER CODE blocks of `stm32g4xx_it.c` for G4 HAL callback path; no more timeouts/BUSY; FIFOs off for LED UARTs). I2S audio: finalized 16b mono duplicated wire format + DMA alignments; clean 440 Hz tone confirmed; temp [tone diag] spam removed; actual Fs~33.2 kHz documented. Logging API imported (`App/logging-api` present, IDE build disabled for this commit only). CORDIC+FMAC already enabled in .ioc + CMSIS DSP prebuilts linked; side-project queued. AI-Readme updated (status, strip map incl. #5, TODOs 10/11, architecture notes for current SAI1.A 16b config). "Autogenerated code regeneration rule (mandatory)" already present. |

---

**End of AI-Readme.md** — keep this file accurate and up to date.
