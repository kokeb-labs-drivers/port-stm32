# Getting started

## 1. Add the port to a CMake firmware project

The port is a CMake project; `add_subdirectory` it from the application's `CMakeLists.txt` with the device and the
folder of the application's `stm32<family>xx_hal_conf.h`. The HAL, CMSIS device and CMSIS-Core are fetched at their
pinned commits; the startup file of the device is handed back in `KB_PORT_STM32_STARTUP`.

```cmake
cmake_minimum_required(VERSION 3.25)
project(app C CXX ASM)
set(KB_PORT_STM32_DEVICE STM32G474xx CACHE STRING "")
set(KB_PORT_STM32_HAL_CONF_DIR "${CMAKE_CURRENT_LIST_DIR}/board" CACHE PATH "")
add_subdirectory(third_party/port-stm32 port-stm32)

add_executable(app.elf src/main.cpp "${KB_PORT_STM32_STARTUP}")
target_link_libraries(app.elf PRIVATE kb::port_stm32)
target_link_options(app.elf PRIVATE -T${CMAKE_CURRENT_LIST_DIR}/board/linker.ld -specs=nano.specs
                    -specs=nosys.specs -Wl,--gc-sections)
```

Configure with an Arm GNU toolchain file; `cmake/arm-none-eabi.cmake` is one (`-DKB_ARM_CORE=cortex-m7f` for H7):

```bash
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=third_party/port-stm32/cmake/arm-none-eabi.cmake
cmake --build build
```

The HAL configuration must enable the modules the port uses: `HAL_MODULE_ENABLED` and the `I2C`, `SPI`, `UART`,
`GPIO`, `DMA`, `RCC`, `CORTEX`, `PWR`, `ADC`, `TIM`, `EXTI` and `FDCAN` (G4, H7) or `CAN` (F4, L4) modules. The
examples' `examples/stm32/boards/*/stm32*_hal_conf.h` include ST's template unchanged, which enables every module.
`KB_RTOS` selects the `kb::rtos` backend: `NONE` (bare metal, the default off ESP-IDF) or `FREERTOS` with
`KB_FREERTOS_INCLUDE_DIRS` naming the kernel's headers.

## 2. Create the objects

```cpp
#include "kb/port/stm32/port.hpp"

namespace stm = kb::port::stm32;

stm::I2cConfig MakeI2c() {
  stm::I2cConfig c{};
  c.instance = 1U;
  c.scl = stm::PinId{stm::GpioPort::B, 8U, 4U}; // PB8, AF4
  c.sda = stm::PinId{stm::GpioPort::B, 9U, 4U}; // PB9, AF4
  c.clock_hz = 400000U;
  return c;
}

int main() {
  HAL_Init(); // and the application's clock setup
  static stm::Clock clock{};
  static_cast<void>(clock.Start());
  static stm::I2cController bus{MakeI2c()}; // static: drivers keep references
  if (bus.EnsureReady() != kb::Status::Ok) {
    // bus.LastError().Detail(): HAL status in bits 0..7, ErrorCode from bit 8
  }
  const std::array<uint8_t, 2> tx{0x06U, 0x00U};
  const kb::Status s = bus.Write(0x20U, tx); // NoDevice, Nack, Timeout, BusError, Busy ...
}
```

Objects keep their configuration and touch no hardware until `EnsureReady()` (`Start()` for the clock). Call it
once from one context before the object is shared; after that every member is safe to call from several tasks
([Thread safety](thread-safety.md)). The application provides `SysTick_Handler` calling `HAL_IncTick()` (the
HAL's millisecond tick, which the port's waits count).

## 3. Bind a driver

A driver's port is a thin adapter from its hooks to the kb contracts; `examples/stm32/src/driver_adapters.hpp` has
the adapters for pcal9555, tmc5160 and bno08x. They are templates over the contracts, so the same adapter runs over
this port, another MCU port, a host fake or a `*Ref` chosen at run time.

## Building the host tests

```bash
cmake -S . -B build -G Ninja -DKB_PORT_STM32_BUILD_TESTS=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

No HAL and no Arm toolchain are needed: the boundary files build against fakes of every HAL call they make
([Testing and CI](testing.md)).
