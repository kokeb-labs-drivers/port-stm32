# Clock, delay and log

## Clock (`MonotonicClock`)

`kb::port::stm32::Clock` is a 32-bit timer (TIM2 or TIM5, both 32-bit on every supported family) counting at
1 MHz, extended to 64 bits by its update interrupt.

| Call | What |
| --- | --- |
| `Start()` | Once: the timer's clock, `HAL_TIM_Base_Init` (prescaler for 1 MHz from the timer clock, auto-reload 0xFFFFFFFF), the update interrupt at `irq_priority` (default 1: high), `HAL_TIM_Base_Start`. Later calls, and other `Clock` objects on the same timer, return `Ok` |
| `Now()` | The wrap count, the counter, the wrap count again and the pending update flag, combined (`Combine()`) so the result never goes back, even inside a critical section where the interrupt cannot run |
| `HandleClockIrq(timer)` | From `TIM2_IRQHandler` / `TIM5_IRQHandler`: clears the flag, counts one wrap (every 71.6 minutes) |

Before `Start()`, `Now()` returns 0. The timer clock is `timer_clock_hz`, or the APB1 timer clock (doubled behind an
APB prescaler); a clock that is not a multiple of 1 MHz runs the clock slightly fast or slow (the prescaler is
rounded to nearest).

## BusyWait (`BusyWait`)

`kb::port::stm32::BusyWait` spins on the Cortex-M DWT cycle counter (`DWT->CYCCNT`, enabled with `TRCENA` on first
use) for at least the asked microseconds at `SystemCoreClock`, rounded up. Waits longer than 2^30 cycles are split,
so the 32-bit counter never wraps within one step. Interrupts lengthen a wait, never shorten it. Keep
`SystemCoreClock` current (`SystemCoreClockUpdate()` after a clock change).

## Log sinks (`LogSink`)

| Sink | Where | When nothing listens |
| --- | --- | --- |
| `ItmLogSink` | ITM stimulus port 0, read through SWO by a debug probe | The line is dropped at once when the ITM or port 0 is not enabled (no probe); each character waits at most 10000 polls for the port |
| `UartLogSink` | A port `Uart` (`Write()`) | The line is dropped when the `Uart` refuses it; its `write_timeout` bounds the call |

Both write `<letter> text\r\n` (`E`, `W`, `I`, `D`) per call, cut at 160 characters (`kLogMaxLine`), from a stack
buffer: no heap, no `printf`.
