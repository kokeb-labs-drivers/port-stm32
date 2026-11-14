# UART

`kb::port::stm32::Uart` is one USART, UART or LPUART1 and satisfies `kb::iface::UartPort`, `UartBaudControl` and
`UartTxDrain`. HAL `stm32<family>xx_hal_uart` for the setup and the transmit; the receive path is the port's own
interrupt handler.

## How it works

| Step | What |
| --- | --- |
| `EnsureReady()` | The instance clock, TX and RX as alternate function (RX with pull-up), the DE pin as GPIO driven low, `HAL_UART_Init` (baud, 7 or 8 data bits, parity, 1 or 2 stop bits, no flow control, oversampling 16), the receive and error interrupts, the NVIC line at `irq_priority` |
| Receive | `HandleUartIrq(instance)` (from the vector, see [Interrupts](interrupts.md)) reads each byte into a ring of 256 bytes and latches overrun, framing, parity and noise errors; it does not call `HAL_UART_IRQHandler` or any HAL callback |
| `Read(rx, timeout)` | Waits at most `timeout` for the first byte, then takes whatever else is in the ring (up to `rx.size()`) |
| `Write(tx)` | `HAL_UART_Transmit` (polling) in chunks of at most 65535 bytes; with a DE pin, DE high before and low after the transmission-complete flag |
| `SetBaud(baud)` | Both locks, `HAL_UART_Init` again with the new rate; the ring is kept |
| `WaitTxDone(timeout)` | Polls the transmission-complete flag |
| `FlushInput()` | Empties the ring and clears latched errors |

## Pins and clock

| Item | Note |
| --- | --- |
| TX, RX | `PinId` with the UART alternate function (AF7 for USART1 to USART3 on most parts, AF8 for UART4 and up; LPUART1 varies) |
| DE | RS-485 driver enable as a GPIO, high while sending; or unused |
| Instance | 1 to 8 for USART1 to UART8 where the device has them, `kLpuart1` (101) for LPUART1; UART9 and USART10 (H7) are not served |
| Baud | The HAL computes BRR from the instance's clock; the application keeps the clock tree consistent |

## Timing limits

| Wait | Bound |
| --- | --- |
| The writer lock | `write_timeout` (default 100 ms) |
| One write chunk | `write_timeout` as the HAL's `Timeout` |
| DE release after a write | At most one more `write_timeout` for the last stop bit |
| The reader lock | The `Read()` timeout |
| `Read()` | Its timeout, counted in `HAL_GetTick()` milliseconds, yielding through `kb::rtos::Yield()` while it waits |

## Results

| Result | When |
| --- | --- |
| `Ok` | Done (`Read`: one byte or more) |
| `Timeout` | `Read`: nothing in time. `Write`: `HAL_TIMEOUT`; `WaitTxDone`: not complete in time |
| `Overflow` | `Read`: the receiver overran or the ring was full since the last `Read` (Detail: the `uart_flags` mask) |
| `BusError` | `Read`: framing, parity or noise error since the last `Read` (Detail: the `uart_flags` mask) |
| `Busy` | The reader or writer lock was not had in time; or the instance is owned by another object |
| `InvalidArgument` | Empty span, baud 0, a bad configuration |
| `NotReady` | Before `EnsureReady()` |

An error is reported once, by the next `Read()`; the bytes after it are returned by the reads that follow. Bytes
received with a framing, parity or noise error are dropped.

## Example

```cpp
static stm::Uart uart{cfg};
std::array<uint8_t, 32> rx{};
const kb::Result<std::size_t> n = uart.Read(rx, kb::Milliseconds{20U});
```
