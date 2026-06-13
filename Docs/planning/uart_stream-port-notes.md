# `uart_stream` port notes (G474)

**Status:** Design captured — **not implemented**. **v1.1 stretch goal** (user lock 2026-06-13) — not required for PLAY v1/v1.1 grammar ship. Read this before integrating non-blocking debug-UART TX (piano UI, verbose PLAY trace, logging migration).

**When to load:** User says *uart_stream*, *non-blocking console*, *USART2 ISR*, *HAL out of IRQ path*, or *virtual piano TX*.

**Cross-refs:** [terminal-piano-and-player-notes.md](terminal-piano-and-player-notes.md) · [PROJECT.md](../PROJECT.md) wishlist · reference drop `not-in-project/uart_stream.{c,h}` (gitignored — local only).

---

## Problem statement

The debug console (**USART2**, 921600 baud, ST-Link VCP) today uses blocking HAL/polling paths suitable for menus and logging. A **terminal piano UI** and bursty ANSI redraws need a **non-blocking TX ring** with interrupt-driven drain.

**HAL UART IRQ processing is a poor fit** for an always-on streaming console:

- High overhead (state machine, callback indirection).
- On ORE/RTO and related error paths, **`HAL_UART_IRQHandler`** calls **`UART_EndRxTransfer`**, which **aborts reception and disables RX interrupts** — hostile to a console that should survive terminal glitches and keep running.
- Do **not** route owned UARTs through **`HAL_UART_IRQHandler`**, even as a “tail hook” after HAL runs.

**Design intent:** **`uart_stream` owns interrupt processing directly** for each **bound** instance — register-level ISR only. HAL may be used for **init only** (clocks, pins, baud, FIFO off); after bind, HAL must **not** touch `CR1` interrupt enables or runtime state for that UART.

---

## Reference code (local only)

| Item | Location |
|------|----------|
| Author’s reference drop | `not-in-project/uart_stream.{c,h}` |
| Ring buffer primitive | `not-in-project/queue.{c,h}` (or project `App/Src/queue.c` if already ported) |

**Never commit** `not-in-project/` — gitignored by design.

The reference targets **STM32G0B0** headers in places; G474 port must use **`stm32g4xx.h`** / G4 register names (same USART v2 pattern: `ISR`, `RDR`, `TDR`, `ICR`, `CR1` TXE/RXNE enables).

### What the reference does well

`v_uart_stream_common_isr()` (reference) is the right **shape**:

1. For each **active** bound instance: read `Instance->ISR`.
2. **RX:** drain `RDR` into RX ring while `RXNE`/`RXFNE` set.
3. **TX:** dequeue into `TDR` while `TXE`/`TXFNF` and queue non-empty.
4. **Errors:** clear `ORE/FE/NE/PE` via `ICR`, increment error counter — **do not disable UART**.
5. **TXEIE:** auto-clear when TX queue empty (stop interrupt noise).

Public API: init/deinit, non-blocking + blocking TX/RX helpers, queue depth queries, error count.

### What the reference does wrong on G474

It installs **strong IRQ handlers for every UART** on that MCU family:

```c
void USART1_IRQHandler(void)       { v_uart_stream_common_isr(); }
void USART2_IRQHandler(void)       { v_uart_stream_common_isr(); }
void USART3_4_5_6_IRQHandler(void) { v_uart_stream_common_isr(); }
void LPUART1_IRQHandler(void)      { v_uart_stream_common_isr(); }
```

On this board that **collides** with **HAL+DMA LED strips** and is the **wrong scope** — only **USART2** is a `uart_stream` candidate today.

---

## G474 UART ownership (do not mix)

| Peripheral | Role today | Owner | IRQ path |
|------------|------------|-------|----------|
| **USART2** | Debug console @ 921600 | **`uart_stream`** (target) | Register ISR in `stm32g4xx_it.c` → instance handler |
| **USART1** | LED strip [1] WS2812 | `led_strip_control` | HAL + DMA (`DMA1_Ch1`); completion in `stm32g4xx_it.c` |
| **USART3** | LED strip | `led_strip_control` | HAL + DMA |
| **UART4** | LED strip | `led_strip_control` | HAL + DMA |
| **UART5** | LED strip | `led_strip_control` | HAL + DMA |
| **LPUART1** | LED strip | `led_strip_control` | HAL + DMA |

