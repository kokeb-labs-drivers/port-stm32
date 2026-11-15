# SPI

`kb::port::stm32::SpiBus` is one SPI peripheral as master; `kb::port::stm32::SpiDevice` is one chip select on it
and satisfies `kb::iface::SpiDevice` and `kb::iface::SpiClockControl`. HAL `stm32<family>xx_hal_spi`,
interrupt-driven transfers waited for on the port's clock ([How the port waits](../waits.md)), 8-bit frames, MSB
first.

## How it works

| Step | HAL call |
| --- | --- |
| `SpiBus::EnsureReady()` | The kernel clock checked (0 Hz: `NotReady`); the instance clock, SCK, MOSI and MISO as alternate function (MOSI or MISO may be unused), `HAL_SPI_Init` in mode 0 at the slowest prescaler (/256), the instance's interrupt at `irq_priority` (default 5); the first frame sets its device's mode and clock |
| `SpiDevice::EnsureReady()` | The bus's `EnsureReady()`, the chip select as push-pull GPIO driven high |
| `Transfer(tx, rx)` | The bus lock; when another device used the bus last, CPOL, CPHA and the baud-rate prescaler are set again (`HAL_SPI_DeInit`, `HAL_SPI_Init`: the peripheral is disabled while it is reprogrammed); CS low; `HAL_SPI_TransmitReceive_IT` (or `HAL_SPI_Transmit_IT` for a write-only frame; a read-only frame clocks its zeroed receive buffer out in place with `HAL_SPI_TransmitReceive_IT`, on every family) in chunks of at most 65535 bytes, each waited for until the HAL's completion callback; CS high, whatever the result |
| After a timeout | `HAL_SPI_Abort`; the peripheral is initialised again before the next frame (`Stats().rebuilds`) |
| `SetClockHz(hz)` | Recomputes the prescaler; applied at the next frame |
| `SpiDevice::Release()` | The chip select pin back to its reset state and unclaimed |
| `SpiBus::Release()` | `HAL_SPI_DeInit` (release the devices first) |

Chip select is a GPIO, not the peripheral's NSS: any pin can be a chip select, and CS stays low across chunks. Its
idle level is written before the pin becomes an output.

While the peripheral is disabled for a mode change, SCK is not driven on F4, G4 and L4 (H7 keeps it at its idle
level with `MasterKeepIOState`); every CS is high then, so no device sees a clock, but a pull to the CPOL level on
SCK keeps the line quiet. The inter-data idleness (H7 MIDI) is the HAL default, none, for every device: observed
with an earlier implementation on STM32H7 hardware, a gap inside a 32-bit word desynchronised a slave, so a device
that needs one is not served yet.

## Pins and clock

| Item | Note |
| --- | --- |
| SCK, MOSI, MISO | `PinId` with the SPI alternate function (AF5 or AF6 on most pins). Push-pull, very high speed |
| CS | Any GPIO, push-pull, active low; one per `SpiDevice` |
| Clock | `clock_hz` is the highest SCK wanted; the prescaler (2, 4, ... 256) rounds down: the SCK is the fastest at or below both `clock_hz` and the bus's interrupt ceiling (below), so it may be lower than asked. `ActualClockHz()` (and `Stats().actual_clock_hz`) is the SCK used; `Stats().clamps` counts lowered selections. `InvalidArgument` when even /256 is above either |
| Kernel clock | `kernel_clock_hz`, or 0: the instance's APB clock (SPI2 and SPI3 on APB1, the others on APB2), and on H7 the SPI kernel clock from the RCC |
| Modes | 0 to 3 (CPOL, CPHA) per device |

## The interrupt ceiling

Transfers run by the HAL's interrupt handler, which runs once for each transmit-empty and each receive-not-empty
event: about 300 core cycles per byte with exception entry and exit. Without a receive FIFO (F4) a received byte
must be read within one byte time, or the next one overruns it (`HAL_SPI_ERROR_OVR`, reported as `BusError`); with a
FIFO (G4 and L4: 4 bytes, H7: 16) the handler has a few bytes of slack but must keep up on average. The SCK of every
interrupt transfer is therefore limited to:

