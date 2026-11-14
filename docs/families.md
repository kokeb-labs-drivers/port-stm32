# Families

One library for four families. The family is chosen by the CMSIS device macro (`KB_PORT_STM32_DEVICE`, for example
`STM32G474xx`); the port's boundary files then include `stm32<family>xx_hal.h` and use the device header's instance
macros (`I2C4`, `FDCAN2`, `TIM5`, ...) so a peripheral the device lacks is refused with `Unsupported` or
`InvalidArgument`, never a fault.

| | G4 | H7 | F4 | L4 |
| --- | --- | --- | --- | --- |
| Example device | STM32G474xx | STM32H743xx | STM32F446xx | STM32L476xx |
| Core, toolchain core | Cortex-M4F, `cortex-m4f` | Cortex-M7F, `cortex-m7f` | Cortex-M4F, `cortex-m4f` | Cortex-M4F, `cortex-m4f` |
| HAL driver | `stm32g4xx-hal-driver` v1.2.7 | `stm32h7xx-hal-driver` v1.11.7 | `stm32f4xx-hal-driver` v1.8.6 | `stm32l4xx-hal-driver` v1.13.6 |
| CMSIS device | `cmsis-device-g4` v1.2.6 | `cmsis-device-h7` v1.10.8 | `cmsis-device-f4` v2.6.11 | `cmsis-device-l4` v1.7.5 |
| I2C peripheral | v2 (TIMINGR) | v2 (TIMINGR) | v1 (CCR, ClockSpeed) | v2 (TIMINGR) |
| I2C clock range | 10 kHz to 1 MHz | 10 kHz to 1 MHz | 10 kHz to 400 kHz | 10 kHz to 1 MHz |
| CAN | FDCAN1 to FDCAN3 (classic) | FDCAN1, FDCAN2 (classic) | bxCAN CAN1, CAN2 | bxCAN CAN1 (CAN2 on some parts) |
| ADC calibration | `HAL_ADCEx_Calibration_Start` | `HAL_ADCEx_Calibration_Start` | none | `HAL_ADCEx_Calibration_Start` |
| ADC resolution (`FullScale()`) | 12 bit (4095) | 16 bit (65535) | 12 bit (4095) | 12 bit (4095) |
| UART receive flags | ISR register | ISR register | SR register | ISR register |
| 32-bit timers (Clock) | TIM2, TIM5 | TIM2, TIM5 | TIM2, TIM5 | TIM2, TIM5 |

## Clock notes

- The port never configures the clock tree: the application does (the examples run from the reset clock: HSI on
  G4, H7 and F4, MSI on L4), and calls `SystemCoreClockUpdate()` after a change.
- **Kernel clocks.** Where a peripheral's kernel clock can differ from its bus clock the config has a
  `kernel_clock_hz` field; 0 takes the reset selection: PCLK1 for I2C, `HAL_RCCEx_GetPeriphCLKFreq` for SPI on H7
  and for FDCAN, the APB clock of the instance for SPI elsewhere, PCLK1 for bxCAN. Set it when the application
  selects another kernel source. The UART baud rate is computed by the HAL from its own clock getter.
- **Timer clocks.** Timers run at twice their APB clock when the APB prescaler is not 1 (the RM's rule); the port
  applies it. `timer_clock_hz` overrides it.
- **H7 caches.** The port's transfers are CPU copies (no DMA), so the D-cache needs no maintenance for them.

## Pin notes

- Every peripheral pin is a `PinId{port, pin, af}`: the alternate function number comes from the device
  datasheet's alternate-function table, not from the port. A wrong AF shows as a peripheral that never answers.
- I2C pins are open-drain with optional weak pull-ups; SPI, UART and CAN pins push-pull at high speed; ADC pins are
  set analog; chip selects, RS-485 DE and GPIOs are plain GPIO.
- Ownership: each I2C, SPI, UART and CAN instance, ADC channel count and timer is opened by one object at a time,
  and every GPIO-mode pin (an `OutputPin`, `InputPin`, chip select or RS-485 DE) is claimed by its object; a second
  object gets `Busy`. Alternate-function pins are not claimed: keep the board's pin map free of overlaps.
