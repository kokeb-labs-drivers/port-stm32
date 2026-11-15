# Integration: CubeMX, interrupts, RTOS and caches

## What the port owns

The port owns the HAL handles of every peripheral instance it opens (they live in its boundary files), the
interrupt vectors of those peripherals, and these HAL callbacks, which it defines (they are weak in the HAL):

| Peripheral | HAL callbacks the port defines |
| --- | --- |
| I2C | `HAL_I2C_MasterTxCpltCallback`, `HAL_I2C_MasterRxCpltCallback`, `HAL_I2C_ErrorCallback`, `HAL_I2C_AbortCpltCallback` |
| SPI | `HAL_SPI_TxCpltCallback`, `HAL_SPI_TxRxCpltCallback`, `HAL_SPI_ErrorCallback` |
| ADC | `HAL_ADC_ConvCpltCallback`, `HAL_ADC_ErrorCallback` |
| FDCAN (G4, H7) | `HAL_FDCAN_RxFifo0Callback`, `HAL_FDCAN_TxBufferCompleteCallback` |
| bxCAN (F4, L4) | `HAL_CAN_RxFifo0MsgPendingCallback`, `HAL_CAN_ErrorCallback`, `HAL_CAN_TxMailbox{0,1,2}CompleteCallback`, `HAL_CAN_TxMailbox{0,1,2}AbortCallback` |

Each callback looks up the handle in the port's table and does nothing for a handle the port does not own. The
UART, EXTI and clock interrupts do not use HAL callbacks: the port's own handlers read the registers.

With `USE_HAL_*_REGISTER_CALLBACKS` set to 1 in the HAL configuration, `HAL_*_Init` installs these same functions as
the handle's callbacks, so the port works either way.

## A CubeMX project

- **Leave the port's peripherals unconfigured in CubeMX** (no init code, no NVIC entry): the port initialises them in
  `EnsureReady()`. Peripherals the port does not use stay CubeMX's.
- **Drop the generated handlers of the port's vectors** from `stm32xx_it.c` (USART/UART/LPUART, EXTI, TIM2 or TIM5,
  I2C event and error, SPI, ADC, FDCAN line 0 or bxCAN TX/RX0/SCE), and the generated callback functions listed above.
  A duplicate is a link error, never a silent override. Or set `KB_PORT_STM32_VECTORS=OFF` and call the entry points
  of `irq.hpp` from the generated handlers.
- **Drop the `HAL_*_MspInit` / `HAL_*_MspDeInit` bodies** of the port's peripherals: the port configures its pins
  itself, and an `MspDeInit` that resets pins would run on every SPI mode change (`SpiConfigure` re-initialises).
- **The HAL time base**: with FreeRTOS, CubeMX moves the HAL time base off SysTick to a TIMx (often at the lowest
  priority). That is fine for the port, whose waits never read it; but it must run whenever tasks run, because the
  HAL's own initialisation waits read it (see [How the port waits](waits.md)). Never call `EnsureReady()` inside a
  critical section or with BASEPRI raised.

## Interrupt priorities

NVIC priority numbers are the unshifted ones of `HAL_NVIC_SetPriority` (0 to 15 with 4 priority bits). Under
FreeRTOS, an interrupt that calls the RTOS must have a priority number at or above (that is, at or below in
urgency) `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`:

| Interrupt | Default | Calls the RTOS |
| --- | --- | --- |
| UART (`UartConfig::irq_priority`) | 5 | Yes (a semaphore give) |
| I2C event and error (`I2cConfig::irq_priority`) | 5 | Yes |
| SPI (`SpiBusConfig::irq_priority`) | 5 | Yes |
| ADC (fixed) | 5 | Yes |
| CAN (`CanConfig::irq_priority`) | 5 | Yes |
| EXTI (`InputConfig::irq_priority`) | 5 | Only if the application's callback does |
| Clock wrap (`ClockConfig::irq_priority`) | 1 | No (it may stay above the syscall limit) |

The defaults match `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5, the value in the example's
`FreeRTOSConfig.h` and a common one; with another value, set the priorities accordingly.

## RTOS objects and start-up order

`EnsureReady()` creates the object's `kb::rtos` mutexes and semaphores. Call it after the scheduler runs (in the
first task), never from a static constructor: an RTOS object created before the scheduler can leave BASEPRI
raised until the scheduler starts, which stops the HAL time base. The example under `EXAMPLE_RTOS=FREERTOS` does
this.

## Caches and DMA (H7)

The port moves data by interrupt (the CPU copies every byte); it uses no DMA. CPU-path transfers need no cache
maintenance, and caller buffers may be anywhere the CPU can reach, D-cache on or off.

When DMA is added to the port, these rules apply on STM32H7 (observed with an earlier implementation on STM32H7
hardware):

- DMA buffers are 32-byte aligned and sized to whole cache lines; the D-cache is cleaned before a transmit and
  invalidated before the CPU reads received data, or the buffers are in an MPU region marked non-cacheable;
- invalidating the D-cache after a CPU copy into a cacheable buffer discards the data just written, so the
  maintenance belongs to DMA buffers only;
- DMA1 and DMA2 cannot reach DTCM: no DMA buffer there;
- on the Cortex-M4 of a dual-core part, byte and half-word loads from FMC SDRAM can return wrong data (errata
  ES0445): no such buffers.

## Dual-core H7

The port supports the Cortex-M7 of single-core STM32H7 parts. Dual-core parts (`STM32H745xx`, `STM32H747xx`,
`STM32H755xx`, `STM32H757xx`) are refused at configure time, and a build with `CORE_CM4` stops at compile time: on
the second core the EXTI pending bits are in the core-2 registers (`C2PR1`), the port's clock and tables are per
core, and the peripherals are shared between the cores.
