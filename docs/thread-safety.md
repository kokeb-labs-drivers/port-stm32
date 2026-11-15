# Thread safety

The port follows kb-core's thread models (kb-core `docs/design/handlers.md`): a bus is either owned by one task
(model O) or shared through a guarded facade (model G). The port is safe under both, because every shared
resource has its own bounded lock, a `kb::rtos::Mutex` taken with `TryLockFor(timeout)`.

| Object | Lock | Held for | When not had in time |
| --- | --- | --- | --- |
| `I2cController` | One per controller | One transfer (plus a probe with `nack_probe`, and a restore or recovery after a failure) | `Busy`, nothing sent |
| `SpiBus` | One per instance, shared by its devices | One frame (all its chunks) or one device change | `Busy`, nothing clocked |
| `Uart` | One for the reader, one for the writer | One `Read`, one `Write`; `SetBaud` takes both | `Busy` |
| `AdcChannel` | One per ADC instance, shared by its channels | One conversion | `Busy` |
| `CanController` | One for sending, one for receiving | One `Send` or one `Receive` with its wait; `Recover` takes the send lock | `Busy` |
| `OutputPin`, `InputPin` | None | One register access (BSRR, IDR); `Toggle` masks interrupts for its read-modify-write | |
| `PwmChannel` | None | Register writes; one object per channel, used from one context; timer ownership changes with interrupts masked | |
| `Clock`, `BusyWait`, log sinks | None | `Clock::Now` is lock-free and interrupt-safe; `UartLogSink` uses the `Uart`'s writer lock | |

Rules:

- **Create, then share.** `EnsureReady()` creates the object's lock; call it once from one context before the object
  is used by others. Later calls are idempotent and cheap.
- **No lock across calls.** A lock is never held when a call returns, so no caller can block another longer than
  one transfer plus its timeout.
- **Interrupts never take a lock.** The port's handlers and the HAL callbacks write only single-producer data (the
  receive rings, latched flags, the wrap counter, a `Completion`) and give a semaphore; the GPIO callback runs in
  interrupt context and may only use `kb::rtos` `...FromIsr` calls.
- **Shared tables change with interrupts masked.** Pin claims, the EXTI line table (callback and context are atomic,
  written with interrupts masked, so a handler always sees a callback with its own context), timer ownership and the
  ADC instances' channel counts are changed only inside a few-instruction section with interrupts masked (PRIMASK
  saved and restored), never a kb::rtos critical section (which, before the scheduler, could leave BASEPRI raised).

## LastError(), LastFailure() and Stats()

`LastError()` is the status and vendor code of the most recent call on the object **by any task**; `LastFailure()`
(I2C) is the failure kind of the most recent transfer; `Stats()` are the object's counters. All three are safe to
read from any task at any time: the record is written and read with interrupts masked (it never tears and is never
a data race), the failure kind is atomic, and the counters are updated and copied with interrupts masked.

They are not per task. Under the owner-task model (one task per bus, kb-core's default for real-time buses) the
owner reads its own call's detail. Under a shared model, another task's call may have replaced the record between a
call and the read of its detail: rely on the returned `kb::Status` there, and read `LastError()` only for
diagnostics.
- **Under a model G facade** the handler's lock is taken first, then the port's lock (kb-core's fixed order); the
  port's lock is the inner one and never waits for a handler.

With `KB_RTOS=NONE` (bare metal, and the host tests) the locks still exist: a call made while the lock is held
(from an interrupt, or nested) is refused with `Busy` after its timeout instead of corrupting the transfer, which
the host tests use to prove every lock path. With `KB_RTOS=FREERTOS` they are FreeRTOS mutexes.
