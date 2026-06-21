# `uart_stream` design notes (G474, USART2 console)

**Status:** **Shipped (G11).** Non-blocking USART2 console TX/RX driven by a
register-level ISR. Code lives in [`App/uart-stream/uart_stream.{c,h}`](../../App/uart-stream/);
the vector is wired in `Core/Src/stm32g4xx_it.c` (`USART2_IRQHandler` →
`v_uart_stream_isr_for(USART2)`, inside USER CODE). `__io_putchar` / `__io_getchar`
route stdio through it (see AGENTS.md § *Critical Rules* on the `--wrap=fflush`
console-drain dependency).

This file is kept as a **design reference** — the ownership rules and the
register-ISR-vs-HAL rationale still govern any future UART that wants this treatment
(next likely consumer: the ANSI terminal piano UI).

**When to load:** User says *uart_stream*, *non-blocking console*, *USART2 ISR*,
*HAL out of IRQ path*, or *virtual piano TX*.

**Cross-refs:** [terminal-piano-and-player-notes.md](terminal-piano-and-player-notes.md) · [PROJECT.md](../PROJECT.md).

---

## Why a register ISR instead of HAL

The debug console (**USART2**, 921600 baud, ST-Link VCP) needs a **non-blocking TX
ring** with interrupt-driven drain for bursty ANSI redraws (piano UI, verbose PLAY
trace). **HAL UART IRQ processing is a poor fit** for an always-on streaming console:

- High overhead (state machine, callback indirection).
- On ORE/RTO and related error paths, **`HAL_UART_IRQHandler`** calls
  **`UART_EndRxTransfer`**, which **aborts reception and disables RX interrupts** —
  hostile to a console that should survive terminal glitches and keep running.
- Do **not** route owned UARTs through **`HAL_UART_IRQHandler`**, even as a "tail
  hook" after HAL runs.

**Design intent (as shipped):** `uart_stream` **owns interrupt processing directly**
for each **bound** instance — register-level ISR only. HAL is used for **init only**
(clocks, pins, baud, FIFO off); after bind, HAL must **not** touch `CR1` interrupt
enables or runtime state for that UART.

### ISR shape (`v_uart_stream_isr_for`)

1. For the bound instance: read `Instance->ISR`.
2. **RX:** drain `RDR` into RX ring while `RXNE`/`RXFNE` set.
3. **TX:** dequeue into `TDR` while `TXE`/`TXFNF` and queue non-empty.
4. **Errors:** clear `ORE/FE/NE/PE` via `ICR`, increment error counter — **do not
   disable the UART**.
5. **TXEIE:** auto-clear when the TX queue empties (stop interrupt noise).

Public API: init/deinit, non-blocking + blocking TX/RX helpers, queue-depth queries,
error count. Static queue buffers (caller-owned storage; no malloc on the bind path).

---

## G474 UART ownership (do not mix)

**Rule: one vector, one owner.** Only **USART2** is on the `uart_stream` register-ISR
path. The LED strip UARTs stay on the existing **HAL + DMA** path
(`App/Src/led_strip_control.c`, completion callbacks in `App/Src/app_main.c`) — do
**not** add `uart_stream` to those vectors, and do **not** enable their USART global
IRQs (see AGENTS.md § *LED Driver*).

| Peripheral | Role | Owner | IRQ path |
|------------|------|-------|----------|
| **USART2** | Debug console @ 921600 | **`uart_stream`** | Register ISR → `v_uart_stream_isr_for(USART2)` |
| **USART1** | LED strip [1] WS2812 | `led_strip_control` | HAL + DMA; completion in `stm32g4xx_it.c` |
| **USART3** | LED strip | `led_strip_control` | HAL + DMA |
| **UART4**  | LED strip | `led_strip_control` | HAL + DMA |
| **UART5**  | LED strip | `led_strip_control` | HAL + DMA |
| **LPUART1**| LED strip | `led_strip_control` | HAL + DMA |

---

## Adding another `uart_stream` instance later (checklist)

If a future feature binds a second UART (e.g. an ESP32 coprocessor link):

- [ ] Bind via `uart_stream` init (HAL init-only; enable `RXNEIE`/`TXEIE` or the G4
      FIFO equivalents **inside** `uart_stream`, not via `HAL_UART_*_IT`).
- [ ] Add **only** that UART's `*_IRQHandler` in `stm32g4xx_it.c` USER CODE, forwarding
      to `v_uart_stream_isr_for()`.
- [ ] Leave LED strip UARTs untouched (DMA TX only).
- [ ] No `HAL_UART_IRQHandler()` on the owned path.
- [ ] Build + `/smoke` after IRQ wiring (the USART2 banner must still print cleanly).

---

## Downstream consumers

| Consumer | Needs `uart_stream`? |
|----------|----------------------|
| ANSI terminal piano UI | **Yes** — bursty ANSI diffs on NOTE on/off |
| PLAY **I8** verbose trace | Optional — short log lines; can stay blocking |
| Debug menu / logging | Already on the stdio path via `__io_putchar`/`__io_getchar` |

See [terminal-piano-and-player-notes.md](terminal-piano-and-player-notes.md).

---

*Original port plan captured 2026-06-11; shipped as G11. Update this file if UART
ownership rules change.*
