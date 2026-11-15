# port-stm32

kb-core's bus, pin, clock and log contracts (`kb::iface`) implemented over the **STM32Cube HAL** for the **STM32 G4,
H7, F4 and L4** families. Drivers written against the kb contracts run on an STM32 with no HAL code of their own.

| | |
| --- | --- |
| MCU families | STM32G4 (Cortex-M4F), STM32H7 (Cortex-M7F), STM32F4 (Cortex-M4F), STM32L4 (Cortex-M4F) |
| SDK | STM32Cube HAL and CMSIS of each family, fetched unchanged at pinned tags and commits |
| Namespace / CMake target | `kb::port::stm32` / `kb::port_stm32` (CMake project `kb_port_stm32`, consumed with `add_subdirectory`) |
| Include root | `kb/port/stm32/` (`#include "kb/port/stm32/port.hpp"`) |
| C++ | ISO C++20; no heap, no exceptions, no RTTI, no virtual functions |
| Assurance | Level high: MISRA C++:2023 (CodeQL) with records only for the HAL boundary files |

## Contracts

| kb contract | Type | HAL |
| --- | --- | --- |
| `I2cBus` | `I2cController` | `hal_i2c` interrupt API, TIMINGR computed (v2) or ClockSpeed (F4), handle restore and BUSY reset, stuck-bus recovery (nine clocks and a STOP) |
| `SpiDevice`, `SpiClockControl` | `SpiBus` + `SpiDevice` | `hal_spi` interrupt API, GPIO chip select held across chunks, abort and re-init after a timeout |
| `UartPort`, `UartBaudControl`, `UartTxDrain` | `Uart` | `hal_uart` setup, the port's own interrupt for both directions, a receive ring, RS-485 DE optional |
| `GpioOutput`; `GpioInput`, `GpioInterrupt` | `OutputPin`; `InputPin` | `hal_gpio`, EXTI through the port's handlers |
| `AdcChannel` | `AdcChannel` | `hal_adc`, single conversion ended by its interrupt, calibration on G4, H7, L4 |
| `PwmOutput` | `PwmChannel` | `hal_tim`, PWM mode 1 |
| `CanBus` | `CanController` | `hal_fdcan` (G4, H7, classic frames) or `hal_can` (F4, L4), receive interrupt into a ring, bus-off recovery |
| `MonotonicClock`, `BusyWait`, `LogSink` | `Clock`, `BusyWait`, `ItmLogSink` / `UartLogSink` | TIM2/TIM5 at 1 MHz extended to 64 bits, DWT cycle counter, ITM or a `Uart` |

Every type is `static_assert`ed against its concepts and its `*Ref`. Every wait is bounded by a timeout of at most
2 s, measured on the port's own clock and ended by an interrupt (asleep under FreeRTOS; [How the port waits](docs/waits.md));
every failure is a `kb::Status` with the HAL status and the handle's `ErrorCode` kept as the vendor code
(`LastError().Detail()`). What is not implemented, and why, is on the [home page](docs/index.md).

## Use

```cpp
#include "kb/port/stm32/port.hpp"
namespace stm = kb::port::stm32;

stm::I2cConfig cfg{};
cfg.instance = 1U;
cfg.scl = stm::PinId{stm::GpioPort::B, 8U, 4U}; // PB8, AF4
cfg.sda = stm::PinId{stm::GpioPort::B, 9U, 4U}; // PB9, AF4
static stm::I2cController bus{cfg};
if (bus.EnsureReady() == kb::Status::Ok) {
  const std::array<uint8_t, 1> reg{0x00U};
  std::array<uint8_t, 2> rx{};
  const kb::Status s = bus.WriteRead(0x4AU, reg, rx); // NoDevice, Nack, Timeout, BusError, Busy ...
}
```

`add_subdirectory()` the repository with `KB_PORT_STM32_DEVICE` (for example `STM32G474xx`) and
`KB_PORT_STM32_HAL_CONF_DIR` set, and link `kb::port_stm32` ([Getting started](docs/getting-started.md)).

## Examples

`examples/stm32` builds `all_drivers.elf` for STM32G474, STM32H743, STM32F446 and STM32L476: a self test of every
contract, and the pcal9555 (I2C), tmc5160 (SPI) and bno08x (I2C) drivers bound through the port
([Examples](docs/examples.md)).

## Build and test

```bash
git submodule update --init drivers                       # the example drivers
cmake -S . -B build -G Ninja -DKB_PORT_STM32_BUILD_TESTS=ON
cmake --build build && ctest --test-dir build             # host tests over HAL fakes, two family variants
tools/preflight.sh                                        # what CI checks, locally
cmake -S examples/stm32 -B build-g4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DEXAMPLE_BOARD=g4
cmake --build build-g4                                    # firmware against the pinned HAL
```

## Documentation

The site (`docs/`, `mkdocs build -f _config/mkdocs.yml --strict`) has one page per peripheral with the HAL calls,
pins and clocks, timing limits and results, plus the [families](docs/families.md),
[error mapping](docs/error-mapping.md), [thread safety](docs/thread-safety.md), [testing](docs/testing.md) and
[MISRA boundary](docs/deviations.md).

## Licence

MIT, copyright The port-stm32 authors ([LICENSE](LICENSE)). The fetched STM32Cube HAL and CMSIS files keep their
own licences (BSD-3-Clause, Apache-2.0). Contact: <kokeblabs@gmail.com>.