| Family | Ceiling (`SpiInterruptCeilingHz`) | Example | SCK used (prescaler at or below the ceiling) |
| --- | --- | --- | --- |
| F4 (no FIFO) | core clock / 64 (512 cycles per byte: the handler plus room for other interrupts) | 168 MHz core, SPI1 on an 84 MHz APB2: ceiling 2.625 MHz | 84 MHz / 32 = 2.625 MHz |
| G4, L4 (4-byte FIFO) | core clock / 40 (320 cycles per byte) | 170 MHz core and kernel: ceiling 4.25 MHz | 170 MHz / 64 = 2.66 MHz |
| | | 80 MHz core and kernel (L4): ceiling 2 MHz | 80 MHz / 64 = 1.25 MHz |
| H7 (FIFO: 16 bytes on SPI1 to SPI3, 8 on SPI4 to SPI6) | core clock / 40 | 480 MHz core, 200 MHz SPI kernel clock: ceiling 12 MHz | 200 MHz / 32 = 6.25 MHz |

The bus computes the ceiling from `SystemCoreClock` at `EnsureReady()`. A device asking for more runs at the fastest
prescaler at or below the ceiling: **the SCK may be lower than `clock_hz`**. `Config().clock_hz` keeps what was
asked; `ActualClockHz()` and `Stats().actual_clock_hz` give the SCK in use, and `Stats().clamps` counts the
selections that were lowered. A driver or application that paces work from the SPI clock (a converter whose readout
must finish within one conversion period, for example) reads `ActualClockHz()` after `EnsureReady()` and
`SetClockHz()`, not the configured value.

With the port's own interrupts a frame is not expected to overrun at the ceiling. A long critical section (FreeRTOS
masks the SPI interrupt at the syscall limit in every critical section, and the port's own interrupt-masked
sections), or a higher-priority interrupt, can still delay the handler past one byte time on F4 and cause an overrun,
reported as `BusError` (`HAL_SPI_ERROR_OVR`). The ceilings are estimates from the pinned HAL's handler structure;
**a bench measurement per family is an open verification item (not yet done)**. At the ceiling the handler takes
most of the CPU for the frame's duration (at the SPI interrupt's priority): long frames are cheaper on a slower SCK,
or with DMA.

Not implemented: DMA, and a polled path above the ceiling. Observed with an earlier implementation on STM32H7
hardware, SPI by circular DMA into non-cacheable SRAM ran at about 4.8 % of a 240 MHz core against about 28 % for the
polled path, with zero errors; DMA is the way past the ceiling and comes with the H7 cache rules of
[Integration](../integration.md). A polled register loop for short frames would busy-wait at the caller's priority,
which the port's wait rule excludes under an RTOS ([How the port waits](../waits.md)), so it is not offered.

## Timing limits

| Wait | Bound |
| --- | --- |
| The bus lock | `SpiDeviceConfig::timeout` (default 20 ms, at most 2 s); `Busy` when not had, nothing clocked |
| One chunk (65535 bytes or less) | `SpiDeviceConfig::timeout` on the port's clock; the abort after a timeout is a counted wait in the HAL |
| A device change | One `HAL_SPI_Init` |

## Results

| Result | When |
| --- | --- |
| `Ok` | The frame was clocked |
| `InvalidArgument` | Empty spans, `tx` and `rx` of different lengths, a clock out of the prescaler's reach, a bad configuration |
| `Busy` | The bus lock was not had in time; or the CS pin or the instance is owned by another object |
| `Timeout` | The transfer did not end in time: it was aborted and the peripheral will be initialised again |
| `BusError` | The error callback with `HAL_SPI_ERROR_MODF`, `_OVR`, `_CRC` or `_FRE` in `ErrorCode` |
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
