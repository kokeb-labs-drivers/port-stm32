# How the port waits

Every call that waits for hardware is bounded, measured on the port's own clock, and asleep under an RTOS. No wait
of the port reads `HAL_GetTick()`, and none spins the CPU at the caller's priority.

## The rule

| Wait | Started by | Ended by | Bounded by |
| --- | --- | --- | --- |
| I2C transfer | `HAL_I2C_Master_Seq_Transmit_IT` / `_Seq_Receive_IT` / `_Master_Transmit_IT` (probe) | The HAL's completion or error callback | `I2cConfig::timeout` |
| SPI chunk | `HAL_SPI_TransmitReceive_IT` / `HAL_SPI_Transmit_IT` | The HAL's completion or error callback | `SpiDeviceConfig::timeout` per chunk |
| UART write | The transmit-empty interrupt (the port's own handler) | The last byte handed over, or transmit-complete with RS-485 | `UartConfig::write_timeout` |
| UART read | — | The receive interrupt (a byte or an error) | The `Read()` timeout |
| `WaitTxDone()` | The transmit-complete interrupt | Transmit-complete | Its timeout |
| ADC conversion | `HAL_ADC_Start_IT` | `HAL_ADC_ConvCpltCallback` or `HAL_ADC_ErrorCallback` | `AdcConfig::conversion_timeout` |
| CAN receive | — | The RX FIFO 0 interrupt (into the port's ring) | The `Receive()` timeout |
| CAN send (no free slot) | — | The transmit-complete interrupt | The `Send()` timeout |
| CAN bus-off recovery | `Recover()` | The controller back on the bus (checked after each pause) | The `Recover()` timeout |
| Locks | — | `kb::rtos::Mutex` | The object's lock timeout |

Every timeout, configured or per call, is at most `kMaxTimeout` (2 s); a longer one, and `HAL_MAX_DELAY`, is refused
with `InvalidArgument`. 2 s leaves room for a secure element that stretches SCL for several hundred milliseconds,
and is short enough that a failing bus cannot hold callers for long. A caller that must wait longer loops.

## Deadline and Completion

`Deadline` (`completion.hpp`) measures a budget on the port's `Clock` (TIM2 or TIM5) once one has been started, and
on the DWT cycle counter (extended to 64 bits before any division, never written) before that. A second,
independent bound ends a wait even if that time source stops (an application that re-initialises or gates the
clock's timer, for example):

- under FreeRTOS (`KB_RTOS=FREERTOS`, CMake defines `KB_PORT_STM32_RTOS_BLOCKS`), the time slept on the RTOS tick:
  each slice that ended by its timeout lasted at least the slice, so the deadline expires after budget +
  `kWaitSliceMs` of slept slices (a 50 ms wait at most 60 ms of RTOS time);
- under kb-core's `none` backend, where a take cannot sleep, the poll count: (budget in ms + 1) x
  `kDeadlinePollsPerMs` polls.

`Completion` is armed by the task, completed from the interrupt (or the HAL callback) with the vendor code, and
waited for in slices of at most `kWaitSliceMs` (10 ms) on a `kb::rtos::Semaphore`:

- **FreeRTOS** (`KB_RTOS=FREERTOS`): the task blocks in the semaphore take until the interrupt gives it or the slice
  ends. It uses no CPU while it waits, and lower-priority tasks run.
- **`none` backend** (`KB_RTOS=NONE`, bare metal): kb-core's `none` backend has no scheduler; its take cannot block
  and returns at once (its `SleepFor` does not sleep on silicon either: its tick is a fake counter). The wait becomes
  a poll of the completion flag, bounded by the `Deadline` on the port clock or the DWT and by the poll cap. That is
  the documented fallback: correct and bounded, but it occupies the CPU for the wait, as any bare-metal wait does.

On a timeout the operation is stopped before the call returns: an I2C transfer has its handle restored (and the
peripheral reset if BUSY stays set), an SPI transfer is aborted (`HAL_SPI_Abort`, a counted wait in the HAL) and the
peripheral initialised again before the next frame, an ADC conversion is stopped (`HAL_ADC_Stop_IT`), a UART write
has its transmit interrupts disabled.

## What still depends on the HAL tick

The HAL's own initialisation and stop paths wait on `HAL_GetTick()` for single hardware acknowledgements that
take microseconds on healthy silicon: `HAL_UART_Init` (TEACK/REACK), `HAL_ADC_Start_IT`'s first enable (ADRDY) and
`HAL_ADC_Stop_IT`, `HAL_FDCAN_Init`/`HAL_CAN_Init` and their start and stop (INIT acknowledge). The port does not
replace the HAL there; instead:

- `Uart`, `CanController` and `AdcChannel` refuse `EnsureReady()` with `NotReady` (port code `kHalTickStopped`)
  when the HAL time base does not advance within two HAL tick periods (`HAL_GetTickFreq()`) and at least 25 ms,
  measured on the DWT, so a HAL tick advanced at 100 Hz (from an RTOS tick hook) is accepted; `Uart::SetBaud()` and
  the FDCAN restart in `CanController::Recover()` check it again before their HAL calls;
- call `EnsureReady()`, `Release()` and `SetBaud()` from task context, never with interrupts masked, never inside
  a critical section, and never from an interrupt;
- under an RTOS, give the HAL its time base from a source that runs whenever tasks run (see
  [Getting started](getting-started.md)).

The I2C and SPI initialisations and every transfer do not use the HAL tick.

Two waits spin the CPU at the caller's priority by design, both bounded and outside the transfers: the HAL's
I2C v1 (F4) busy-flag wait at the start of a transfer (a counted loop, about 25 ms of iterations, only when BUSY is
set), and the HAL time-base check of `EnsureReady()` (ends at the first tick, at most 25 ms or two tick periods).

## Later: kb-core's asynchronous Start/Poll contract

kb-core is adding an asynchronous `Start/Poll` contract for hot paths (its design, section 16: "a non-blocking form"
for bus calls). The port is built so that it maps onto it without a new HAL path:

- every interface call is already split into a start (`sdk::I2cStartTransmit`, `sdk::SpiStart`, `sdk::AdcStart`,
  the transmit-interrupt enable) and an end signalled by an interrupt into a `Completion`;
- `Start()` would arm the `Completion` and call the same start; `Poll()` would return `Done()` and, when done, the
  result from `Code()` (and `Value()` for the ADC), without waiting; a timeout would be the caller's deadline on the
  same port clock, and the same stop-and-restore runs when the caller gives up;
- the blocking calls stay, written as `Start()` followed by `Completion::Wait()`.

The change is then local to the platform-free files (`i2c_controller.cpp`, `spi_device.cpp`, `adc_pwm.cpp`,
`uart.cpp`, `can.cpp`); the boundary files and the HAL callbacks stay as they are.
