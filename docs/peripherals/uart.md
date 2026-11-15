# UART

`kb::port::stm32::Uart` is one USART, UART or LPUART1 and satisfies `kb::iface::UartPort`, `UartBaudControl` and
`UartTxDrain`. HAL `stm32<family>xx_hal_uart` for the setup; both directions run in the port's own interrupt
handler, and every wait is on the port's clock, asleep under an RTOS ([How the port waits](../waits.md)).

## How it works

| Step | What |
| --- | --- |
| `EnsureReady()` | Checks that the HAL time base runs, then the instance clock, TX and RX as alternate function (RX with pull-up), the DE pin as GPIO driven low, `HAL_UART_Init` (baud, 7 or 8 data bits, parity, 1 or 2 stop bits, no flow control, oversampling 16, FIFO off), the receive and error interrupts, the NVIC line at `irq_priority` |
| Receive | `HandleUartIrq(instance)` (from the vector, see [Interrupts](interrupts.md)) reads the flags and the data register; `OnInterrupt()` masks the word to the data bits (a 7-bit frame's parity bit never reaches the caller), puts the byte into a ring of 256 bytes, latches overrun, framing, parity and noise errors (the error flags are always cleared) and wakes a waiting `Read()` |
| `Read(rx, timeout)` | Waits at most `timeout` for the first byte, then takes whatever else is in the ring (up to `rx.size()`) |
| `Write(tx)` | Enables the transmit-empty interrupt; the handler sends byte by byte and ends the write at the last byte (with a DE pin: at transmit-complete, then DE low). Any length; the whole frame within `write_timeout` |
| `SetBaud(baud)` | Both locks, `HAL_UART_Init` again with the new rate; the ring is kept |
| `WaitTxDone(timeout)` | Done when transmit-complete is set; else waits for the transmit-complete interrupt |
| `FlushInput()` | Empties the ring and clears latched errors |

## Pins and clock

| Item | Note |
| --- | --- |
| TX, RX | `PinId` with the UART alternate function (AF7 for USART1 to USART3 on most parts, AF8 for UART4 and up; LPUART1 varies) |
| DE | RS-485 driver enable as a GPIO, high while sending; or unused |
| Instance | 1 to 8 for USART1 to UART8 where the device has them, `kLpuart1` (101) for LPUART1; UART9 and USART10 (H7) are not served |
| Baud | The HAL computes BRR from the instance's clock; the application keeps the clock tree consistent |
| Framing | 8N1, 8E1, 8O1 (9-bit words), 7E1, 7O1 (8-bit words); 7N1 on G4, H7 and L4 only (F4 has no 7-bit word: `Unsupported`) |
| FIFO (G4, H7, L4) | Off. Observed with an earlier implementation on STM32H7 hardware, at 921 600 Bd with the FIFO off any interrupt-masked stretch longer than one byte time (10.8 us) overran RX; at such rates use a higher interrupt priority and keep masked sections short |

## Timing limits

| Wait | Bound |
| --- | --- |
| The writer lock | `write_timeout` (default 100 ms, at most 2 s) |
| `Write()` | `write_timeout` for the whole frame (DE included), on the port's clock; on a timeout the transmit interrupts are stopped and DE is driven low |
| The reader lock | The `Read()` timeout |
| `Read()` | Its timeout (at most 2 s), on the port's clock; the task blocks under an RTOS until a byte or an error arrives |
| `WaitTxDone()` | Its timeout (at most 2 s) |

## Results

| Result | When |
| --- | --- |
| `Ok` | Done (`Read`: one byte or more) |
| `Timeout` | `Read`: nothing in time. `Write`: `HAL_TIMEOUT`; `WaitTxDone`: not complete in time |
| `Overflow` | `Read`: the receiver overran or the ring was full since the last `Read` (Detail: the `uart_flags` mask) |
| `BusError` | `Read`: framing, parity or noise error since the last `Read` (Detail: the `uart_flags` mask) |
| `Busy` | The reader or writer lock was not had in time; or the instance is owned by another object |
| `InvalidArgument` | Empty span, baud 0, a timeout above 2 s, a bad configuration |
| `Unsupported` | 7 data bits without parity on F4 |
| `NotReady` | Before `EnsureReady()`; or the HAL time base does not run (`kHalTickStopped`) |

`Release()` stops the interrupt and de-initialises the instance; the DE pin stays an output driven low (a floating
DE with a pull-up would enable the RS-485 driver and hold the bus).

An error is reported once, by the next `Read()`; the bytes after it are returned by the reads that follow. Bytes
received with a framing, parity or noise error are dropped.

## Example

```cpp
static stm::Uart uart{cfg};
std::array<uint8_t, 32> rx{};
const kb::Result<std::size_t> n = uart.Read(rx, kb::Milliseconds{20U});
```
