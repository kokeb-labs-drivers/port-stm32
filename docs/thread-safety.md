# Thread safety

The port follows kb-core's thread models (kb-core `docs/design/handlers.md`): a bus is either owned by one task
(model O) or shared through a guarded facade (model G). The port is safe under both, because every shared
resource has its own bounded lock, a `kb::rtos::Mutex` taken with `TryLockFor(timeout)`.

| Object | Lock | Held for | When not had in time |
| --- | --- | --- | --- |
| `I2cController` | One per controller | One transfer (plus a probe and a recovery after a failure) | `Busy`, nothing sent |
| `SpiBus` | One per instance, shared by its devices | One frame (all its chunks) or one device change | `Busy`, nothing clocked |
| `Uart` | One for the reader, one for the writer | One `Read`, one `Write`; `SetBaud` takes both | `Busy` |
| `AdcChannel` | One per ADC instance, shared by its channels | One conversion | `Busy` |
| `CanController` | One per controller | One fill-level check and one HAL call per poll; the whole wait of `Recover` | `Busy` |
| `OutputPin`, `InputPin` | None | One register access (BSRR, IDR); `Toggle` masks interrupts for its read-modify-write | |
| `PwmChannel` | None | Register writes; one object per channel, used from one context | |
| `Clock`, `BusyWait`, log sinks | None | `Clock::Now` is lock-free and interrupt-safe; `UartLogSink` uses the `Uart`'s writer lock | |

Rules:

- **Create, then share.** `EnsureReady()` creates the object's lock; call it once from one context before the object
  is used by others. Later calls are idempotent and cheap.
- **No lock across calls.** A lock is never held when a call returns, so no caller can block another longer than
  one transfer plus its timeout.
- **Interrupts never take a lock.** The port's handlers (UART receive, EXTI, the clock's wrap) write only
  single-producer data (the receive ring, latched flags, the wrap counter); the GPIO callback runs in interrupt
  context and may only use `kb::rtos` `...FromIsr` calls.
- **Under a model G facade** the handler's lock is taken first, then the port's lock (kb-core's fixed order); the
  port's lock is the inner one and never waits for a handler.

With `KB_RTOS=NONE` (bare metal, and the host tests) the locks still exist: a call made while the lock is held
(from an interrupt, or nested) is refused with `Busy` after its timeout instead of corrupting the transfer, which
the host tests use to prove every lock path. With `KB_RTOS=FREERTOS` they are FreeRTOS mutexes.
