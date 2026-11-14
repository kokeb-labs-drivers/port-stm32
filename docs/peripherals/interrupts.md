# Interrupts

The port serves three kinds of interrupt itself and never calls a HAL interrupt handler or a HAL callback:

| Entry point (`kb/port/stm32/irq.hpp`) | Vector | What it does |
| --- | --- | --- |
| `HandleUartIrq(instance)` | `USART1_IRQHandler` ... `UART8_IRQHandler`, `LPUART1_IRQHandler` | Reads the receive flags; each byte into the `Uart`'s ring, errors latched; clears the error flags |
| `HandleExtiIrq(first, last)` | `EXTI0_IRQHandler` ... `EXTI4_IRQHandler`, `EXTI9_5_IRQHandler`, `EXTI15_10_IRQHandler` | For each pending line of the range: clears it, calls the `InputPin`'s callback |
| `HandleClockIrq(timer)` | `TIM2_IRQHandler`, `TIM5_IRQHandler` | Counts one wrap of the `Clock` |

The vectors are defined in `src/stm32_vectors_boundary.cpp`, built unless `KB_PORT_STM32_VECTORS=OFF`, each only
where the device header defines the instance. An application that needs one of those vectors for itself (a USART
used with DMA by other code, a TIM5 used elsewhere) sets the option `OFF` and calls the entry points from its own
handlers.

Priorities: the `irq_priority` of each config (NVIC preemption priority, sub-priority 0). Under FreeRTOS keep them
numerically at or above `configMAX_SYSCALL_INTERRUPT_PRIORITY` when a callback uses `...FromIsr` calls.
