# LED_Strip_Controller_G474

STM32G474RE (Nucleo-G474RE) LED strip controller.

- USART + DMA line-encoding driver for WS2812B (21-LED ring) and SK6812 RGBW (3× 10-LED lines)
- Debug menu over ST-Link VCP (USART2) — `t` for LED tests, `i` for I2S audio sine tests
- I2S/SAI audio output path (toward MAX98357)
- Forward development baseline (lineage: G0B0 → L476 → G474)

**Remote:** https://github.com/n9xmj/LED_Strip_Controller_G474 (personal account, public)

See [Docs/AI-Readme.md](Docs/AI-Readme.md) for full project guide, hardware layout, coding style, and TODOs. This document (and the AI-Readme) are living documents.

Current focus: debug/bring-up of the G474 port (debug menu works; LED strip driving and audio paths need validation/fixes).
