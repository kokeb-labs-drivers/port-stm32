# SPI

`kb::port::stm32::SpiBus` is one SPI peripheral as master; `kb::port::stm32::SpiDevice` is one chip select on it
and satisfies `kb::iface::SpiDevice` and `kb::iface::SpiClockControl`. HAL `stm32<family>xx_hal_spi`, blocking
calls, 8-bit frames, MSB first.

## How it works

| Step | HAL call |
| --- | --- |
| `SpiBus::EnsureReady()` | The instance clock, SCK, MOSI and MISO as alternate function (MOSI or MISO may be unused), `HAL_SPI_Init` in mode 0 at the slowest prescaler (/256); the first frame sets its device's mode and clock |
| `SpiDevice::EnsureReady()` | The bus's `EnsureReady()`, the chip select as push-pull GPIO driven high |
| `Transfer(tx, rx)` | The bus lock; when another device used the bus last, CPOL, CPHA and the baud-rate prescaler are set again (`HAL_SPI_DeInit`, `HAL_SPI_Init`); CS low; `HAL_SPI_TransmitReceive` (or `HAL_SPI_Transmit` / `HAL_SPI_Receive` for a write-only or read-only frame) in chunks of at most 65535 bytes; CS high, whatever the result |
| After a HAL timeout | `HAL_SPI_Abort`; the peripheral is initialised again before the next frame (`Stats().rebuilds`) |
| `SetClockHz(hz)` | Recomputes the prescaler; applied at the next frame |
| `SpiDevice::Release()` | The chip select pin back to its reset state and unclaimed |
| `SpiBus::Release()` | `HAL_SPI_DeInit` (release the devices first) |

Chip select is a GPIO, not the peripheral's NSS: any pin can be a chip select, and CS stays low across chunks.

## Pins and clock

| Item | Note |
| --- | --- |
| SCK, MOSI, MISO | `PinId` with the SPI alternate function (AF5 or AF6 on most pins). Push-pull, very high speed |
| CS | Any GPIO, push-pull, active low; one per `SpiDevice` |
| Clock | `clock_hz` is the highest SCK wanted; the prescaler (2, 4, ... 256) rounds down: the SCK is the fastest at or below it. `InvalidArgument` when even /256 is too fast |
| Kernel clock | `kernel_clock_hz`, or 0: the instance's APB clock (SPI2 and SPI3 on APB1, the others on APB2), and on H7 the SPI kernel clock from the RCC |
| Modes | 0 to 3 (CPOL, CPHA) per device |

## Timing limits

| Wait | Bound |
| --- | --- |
| The bus lock | `SpiDeviceConfig::timeout` (default 20 ms); `Busy` when not had, nothing clocked |
| One chunk (65535 bytes or less) | `SpiDeviceConfig::timeout` as the HAL's `Timeout` |
| A device change | One `HAL_SPI_Init` |

## Results

| Result | When |
| --- | --- |
| `Ok` | The frame was clocked |
| `InvalidArgument` | Empty spans, `tx` and `rx` of different lengths, a clock out of the prescaler's reach, a bad configuration |
| `Busy` | The bus lock was not had in time; or the CS pin or the instance is owned by another object |
| `Timeout` | `HAL_TIMEOUT`: the frame was aborted and the peripheral will be initialised again |
| `BusError` | `HAL_ERROR` with `HAL_SPI_ERROR_MODF`, `_OVR`, `_CRC` or `_FRE` in `ErrorCode` |
| `NotReady` | Before `EnsureReady()` |

## Example

```cpp
static stm::SpiBus spi{bus_cfg};
stm::SpiDeviceConfig dc{};
dc.cs = stm::PinId{stm::GpioPort::B, 6U, 0U};
dc.mode = 3U;
dc.clock_hz = 4000000U;
static stm::SpiDevice tmc{spi, dc};
std::array<uint8_t, 5> tx{0x00U, 0U, 0U, 0U, 0U};
std::array<uint8_t, 5> rx{};
const kb::Status s = tmc.Transfer(tx, rx);
```
