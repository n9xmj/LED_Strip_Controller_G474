# INMP441 I2S mic — bench wiring (G474)

**Category:** hardware / firmware handoff  
**Validated:** 2026-06-20 (DMA ping-pong → job queue path; dBFS VU meter + LED bargraph on bench)

## Bench configuration (current)

Single **INMP441** on **I2S2** (PB12 WS, PB13 CK, PB15 SD). MCU is **I2S master RX**; CubeMX: Philips, 24-bit, ~32 kHz.

| INMP441 pin | Connection |
|-------------|------------|
| SCK | I2S2_CK (shared clock) |
| WS | I2S2_WS (shared LRCLK) |
| SD | I2S2_SD (data in) |
| **L/R (SEL)** | **GND → left channel** |

## Firmware implication (must-ship for handlers)

INMP441 is **mono**: it drives **SD only during the left WS half** and **tri-states** on the right half. Scope shows SD active ~50% of LRC period — expected, not a fault.

- **Use left-channel slots only** when computing level, RMS, FFT, or playback — or auto-select the slot with meaningful AC energy (see bring-up meter).
- **Right-channel samples are garbage** (floating bus / undefined reads). Do **not** mix both slots, and do **not** take `max(L, R)` on raw RMS — the inactive slot can peg meters.
- **24-bit unpack (STM32 I2S2, 24b in 32b slot) — halfword order matters:** the DMA/HAL stores the **first** received halfword (sample bits [23:8]) at `pair[0]` and the **second** (bits [7:0] in its upper byte) at `pair[1]`. Reassemble **`raw = (pair[0] << 16) | pair[1]`**, then `(int32_t)((int32_t)raw >> 8)` (left-justified in DR). **Do not** swap the words (`(pair[1]<<16)|pair[0]`) — that lands the LSBs high and pegs meters at `pk≈32768`/`ac≈19000` regardless of sound (bench-confirmed 2026-06-19). **Do not** use low-24 (`<<8>>8`) either (pk≈16M garbage).
  - ⚠️ Supersedes the earlier (2026-06-18) note that recorded `(pair[1]<<16)|pair[0]` as validated — that order was wrong; the polled `m`/`d` meters used it and never cleanly distinguished silence.
- **Mono on shared SD:** per WS pair take `max(|slot0|, |slot1|)` with the `>>8` decode above — immune to L/R slot slipping.
- **VU / level meters:** use **AC RMS** on the mono-pair stream. INMP441 has DC bias; total RMS tracks DC, not ambient audio.

## DMA config (SPI2_RX / I2S2 — bench-confirmed 2026-06-19)

Set in the `.ioc` (single source of truth; the earlier runtime override in `i2s_audio_in.c` was removed once the `.ioc` was corrected):
- **Mode = Circular** (ping-pong half/complete).
- **Peripheral + Memory data width = Half Word** (16-bit). CubeMX originally generated **Byte**, which corrupts the I2S stream (works in polled mode because that path bypasses DMA). Symptom of Byte: `ac=0/pk=0` runs then a stuck constant ("lost sync").

## Stereo expansion (two mics, future)

Shared CLK + WS + **SD**; mic A **L/R→GND**, mic B **L/R→VDD**. Each mic owns one WS half; both slots then carry real audio.

## Cross-refs

- `I2S_AUDIO_IN_I2S_HANDLE` → `hi2s2` in `App/Inc/platform.h`
- Driver: `i2s_audio_in` (`App/i2s_audio_in.*`) — circular DMA ping-pong; per-half decode → mono PCM; consume callback runs in **ISR** context (do not block).
- Main-context ingest: `audio_in_service` (`App/audio_in_service.*`) — ISR posts **`JOB_I2S_AUDIO_IN_CHUNK`** (param1=half index, param2=frames, pointer=PCM half) to `gx_job_queue`; `v_process_next_job` runs the registered chunk handler. This is the path real DSP/LED stages hook into. NULL handler = built-in AC RMS + peak stats.
- Bench tools (debug menu **`i`** submenu), both off the job-queue stream:
  - **`m`** — VU meter: dBFS bar (−60…0), display-only digital gain knob (`+`/`-`/`0`), numeric readout = true pre-gain level. Mirrors to a 10-LED SK6812 bargraph on `LED_CHANNEL_2` (5 green / 3 yellow / 2 red), DMA fire-and-forget.
  - **`r`** — DMA stream bench: prints `chunks/ac/pk` 1×/s.
  - **`d`** polled diagnostic retired 2026-06-19.
- **Sensitivity:** fixed-gain digital mic (−26 dBFS @ 94 dB SPL, SNR ~61 dB, overload ~120 dB SPL). Yell-on-mic ≈ −3 dBFS; silence floor ≈ −80 dBFS. Ambient = small linear / large dB above floor → meter & DSP should be **dB-based** with digital gain/AGC.
