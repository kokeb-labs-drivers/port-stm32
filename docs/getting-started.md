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
`KB_FREERTOS_INCLUDE_DIRS` naming the kernel's headers (kb-core includes them as `freertos/<header>.h`;
`cmake/kb_port_stm32_freertos.cmake` fetches the pinned FreeRTOS-Kernel and provides that layout, as the example
does with `-DEXAMPLE_RTOS=FREERTOS`).

`KB_RTOS=NONE` has no scheduler: kb-core's `none` backend is deterministic with a fake tick, its `SleepFor` does not
sleep on silicon, and a semaphore take cannot block. The port's waits then poll their completion, still bounded on
the port's clock ([How the port waits](waits.md)). Use FreeRTOS when a task must sleep while it waits.

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

// Under FreeRTOS: the first task, after the scheduler runs. Bare metal: from main().
void AppMain() {
  static stm::Clock clock{};
  static_cast<void>(clock.Start()); // first: every wait of the port is measured on it
  static stm::I2cController bus{MakeI2c()}; // static: drivers keep references
  if (bus.EnsureReady() != kb::Status::Ok) {
    // bus.LastError().Detail(): HAL status in bits 0..7, ErrorCode from bit 8
  }
  const std::array<uint8_t, 2> tx{0x06U, 0x00U};
  const kb::Status s = bus.Write(0x20U, tx); // NoDevice, Nack, Timeout, BusError, Busy ...
}
```

Objects keep their configuration and touch no hardware until `EnsureReady()` (`Start()` for the clock). Call it
once from one context before the object is shared, after the scheduler runs (it creates the object's `kb::rtos`
mutexes and semaphores), never from a static constructor and never with interrupts masked; after that every member
is safe to call from several tasks ([Thread safety](thread-safety.md)). Start the `Clock` first: until a `Clock`
runs, the port measures its waits on the DWT cycle counter.

## 3. The HAL time base

The port's waits never read `HAL_GetTick()`. The HAL's own initialisation code still does (`HAL_UART_Init`, the
ADC enable, the CAN initialisation and start), so its time base must run, and `Uart`, `AdcChannel` and
`CanController` refuse `EnsureReady()` with `NotReady` (port code `kHalTickStopped`) when it does not:

| Setup | HAL time base |
| --- | --- |
| Bare metal | `SysTick_Handler` calling `HAL_IncTick()` (what `HAL_Init()` sets up) |
| FreeRTOS | FreeRTOS owns `SysTick_Handler` (`xPortSysTickHandler`), so the application must **not** define one. Give the HAL a time base of its own: a TIMx whose update interrupt calls `HAL_IncTick()` (CubeMX's "Timebase Source" setting, `HAL_InitTick()` overridden), or `HAL_IncTick()` from `vApplicationTickHook()` as the example does |

Either way, call `EnsureReady()` only from task context with interrupts enabled.

## 4. Interrupts

The port defines the vectors of the peripherals it serves and the HAL completion callbacks of I2C, SPI, ADC and
CAN; a CubeMX project leaves those peripherals unconfigured and drops the generated handlers and callbacks
([Integration](integration.md)). The default priorities (5) suit FreeRTOS with
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5.

## 5. Bind a driver

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
