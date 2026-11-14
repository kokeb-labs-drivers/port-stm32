# Examples

`examples/stm32/` is one CMake firmware project for the four families: `EXAMPLE_BOARD` picks the device, its linker
script, its HAL configuration and its pins (`boards/<board>/`). It builds `all_drivers.elf`:

1. a self test of the port: `Clock`, `BusyWait`, `ItmLogSink`, a `PwmChannel` at 5 kHz and 25 %, an `AdcChannel`
   read in millivolts, a `CanController` in internal loopback (one frame sent and received back), a `Uart` write;
2. the **pcal9555** I/O expander and the **bno08x** IMU sharing one `I2cController`, and the **tmc5160** stepper
   driver on a `SpiDevice` with its enable pin on an `OutputPin`, bound through `src/driver_adapters.hpp`;
3. one loop (bare metal, `KB_RTOS=NONE`) polling the IMU and, every 500 ms, toggling an expander output and reading
   the motor position.

The drivers are the pinned submodules in `drivers/` (`drivers/drivers.lock.yml`): pcal9555, tmc5160 and bno08x at
v0.1.0-rc.1.

## Boards

| `EXAMPLE_BOARD` | Device | Core | Flash, RAM in the linker script | I2C (pcal9555, bno08x) | SPI (tmc5160), CS | UART | PWM | ADC | CAN |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `g4` | STM32G474xx | `cortex-m4f` | 512 KB, 128 KB | I2C1 PB8/PB9 | SPI1 PA5/PA6/PA7, PB6 | USART2 PA2/PA3 | TIM3 CH2 PB5 | ADC1 IN1 PA0 | FDCAN1 PA12/PA11 |
| `h7` | STM32H743xx | `cortex-m7f` | 2 MB, 128 KB (DTCM) | I2C1 PB8/PB9 | SPI1 PA5/PA6/PB5, PD14 | USART3 PD8/PD9 | TIM4 CH1 PD12 | ADC1 INP15 PA3 | FDCAN1 PD1/PD0 |
| `f4` | STM32F446xx | `cortex-m4f` | 512 KB, 128 KB | I2C1 PB8/PB9 | SPI1 PA5/PA6/PA7, PB6 | USART2 PA2/PA3 | TIM3 CH1 PB4 | ADC1 IN0 PA0 | CAN1 PA12/PA11 |
| `l4` | STM32L476xx | `cortex-m4f` | 1 MB, 96 KB | I2C1 PB8/PB9 | SPI1 PA5/PA6/PA7, PB6 | USART2 PA2/PA3 | TIM3 CH1 PB4 | ADC1 IN5 PA0 | CAN1 PA12/PA11 |

The tmc5160 enable and the bno08x interrupt pins are in each `board.hpp`. The log goes to ITM port 0 (SWO).

## Build

```bash
git submodule update --init drivers
cmake -S examples/stm32 -B build/g4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DEXAMPLE_BOARD=g4
cmake --build build/g4                                  # build/g4/all_drivers.elf and all_drivers.map
cmake -S examples/stm32 -B build/h7 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DKB_ARM_CORE=cortex-m7f -DEXAMPLE_BOARD=h7
```

CI builds the four boards (`ci-cmake-matrix.yml`). The link reports newlib-nano's unimplemented system calls
(`_close`, `_read`, ...) as warnings: nothing in the firmware calls them.

## Flash and run

Any SWD probe: `openocd -f interface/stlink.cfg -f target/stm32g4x.cfg -c "program build/g4/all_drivers.elf verify
reset exit"`, then read SWO at the core clock (16 MHz HSI on G4 and F4, 64 MHz HSI on H7, 4 MHz MSI on L4). The
examples have not been run on hardware by this repository's CI.