**Rule:** One vector, one owner. LED UARTs stay on existing **HAL+DMA** path (`App/Src/led_strip_control.c`, `App/Src/app_main.c` callbacks). No `uart_stream` on those vectors.

**Current state:** `Core/Src/stm32g4xx_it.c` has **DMA** IRQ handlers for LED channels; **no USART IRQ handlers** are defined yet (USART vectors still weak/default). That is compatible with adding **only** `USART2_IRQHandler` for `uart_stream`.

---

## Target integration model

### 1. Per-instance bind (not global vector takeover)

- `p_x_uart_stream_init(huart, rx_size, rx_buf, tx_size, tx_buf)` registers **one** slot in a small instance table.
- ISR scans **only bound, active** instances (reference loop pattern — keep it).
- **Selective vectors:** only `USART2_IRQHandler` (or whichever UARTs are explicitly bound) forwards to `v_uart_stream_isr()`.

### 2. HAL init-only

After `MX_USART2_UART_Init()`:

- Optionally call `p_x_uart_stream_init(&huart2, …)`.
- Enable `RXNEIE` / `TXEIE` (or G4 FIFO equivalents) **inside `uart_stream`**, not via `HAL_UART_Receive_IT` / `HAL_UART_Transmit_IT`.
- Do **not** call HAL UART IT/DMA APIs on that handle again at runtime.
- Some teams drop `UART_HandleTypeDef` after init and store only `USART_TypeDef *` + instance struct to make ownership obvious.

### 3. `stm32g4xx_it.c` (USER CODE)

```c
void USART2_IRQHandler(void)
{
    v_uart_stream_isr();   /* or v_uart_stream_isr_for(&huart2) */
}
```

Place in **`/* USER CODE BEGIN 1 */`** — do not hand-edit other generated regions without user approval.

### 4. Coexistence checklist

- [ ] Only **USART2** vector wired to `uart_stream`.
- [ ] LED strip UARTs unchanged (DMA TX only).
- [ ] No `HAL_UART_IRQHandler(&huart2)` on the owned path.
- [ ] Logging / menu code migrated incrementally — consider dual path during bring-up (blocking log until piano UI lands).
- [ ] Build + `/smoke` after IRQ wiring (banner on USART2 must still work).

---

## Port checklist (implementation session)

1. Copy/adapt `uart_stream.{c,h}` into **`App/`** (not from gitignored tree at link time — **copy** the adapted source into the repo).
2. Replace G0 includes with G4; verify `USART_ISR_*` / `USART_CR1_*` symbol names on G474.
3. Remove reference’s blanket `USART1/3/LPUART` IRQ stubs; add **USART2 only** in `stm32g4xx_it.c`.
4. Static queue buffers preferred on embedded path (reference allows `malloc` — evaluate against project norms; G474 PLAY path may prefer static TX/RX rings).
5. Wire init from `app_main.c` after `MX_USART2_UART_Init`.
6. Smoke: enqueue a burst (ANSI cursor home + line) without blocking main loop.
7. Document any logging migration plan separately — out of scope for first port.

---

## Suggested session opener

```
/read-the-docs uart_stream port — Docs/planning/uart_stream-port-notes.md
Reference: not-in-project/uart_stream.{c,h}. Target USART2 only; HAL init-only; register ISR.
```

---

## Related consumers (downstream)

| Consumer | Needs `uart_stream`? |
|----------|----------------------|
| ANSI terminal piano UI | **Yes** — bursty ANSI diffs on NOTE on/off |
| PLAY **I8** verbose trace | Optional — short log lines; can stay blocking initially |
| Debug menu / logging | Eventually — not required for first `uart_stream` bring-up |

See [terminal-piano-and-player-notes.md](terminal-piano-and-player-notes.md).

---

*Captured 2026-06-11 from planning discussion. Update this file when port lands or ownership rules change.*
