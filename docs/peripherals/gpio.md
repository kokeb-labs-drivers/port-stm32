# GPIO and EXTI

`kb::port::stm32::OutputPin` satisfies `kb::iface::GpioOutput`; `kb::port::stm32::InputPin` satisfies
`kb::iface::GpioInput` and `kb::iface::GpioInterrupt`. HAL `stm32<family>xx_hal_gpio` for the setup, the port's own
EXTI handler for interrupts.

## How it works

| Call | What |
| --- | --- |
| `OutputPin::EnsureReady()` | The port clock, `HAL_GPIO_Init` as push-pull or open-drain output with the pull and speed asked, the `initial` level driven first |
| `Set(level)` | One write of BSRR: atomic, no lock |
| `Toggle()` | Reads ODR and writes BSRR with interrupts masked between the two: atomic |
| `InputPin::Get(level)` | Reads IDR |
| `EnableInterrupt(edge, fn, context)` | The EXTI line of the pin number on rising, falling or both edges, the NVIC line (EXTI0 to EXTI4, EXTI9_5, EXTI15_10) at `irq_priority` |
| `DisableInterrupt()` | The EXTI line masked; the NVIC line stays on for the other pins that share it |
| `Release()` | `HAL_GPIO_DeInit`; the pin unclaimed |

Levels are electrical (`High` is VDD), never "active".

## Pins

| Item | Note |
| --- | --- |
| Pin | `PinId{port, pin}`; the alternate function field is ignored |
| Ports | A to K where the device has them (`GPIOx` in the device header); another port is `InvalidArgument` |
| Pull | `None`, `Up` or `Down` (about 40 kOhm) |
| Speed | `Low` (default) to `VeryHigh`: the edge rate; use the lowest that meets the signal |
| One object per pin | A second object on a claimed pin gets `Busy` from `EnsureReady()` |
| One interrupt per line | EXTI has 16 lines, one per pin number: PA3 and PB3 cannot both have an interrupt (`Busy`) |

## Timing limits

| Item | Bound |
| --- | --- |
| Every call | A few register accesses; nothing waits |
| Interrupt latency | The NVIC's, at `irq_priority`; the handler clears the pending bit, then calls the callback |

## Results

| Result | When |
| --- | --- |
| `Ok` | Done |
| `InvalidArgument` | Pin above 15, a port the device lacks, no callback |
| `Busy` | The pin, or for an interrupt its EXTI line, is owned by another object |
| `NotReady` | Before `EnsureReady()` |

The callback runs in interrupt context (`HandleExtiIrq()`, [Interrupts](interrupts.md)) and may only use
`kb::rtos` `...FromIsr` calls.
