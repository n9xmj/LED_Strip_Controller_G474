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
- **Audio synthesis trajectory:** G474 today — monophonic CORDIC sine + **PLAY** sequencer path. Longer term — richer voices (FM and beyond). **Wishlist:** a full **General MIDI (GM)** synthesizer as a **companion engine** to FM — fun project, but realistically needs **STM32H7-class** horsepower and a **free/open GM patch set** (research TBD; nothing selected yet).
- **Terminal “virtual piano” UI (TRS-80 heritage):** Reprise of the author’s **TRS-80 Model I** player display on the debug UART (ANSI + UTF-8). Natural consumer of PLAY **I8** and the live **`p`** note player. **Not active work** — full brief: [`Docs/planning/terminal-piano-and-player-notes.md`](planning/terminal-piano-and-player-notes.md).
- **Networked / smart features:** Offload WiFi, Bluetooth, and MQTT (AWS or local) to an ESP32 coprocessor module communicating over bidirectional UART. Enables remote control, pattern upload, telemetry, and home-automation integration without burdening the real-time STM32 core.
- Other ideas may be added over time.

**Author background:** Strong experience with bare-metal STM32 (especially low/mid-range parts). Less experience with RTOS, DSP, audio signal chains, and TFT displays. Moderate electrical engineering background. **Not a Haskell / functional-programming practitioner** — when mining **@mokus0**’s `Channel.hs` / vTree Haskell for Mk 5 DSP ideas, expect to **lean on AI agents** to decode algorithms into plain-language + embedded-friendly pseudocode; do not assume the author will read Haskell source directly.

## Product lineage — vTree+ and PLAY (author lock 2026-06-13)

This repo is **vTree Mk 5** (“vTree+”): LED strips + synthesis + (planned) audio-reactive DSP on STM32G4, in a long hobby arc that predates this hardware generation.

