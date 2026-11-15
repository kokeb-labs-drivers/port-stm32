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

- **The vectors.** With `KB_PORT_STM32_VECTORS=ON` (the default) the port's vector file defines **every**
  USART/UART/LPUART, I2C event and error, SPI, ADC, FDCAN line 0 or bxCAN TX/RX0/SCE, EXTI and TIM2/TIM5 vector the
  device has, whether or not a port object uses the instance. For a peripheral CubeMX (or other code) keeps, name its
  vectors in `KB_PORT_STM32_VECTORS_EXCLUDE` (for example `-DKB_PORT_STM32_VECTORS_EXCLUDE="USART1_IRQHandler"`):
  the port does not define them, and the generated handlers stay. A vector the port serves for an instance no port
  object owns switches its NVIC line off at the first interrupt (UART, I2C, SPI, CAN, a shared ADC vector with no
  open instance), so a peripheral left enabled by other code cannot make it re-enter forever; that peripheral then
  gets no interrupts, so exclude its vectors. EXTI lines are grouped in vectors (`EXTI9_5`, `EXTI15_10`): a line no
  `InputPin` owns is cleared and not served; to serve a line of the application, exclude the vector and call
  `HandleExtiIrq()` from the application's handler for the port's lines. `KB_PORT_STM32_VECTORS=OFF` drops the whole
  file. A vector defined twice is a link error, never a silent override; `tools/check_vectors.cmake` checks after
  the link that every vector the port defines is its strong symbol (an excluded one is not checked).
- **Leave the port's peripherals unconfigured in CubeMX** (no init code, no NVIC entry): the port initialises them in
  `EnsureReady()`. Drop the generated callback functions listed above.
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
| ADC (`AdcConfig::irq_priority`, the instance's first channel's) | 5 | Yes |
| CAN (`CanConfig::irq_priority`) | 5 | Yes |
| EXTI (`InputConfig::irq_priority`) | 5 | Only if the application's callback does |
| Clock wrap (`ClockConfig::irq_priority`) | 1 | No (it may stay above the syscall limit) |

The defaults match `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5, the value in the example's
`FreeRTOSConfig.h` and a common one. Set `KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY` (CMake) to that value: `EnsureReady()`
of `Uart`, `I2cController`, `SpiBus`, `AdcChannel` and `CanController` then refuses an `irq_priority` below it, or
above 15, with `InvalidArgument`, so no RTOS-calling interrupt can run above the syscall limit. It is 0 by default
(bare metal: no limit); the FreeRTOS example sets 5. EXTI and the clock wrap are not checked (their handlers do not
call the RTOS unless the application's EXTI callback does).

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
