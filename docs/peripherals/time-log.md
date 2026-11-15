# Clock, delay and log

## Clock (`MonotonicClock`)

`kb::port::stm32::Clock` is a 32-bit timer (TIM2 or TIM5, both 32-bit on every supported family) counting at
1 MHz, extended to 64 bits by its update interrupt.

| Call | What |
| --- | --- |
| `Start()` | Once: the timer's clock, `HAL_TIM_Base_Init` (prescaler for 1 MHz from the timer clock, auto-reload 0xFFFFFFFF), the counter set to 0 (a timer keeps counting across a jump from a loader), the update interrupt at `irq_priority` (default 1: high), `HAL_TIM_Base_Start`. The timer is then the clock's: a `PwmChannel` on it is refused (`Busy`, port code `kOwned`). Later calls, and other `Clock` objects on the same timer, return `Ok`; a timer clock of 0 Hz is refused (`NotReady`) |
| `Now()` | One `ClockSample`, combined by `Combine()` (below) |
| `HandleClockIrq(timer)` | From `TIM2_IRQHandler` / `TIM5_IRQHandler`, with interrupts masked: clears the update flag and counts one wrap (every 71.6 minutes) |

Before `Start()`, `Now()` returns 0. The timer clock is `timer_clock_hz`, or the APB1 timer clock (doubled behind an
APB prescaler, or HCLK with TIMPRE set where the family has it); a clock that is not a multiple of 1 MHz runs the
clock slightly fast or slow (the prescaler is rounded to nearest).

### Why `Now()` never goes back

A sample reads, in this order: the wrap count `h1`, the counter `c1`, the update flag `p`, the counter `c2`, the
wrap count `h2`. The wrap interrupt clears the flag and counts the wrap with interrupts masked, so any reader sees
both done or neither; the timer sets the flag on the same update event that reloads the counter.

- `h1 != h2`: the interrupt ran inside the sample, so it is not used; a new sample is taken. (After three such
  samples, which needs the reader to be held off for a whole wrap period each time, one sample is taken with
  interrupts masked, where the interrupt cannot run.)
- `h1 == h2` and `p` set: a wrap happened before the flag read and is not counted in `h1`; `c2`, read after the
  flag, is after that wrap. The count is `(h1 + 1, c2)`.
- `h1 == h2` and `p` clear: no uncounted wrap happened up to the flag read (the flag would still be set: the
  interrupt did not run), so none before `c1`. The count is `(h1, c1)`.

Each result is the true count at one of the sample's counter reads, so a later call returns a later count. The host
tests check this against a model of the timer over every interleaving of the five reads with ticks and the wrap
interrupt near a wrap (`tests/test_clock_read.cpp`), including the sequence that made the earlier rule (high,
counter, high, flag) step forward by 2^32 us and back.

The clock is the port's time source for every wait ([How the port waits](../waits.md)). The design follows a rule
observed with an earlier implementation on STM32H7 hardware: a 32-bit counter divided to microseconds wraps and
breaks every elapsed-time computation unless it is extended to 64 bits first, and a timer counter keeps running
across a jump from a bootloader, so it is zeroed when the clock starts.

## BusyWait (`BusyWait`)

`kb::port::stm32::BusyWait` spins on the Cortex-M DWT cycle counter (`DWT->CYCCNT`, enabled with `TRCENA` on first
use, unlocked with `LAR` on the Cortex-M7, never written) for at least the asked microseconds at `SystemCoreClock`,
rounded up. Waits longer than 2^30 cycles are split,
so the 32-bit counter never wraps within one step. Interrupts lengthen a wait, never shorten it. Keep
`SystemCoreClock` current (`SystemCoreClockUpdate()` after a clock change).

## Log sinks (`LogSink`)

| Sink | Where | When nothing listens |
| --- | --- | --- |
| `ItmLogSink` | ITM stimulus port 0, read through SWO by a debug probe | The line is dropped at once when the ITM or port 0 is not enabled (no probe); each character waits at most 10000 polls for the port |
| `UartLogSink` | A port `Uart` (`Write()`) | The line is dropped when the `Uart` refuses it; its `write_timeout` bounds the call |

Both write `<letter> text\r\n` (`E`, `W`, `I`, `D`) per call, cut at 160 characters (`kLogMaxLine`), from a stack
buffer: no heap, no `printf`.