| Mk | Era | What |
|----|-----|------|
| **1–2** | Late high school / university | **Author original** — analog + GLU “color organ” predecessors |
| **3** | Follow-on hobby | 8-bit MCU + analog filtering + **incandescent** lamps, triac-switched low-frequency PWM |
| **4** | Independent successor | [cayuse/color_organ](https://github.com/cayuse/color_organ) (ESP32-C3) — friend’s spiritual successor; **@mokus0**’s Haskell `Channel.hs` / vTree work refines the same **auto-leveling** ideas in software |
| **5 (this tree)** | STM32G474 forward baseline | USART LED drive + CORDIC/PLAY sequencer + planned mic→FFT→strip path |

**PLAY / player thread (same author):** first **commercial software sale** (end of high school) — **TRS-80 Model I** music player + user docs; lineage through **GW-BASIC `PLAY`**, a **failed AVR** experiment, and this **PLAY meta-language** on G474. Terminal piano UI reprise: [`Docs/planning/terminal-piano-and-player-notes.md`](planning/terminal-piano-and-player-notes.md).

**Cross-reference, not authorship:** [cayuse/color_organ](https://github.com/cayuse/color_organ) is the informal **“cayuse project”** — useful algorithm patterns for Mk 5’s mic/DSP path; not a port target.

## Institutional memory — related projects

Future work on **audio-reactive lighting** (mic → analysis → LED mapping) should not start from a blank page. Keep this pointer on file. In chat, **“the cayuse project”** means this row. **vTree** began with the author (Mk 1–3); cayuse/Mk 4 and mokus0’s Haskell work are **successor refinements**, not the origin.

| Project | Link | Why look here |
|---------|------|----------------|
| **Color Organ** (cayuse) | [github.com/cayuse/color_organ](https://github.com/cayuse/color_organ) | **vTree Mk 4** — friend’s ESP32-C3 color organ: I2S → FFT → multi-band energy → adaptive leveling → attack/release → NeoPixel drive. Mine **DSP patterns** (band split, normalization, envelopes) for Mk 5; MCU/LED stack differs. |

**Lineage note:** **vTree originated with this author** (Mk 1–3). **@mokus0**’s Haskell `Channel.hs` / vTree and **cayuse**’s firmware are **later spiritual successors** that refined auto-leveling (rolling history, normal-CDF normalization, asymmetric attack/decay) — cross-check when Mk 5 DSP is scheduled, not a claim that mokus0 authored the original tree. **Author does not read Haskell** — agent-assisted translation required when those sources are mined.

**Status (2026-06-11):** Link recorded only — **not reviewed or ported** yet. Deliberately orthogonal to current **PLAY v1** sequencer/synth planning; revisit when **audio-reactive lighting** moves off the backlog (see long-term goals above). Expected overlap: INMP441 / I2S in, CMSIS-DSP FFT on G474, mapping band energy → strip pixels — not PLAY score playback.

## Development Environment & Hardware

- **MCU board:** STM32 Nucleo-64 G474RE + external test PCB (hardware evolves).
- **Tools:** STM32CubeMX (.ioc) for pin/peripheral config, STM32CubeIDE for editing/building/flashing/debugging (GCC), Tera Term for serial console/debug menu.
- **Reference material:** PDFs in `Docs/` (WS2812, SK6812, ST7789, INMP441, Nucleo G474/L476, STM32G4 RM0440, MAX98357, etc.).

**Current / planned test hardware:**
- WS2812B and SK6812 LED strips
- INMP441 I2S microphone (**L/R → GND, left channel only** on current bench — right I2S slot tri-states; see [`.grok/memory/inmp441_i2s_wiring.md`](../.grok/memory/inmp441_i2s_wiring.md))
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
- Complete removal of proprietary reference material from the public remote history (reference copies remain only locally in the gitignored `not-in-project/` directory — e.g. legacy mirror scripts, author’s **`uart_stream`** reference drop for future piano UI / non-blocking debug UART).
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
- [ ] **Migrate to the shared `automation_console` (acon), retiring the first-generation HIL interface.** Long-term; the MCU-side port is ordinary work but ~5,100 lines of host Python across 17 scripts are written against the current protocol, with no regression suite. Plan, cost and sequencing: [`Docs/planning/automation-console-migration-plan.md`](planning/automation-console-migration-plan.md).
- [ ] CORDIC (and FMAC) experiment: use hardware trig acceleration for sine synthesis, filtering, etc. (prerequisite for music sequencer / player-piano; see wishlist below and [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md)).

## TODO Wishlist (Low Priority / Experiments)

- [ ] IR remote control receiver (demodulated 38 kHz module). Decode NEC-style and map keys to patterns, menu actions, or test triggers. G4 timer input capture options should be re-evaluated against the current `.ioc` pinout when this is worked.

- [ ] USB device usage (STM32G474 USB FS). Explore standard classes for project-relevant features: CDC (command-and-control interface or alternative debug console), HID (lighting pattern upload or custom controls), MIDI (note input to drive the synth / player-piano), or similar. Prefer composite device if multiple classes are combined. Keep initial scope simple; re-evaluate .ioc pinout/clock config and USB middleware if pursued.

- [ ] ESP32 coprocessor module (e.g. ESP32-C3/C6 or S3). Provide WiFi, Bluetooth, and MQTT connectivity (to AWS IoT or a local broker) for remote control, pattern/effect upload, state telemetry, and home-automation integration. Communicate over a bidirectional UART link (following the same architecture as the mirror project) so the main STM32 can stay focused on real-time LED driving, audio synthesis, and low-latency tasks. This also sidesteps STM32 pinmux and RAM constraints for networking features.

- [ ] External storage + lightweight filesystem. **Hardware on-hand but not yet wired — choice undecided between a `W25Q128` SPI NOR flash and a microSD card** (both parts in the bin; pinout/wiring TBD against the current `.ioc`). Use **LittleFS** on SPI/QSPI NOR flash (power-loss safe, wear-leveled, low RAM) for internal patterns, sequences, config, and wavetables; or **FatFs/FAT32** on SD for user-friendly drag-and-drop of lighting patterns, MIDI sequences, or audio samples from a PC. Stream larger data (samples, complex sequences) on demand to avoid loading everything into the 128 KB SRAM. Perfect for the player-piano sequencer (see [Docs/PLAY_language_design.md](Docs/PLAY_language_design.md)) and future polyphonic/sample-based synthesis. **Berry tie-in:** once storage exists, hook this filesystem into Berry's port layer (`App/berry-lang/default/be_port.c` file ops + the `os`/`file` library modules) so Berry scripts can load/save from it. Berry's filesystem-supporting code stays in-tree but **gated off in `berry_conf.h`** (`BE_USE_OS_MODULE` etc.) until storage lands — nothing FS-related gets deleted. The H7 boards already have these peripherals populated.

- [ ] Music sequencer / player-piano API. User docs: [Docs/Player/README.md](Player/README.md). Decision log: [Docs/planning/play-v1-implementation-plan.md](planning/play-v1-implementation-plan.md). Legacy notebook: [Docs/PLAY_language_design.md](PLAY_language_design.md). On-device interpreter **v1+v1.1 shipped** (G1–G10); CORDIC synth and interactive note player are stepping stones.
- [x] Interactive note player (experimental for-fun, 'p' from main debug menu). Monophonic sustained tones via terminal keys (1-8/a-g/A-G layout, octave/vol controls, status, script-friendly short responses). Uses the CORDIC synth engine (set_tone + set_level for live changes, quiet path). 2^(n/12) freq calc from C1 base (no LUT). Precursor / validation for the full sequencer. Implemented in `App/Src/note_player.c`; see [PLAY_language_design.md](PLAY_language_design.md) for the planned PLAY meta-language sequenced playback.

- [ ] **General MIDI (GM) synthesizer (H7 wishlist).** Design a full GM synth engine as a **companion** to a future **FM** synth voice — not a G474 target. Would need substantially more CPU/RAM (likely **STM32H7** board already on the hardware roadmap). Open questions: locate a **free/open GM patch set** (SoundFont / sample bank / procedural patches — nothing vetted yet), polyphony budget, streaming samples from QSPI/SD, and how GM voices share the mix bus with FM + PLAY-driven voices. Motivation: “would be fun” — park here until PLAY + simpler synth voices are further along.

- [ ] **ANSI terminal piano keyboard (“virtual synthboard”).** **Status: idea on paper only.** Agent brief: [`Docs/planning/terminal-piano-and-player-notes.md`](planning/terminal-piano-and-player-notes.md) (layout, **I8** consumer, **JOB_PIANO_DRAW**, depends on **`uart_stream`**).
- [x] **`uart_stream` (debug USART2).** Non-blocking console TX/RX via register-level ISR — **shipped (G11)**; lives in `App/uart_stream/`, stdio routes through it. **Re-vendored 2026-08-12** from the shared `G0B1_Skeleton` module and now byte-identical across all three projects — **live test still owed**. Design reference: [`Docs/planning/uart_stream-port-notes.md`](planning/uart_stream-port-notes.md). Enables the terminal piano UI's bursty ANSI.

- [ ] **Berry scripting layer (`App/berry-lang`).** Embed the [Berry](https://github.com/berry-lang/berry) ultra-light scripting VM (ANSI C99, MIT, <40 KiB core, runs in a few KB of heap) as an *optional* on-device scripting front-end — **invoked by** the debug menu and (potentially) the automated test runner; **not** a replacement for the menu, the test REPL, or PLAY. The **PLAY interpreter is exposed as a Berry predefined/native function** so scripts can drive note sequences. Vendored as a plain source drop, **no submodule**. LLM-friendly references in [`Docs/berry-lang/`](berry-lang/) (from berry-lang [PR #525](https://github.com/berry-lang/berry/pull/525)); human-targeted reference PDF to follow.

See the historical IR options section that was carried forward from the L476 work if needed for reference.

## Automation

**Agents and humans:** see [SCRIPTS.md](../SCRIPTS.md) for complete, agent-friendly instructions on the build / flash / smoke / probe scripts.

Project-local slash commands (the convenient shorthand layer) are documented via `/myskills`.

## References

- Hardware datasheets and app notes: `Docs/` folder
- Coding rules and agent instructions: [AGENTS.md](../AGENTS.md)
- Scripts reference: [SCRIPTS.md](../SCRIPTS.md)
- Project-local skills: `.grok/skills/`
- PLAY user documentation (cheat sheet, chatbot brief, planned howto + EBNF): [Docs/Player/README.md](Player/README.md)
- PLAY legacy design notebook (historical): [Docs/PLAY_language_design.md](PLAY_language_design.md)
- PLAY v1 implementation readiness (decision-log plan, resolve D/S/I/T/Q IDs in chat): [Docs/planning/play-v1-implementation-plan.md](planning/play-v1-implementation-plan.md) · [planning model](planning/decision-log-model.md)
- Audio-reactive lighting (future, **vTree+ Mk 5**): [cayuse/color_organ](https://github.com/cayuse/color_organ) — Mk 4 cross-ref; vTree Mk 1–3 author originals — see *Product lineage* + *Institutional memory*

**This is a living document.** Update it as goals, status, or the roadmap evolve.

**End of Docs/PROJECT.md**