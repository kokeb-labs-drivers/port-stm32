# Interrupts

The port's vector file (`src/boundary/vectors.cpp`, built unless `KB_PORT_STM32_VECTORS=OFF`) defines these
vectors, each only where the device header defines the instance, and calls the entry points of
`kb/port/stm32/irq.hpp`:

| Entry point | Vectors | What it does |
| --- | --- | --- |
| `HandleUartIrq(instance)` | `USART1_IRQHandler` ... `UART8_IRQHandler`, `LPUART1_IRQHandler` | Reads the flag and data registers into a `UartEvent` (errors always cleared); `Uart::OnInterrupt()` decides: the byte into the ring, errors latched, the next byte to send |
| `HandleExtiIrq(first, last)` | `EXTI0_IRQHandler` ... `EXTI4_IRQHandler`, `EXTI9_5_IRQHandler`, `EXTI15_10_IRQHandler` | For each pending line of the range: clears it, calls the `InputPin`'s callback |
| `HandleClockIrq(timer)` | `TIM2_IRQHandler`, `TIM5_IRQHandler` | With interrupts masked: clears the update flag and counts one wrap of the `Clock` |
| `HandleI2cIrq(instance, error)` | `I2Cn_EV_IRQHandler`, `I2Cn_ER_IRQHandler` | `HAL_I2C_EV_IRQHandler` / `HAL_I2C_ER_IRQHandler` of the open instance |
| `HandleSpiIrq(instance)` | `SPIn_IRQHandler` | `HAL_SPI_IRQHandler` of the open instance |
| `HandleAdcIrq(first, last)` | `ADC_IRQHandler` (F4, H7), `ADC1_2_IRQHandler` (G4, L4), `ADC3_IRQHandler` ... `ADC5_IRQHandler` | `HAL_ADC_IRQHandler` of each open instance on the vector |
| `HandleCanIrq(controller)` | `FDCANn_IT0_IRQHandler` (G4, H7), `CANn_TX_IRQHandler`, `CANn_RX0_IRQHandler`, `CANn_SCE_IRQHandler` (F4, L4) | `HAL_FDCAN_IRQHandler` / `HAL_CAN_IRQHandler` of the open controller |

The HAL handlers end in the HAL callbacks the port defines ([Integration](../integration.md)), which complete the
owner's `Completion` or feed its ring.

## Linked for certain

The startup file names every vector weakly and aliases it to `Default_Handler` (an endless loop). A weak
reference never pulls a member out of a static archive, so the vector file is built as an object library whose
object is added to every executable that links `kb::port_stm32`; its strong definitions always replace the aliases.
After each firmware link, the example checks with `nm` (`tools/check_vectors.cmake`) that every vector the vector
object defines, and every HAL callback the port's library defines, is a strong symbol of the firmware at an
address other than `Default_Handler`'s. CI runs the check for every family.

An application that needs one of those vectors for itself sets `KB_PORT_STM32_VECTORS=OFF` and calls the entry
points from its own handlers.

## Priorities

The `irq_priority` of each config (NVIC preemption priority as passed to `HAL_NVIC_SetPriority`, sub-priority 0).
Under FreeRTOS, an interrupt that calls the RTOS needs an NVIC priority number at or above
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (the unshifted value; the defaults, 5, match the common setting).
The table is in [Integration](../integration.md).
