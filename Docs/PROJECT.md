# LED_Strip_Controller_G474 — Project Overview

**STM32G474RE (Nucleo-G474RE) LED strip controller with audio output.**

This is the current forward development baseline for a hobby firmware project.

**Remote:** https://github.com/n9xmj/LED_Strip_Controller_G474 (personal account, public)

See [AGENTS.md](../AGENTS.md) for coding rules, architecture invariants, and instructions for AI assistants.

## Repository Lineage

| Repo | MCU | Role | GitHub |
|------|-----|------|--------|
| Original | STM32G0B0 | Initial USART/DMA LED driver development | [n9xmj/LED-Strip-Controller](https://github.com/n9xmj/LED-Strip-Controller) |
| L476 port | STM32L476RG | Validated working baseline (LED patterns + I2S audio test tone) | [n9xmj/LED_Strip_Controller_L476](https://github.com/n9xmj/LED_Strip_Controller_L476) |
| **G474 port (this tree)** | **STM32G474RE** | **Current forward development baseline** (Nucleo-G474RE) | [n9xmj/LED_Strip_Controller_G474](https://github.com/n9xmj/LED_Strip_Controller_G474) |

Prior G0B0 work lives in its own repo. This tree is the STM32G474 migration and ongoing development home.

## Current Status (Snapshot)

| Area | Status |
|------|--------|
| USART + DMA LED driver (`App/led_strip_control.*`) | Ported + fully debugged (including UART5 / LED_CHANNEL_5). Debug menu `t` tests complete with clean TX completion. All 5 strips software-ready. |
| Test hardware (4 strips + 5th expansion) | Wired per layout; 5th (UART5) ready in software but no dedicated debug-menu test hook yet. |
| I2S audio out (`App/i2s_audio_out.*`, SAI1.A → MAX98357 clone) | 16-bit mono wire format (duplicated L/R slots for single-speaker hardware); ping-pong DMA. Clean 440 Hz sine achieved on bench at actual Fs ≈ 33.2 kHz. |
| Logging API (`App/logging-api`) | Fully integrated and restructured. Sugar macros in `log_helpers.h`; project tags in `App/Inc/debug_config.h`. Reusable template provided. |
| CORDIC / FMAC + CMSIS-DSP | IPs enabled in CubeMX; prebuilt G4 DSP libs linked. Side experiments (e.g. CORDIC for sine synthesis) are possible. |
| Debug menu | Functional. Top-level `@` key reprints the full startup banner (useful for identification). |
| Automation / scripts + skills | Complete custom `scripts/` suite (build, flash, smoke-test, discover, etc.) + project-local skills under `.grok/skills/`. Full round-trips exercised. Legacy reference scripts purged from repo history. |
| RTOS / audio in (INMP441) / TFT / gesture sensor | Not started. I2S mic input was deferred after logging + CORDIC work. |

### UART Strip Map

| Strip | Protocol | LEDs | MCU peripheral |
|-------|----------|------|----------------|
| [1] | WS2812B | 21 (1 + 8 + 12 ring) | USART1 |
| [2] | SK6812 RGBW | 10 (line) | USART3 |
| [3] | SK6812 RGBW | 10 (line) | UART4 |
| [4] | SK6812 RGBW | 10 (line) | LPUART1 |
| [5] | SK6812 RGBW | (TBD / expansion) | UART5 |

**Debug console:** USART2 (ST-Link VCP) at 921600 baud.  
LED test submenu: `t`. I2S audio test submenu: `i`.

## Project Overview & Long-Term Goals

This is a hobby bare-metal STM32 firmware project with the following aspirations:

- **Core (largely done):** Drive WS2812B and SK6812 RGB(W) LEDs using the STM32 USART line-encoding technique (TX invert, 7-bit words, ~2.4 Mbaud, DMA).
- **Audio-reactive lighting:** Detect audio (I2S digital mic or analog) and synchronize LED effects.
- **Gesture / “theremin” interaction:** Hand gestures (e.g. VL53L5CX time-of-flight) to control lighting and possibly sound.
- **Status UI:** Small TFT over SPI (ST7789-class or similar).
- **DSP:** Use CMSIS-DSP (and G4 hardware accelerators CORDIC/FMAC) for audio processing.
- **Networked / smart features:** Offload WiFi, Bluetooth, and MQTT (AWS or local) to an ESP32 coprocessor module communicating over bidirectional UART. Enables remote control, pattern upload, telemetry, and home-automation integration without burdening the real-time STM32 core.
- Other ideas may be added over time.

**Author background:** Strong experience with bare-metal STM32 (especially low/mid-range parts). Less experience with RTOS, DSP, audio signal chains, and TFT displays. Moderate electrical engineering background.

## Development Environment & Hardware

- **MCU board:** STM32 Nucleo-64 G474RE + external test PCB (hardware evolves).
- **Tools:** STM32CubeMX (.ioc) for pin/peripheral config, STM32CubeIDE for editing/building/flashing/debugging (GCC), Tera Term for serial console/debug menu.
- **Reference material:** PDFs in `Docs/` (WS2812, SK6812, ST7789, INMP441, Nucleo G474/L476, STM32G4 RM0440, MAX98357, etc.).

**Current / planned test hardware:**
- WS2812B and SK6812 LED strips
- INMP441 I2S microphone
- Electret microphone + preamp (see `Docs/Analog Electret Microphone Preamp.md`)
- MAX98357 I2S amplifier breakout
- TFT displays (ST7789 class)
- VL53L5CX time-of-flight sensor (wishlist for gestures)
- IR remote receiver (demodulated 38 kHz, low priority)

## Test Board LED Physical Layout

### Strip [1] — WS2812B RGB (21 LEDs: center + 8 + 12)

Serial indices: **0** = center, **1–8** = middle ring, **9–20** = outer ring.

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

Horizontal lines, index **0** = left, **9** = right.

```
*0 *  *  *  *  *  *  *  *  *9  [2] SK6812 RGBW

*0 *  *  *  *  *  *  *  *  *9  [3] SK6812 RGBW

*0 *  *  *  *  *  *  *  *  *9  [4] SK6812 RGBW
```

## Milestones Attained (High Level)

- Successful migration of the LED driver and initial audio test tone from the L476 validation port to the G474RE.
- Resolution of DMA completion and HAL callback issues for all LED channels (FIFOs off + minimal USER-CODE callbacks).
- Correct SAI configuration and 16-bit wire packing for reliable single-speaker audio output (clean 440 Hz tone verified on bench).
- Restructuring of the custom logging API into a reusable, portable form with project-specific tag configuration.
- Implementation of a proper bordered startup banner that includes versioning and reset cause.
- Full custom automation layer (PowerShell + shell scripts + Python helpers) for headless build, flash (with multi-probe discovery and accessibility checks), and smoke testing (including fast banner capture via concurrent reset or `--identify`).
- Project-local Grok skills (`.grok/skills/`) providing convenient `/build`, `/cleanbuild`, `/roundtrip`, `/fixme`, `/flash`, `/smoke`, `/probe`, `/setver`, etc. commands that use local bench defaults.
- Complete removal of proprietary reference material from the public remote history (reference copies remain only locally in the gitignored `not-in-project/` directory).
- Addition of the `@` debug menu shortcut for quick banner reprint / board identification.

## TODO Checklist

Items may be done out of order. Toggle boxes as work completes.

- [x] Get migrated project (G0B0 → L476) up and running as the work baseline.
- [x] Convert project notes to Markdown.
- [x] Create remote repo under personal n9xmj account with appropriate .gitignore.
- [ ] Add an RTOS (FreeRTOS + CMSIS-OS wrappers).
- [ ] Configure project for efficient RTOS use (tasks, priorities, driver interaction).
- [ ] I2S microphone input (STM32 SAI or I2S + DMA).
- [ ] Analog microphone via ADC + DMA.
- [x] I2S audio output for testing (SAI1.A + DMA) — clean 440 Hz tone achieved.
- [ ] Audio output via on-chip DAC + DMA.
- [ ] Gesture sensor: VL53L5CX (VL53L5CX-SATEL board).
- [ ] Small TFT LCD on SPI (e.g. SPI2); consider open-source drivers.
- [ ] CMSIS-DSP audio processing (scope TBD).
- [x] Debug menu bench tests — ongoing; expand as features land.
- [x] Integrate and restructure legacy logging API.
- [ ] CORDIC (and FMAC) experiment: use hardware trig acceleration for sine synthesis, filtering, etc. (prerequisite for music sequencer / player-piano; see wishlist below and [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md)).

## TODO Wishlist (Low Priority / Experiments)

- [ ] IR remote control receiver (demodulated 38 kHz module). Decode NEC-style and map keys to patterns, menu actions, or test triggers. G4 timer input capture options should be re-evaluated against the current `.ioc` pinout when this is worked.

- [ ] USB device usage (STM32G474 USB FS). Explore standard classes for project-relevant features: CDC (command-and-control interface or alternative debug console), HID (lighting pattern upload or custom controls), MIDI (note input to drive the synth / player-piano), or similar. Prefer composite device if multiple classes are combined. Keep initial scope simple; re-evaluate .ioc pinout/clock config and USB middleware if pursued.

- [ ] ESP32 coprocessor module (e.g. ESP32-C3/C6 or S3). Provide WiFi, Bluetooth, and MQTT connectivity (to AWS IoT or a local broker) for remote control, pattern/effect upload, state telemetry, and home-automation integration. Communicate over a bidirectional UART link (following the same architecture as the mirror project) so the main STM32 can stay focused on real-time LED driving, audio synthesis, and low-latency tasks. This also sidesteps STM32 pinmux and RAM constraints for networking features.

- [ ] External storage + lightweight filesystem (onboard QSPI NOR flash + microSD). Use LittleFS on NOR flash (power-loss safe, wear-leveled, low RAM) for internal patterns, sequences, config, and wavetables. Use FatFs/FAT32 on SD for user-friendly drag-and-drop of lighting patterns, MIDI sequences, or audio samples from a PC. Stream larger data (samples, complex sequences) on demand to avoid loading everything into the 128 KB SRAM. Perfect for the player-piano sequencer (see [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md)) and future polyphonic/sample-based synthesis. The H7 boards already have these peripherals populated.

- [ ] Music sequencer / player-piano API. See [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md) for the complete PLAY meta-language specification (note descriptors, durations W/H/Q/I/X/Y + dot, articulation, duty, commands R/T/O/K/V, repeats, labels/gotos, polyphony via independent voices + conductor model, EBNF, parser/scheduler, storage integration, etc.), design, and roadmap. The CORDIC synth engine and interactive note player are stepping stones.
- [x] Interactive note player (experimental for-fun, 'p' from main debug menu). Monophonic sustained tones via terminal keys (1-8/a-g/A-G layout, octave/vol controls, status, script-friendly short responses). Uses the CORDIC synth engine (set_tone + set_level for live changes, quiet path). 2^(n/12) freq calc from C1 base (no LUT). Precursor / validation for the full sequencer. See Docs/Interactive noteplayer spec.txt and [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md) for the planned PLAY meta-language sequenced playback.

See the historical IR options section that was carried forward from the L476 work if needed for reference.

## Automation

**Agents and humans:** see [SCRIPTS.md](../SCRIPTS.md) for complete, agent-friendly instructions on the build / flash / smoke / probe scripts.

Project-local slash commands (the convenient shorthand layer) are documented via `/myskills`.

## References

- Hardware datasheets and app notes: `Docs/` folder
- Coding rules and agent instructions: [AGENTS.md](../AGENTS.md)
- Scripts reference: [SCRIPTS.md](../SCRIPTS.md)
- Project-local skills: `.grok/skills/`
- PLAY meta-language design (player-piano / sequencer spec, parser plans, polyphony model, storage integration): [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md)

**This is a living document.** Update it as goals, status, or the roadmap evolve.

**End of Docs/PROJECT.md**