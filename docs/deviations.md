# MISRA boundary and deviations

The port is scanned with the CodeQL MISRA C++:2023 pack and the KB-CPP20 profile pack at level high
(`ci-misra.yml`, kb-core ADR-0011), on a traced host build and a traced Cortex-M build of `inc/` and `src/` (the
port over the HAL fakes, both family variants: `kb_port_stm32_scan`).

## The boundary

STM32Cube HAL and CMSIS headers are included only by the files of `deviations/boundary-files.yml`:

| File | HAL areas |
| --- | --- |
| `src/stm32_bus_boundary.cpp` | `hal_i2c`, `hal_spi`, `hal_uart`, the UART receive handler |
| `src/stm32_io_boundary.cpp` | `hal_gpio` and EXTI, `hal_cortex` (NVIC), `hal_adc`, `hal_tim`, `hal_rcc`, DWT, ITM, the `HAL_StatusTypeDef` value check |
| `src/stm32_can_boundary.cpp` | `hal_fdcan` (G4, H7) or `hal_can` (F4, L4) |
| `src/stm32_vectors_boundary.cpp` | The device header's instance macros, for the interrupt vectors |

Each boundary function converts its arguments, makes one HAL call (or a fixed short sequence) and returns the
vendor code. Every decision (what a code means, retries, recovery, locking, timing arithmetic) is in the
platform-free files, which include no HAL header and have **no deviation**. The layering check (`ci-layering.yml`)
enforces the boundary, and its poison build compiles the platform-free code with every HAL header replaced by one
that stops the build.

## Records

| Id | Guideline | Where | Why | How the risk is held |
| --- | --- | --- | --- | --- |
| DEV-001 | Rule 6.7.1 (local variables with static storage) | the bus, io and CAN boundary files | The HAL keeps each peripheral's state in a handle that outlives each call and is reached from the interrupt handlers, and no HAL type may reach a public header: the handles live in one function-local static table per peripheral (also the pin claims, EXTI lines, ADC locks, timer wrap counters, CAN owners) | One accessor per table in an anonymous namespace; entries written by the owner's `EnsureReady()`, cleared by `Release()`; a second owner refused with `Busy`; entries shared with an interrupt changed with it masked; every accessor exercised by the host tests |
| DEV-002 | Rule 8.2.3 (casts that remove const) | the bus boundary file, one call | `HAL_I2C_Master_Transmit` takes `uint8_t*` for data it only reads; the contract hands the port `std::span<const uint8_t>`, and a write cannot be split into copies | One call site; the pinned HAL only reads through the pointer; the tests check the bytes reach the bus and the caller's buffer is unchanged |

The records are in `deviations/deviations.yml`. A marker `// codeql::misra_deviation_next_line(DEV-001)` stands
on the line before each finding it covers. A clang-tidy suppression of a check the MISRA matrix relies on names its
record inside the comment, never as its first or last word (a bare id there would be read as a CodeQL marker):
`// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): DEV-002, the HAL lacks the const`. A marker that covers
nothing, an unknown id or a lapsed record (`review_by` passed) fails the scan.

## Findings avoided by design

| Guideline | What the HAL would have needed | What the port does instead |
| --- | --- | --- |
| Rule 8.2.5 (`reinterpret_cast`) | `HAL_UART_Transmit` takes `uint8_t*` for log text | `UartLogSink` converts each character with `std::bit_cast` into a byte buffer |
| Rule 24.5.2 (`memcpy`) | Copying frames and text with `memcpy` | `std::array` assignment and element loops |
| Rule 8.2.6 (`void*` to object pointer) | HAL handles in public headers | Handles stay in the boundary files' tables; the HAL's `void*` callbacks are not used |
| Rule 10.2.3 (`HAL_StatusTypeDef` cast to `void`) | Discarding the result of `HAL_SPI_Abort`, `HAL_ADC_Stop`, `HAL_FDCAN_Stop`, `HAL_CAN_Stop` | Each result is used: folded into the reported status or `ErrorCode` |
| Weak-symbol overrides | Overriding `HAL_UART_RxCpltCallback` and the other HAL callbacks | The port's own handlers read the registers; no HAL callback is defined |

## Advisory findings left

| Guideline | Where | Why it stays |
| --- | --- | --- |
| Rule 6.5.1 (external linkage not declared in a header) | The interrupt vectors in `src/stm32_vectors_boundary.cpp`; `HandleExtiIrq`, `HandleClockIrq` | The vector names are fixed by the startup file's table, which declares them weak; the entry points are declared in `irq.hpp` |
| Rule 10.1.1 (parameter could be const) | The ADC conversion helper | `HAL_ADC_GetValue` takes a non-const handle on F4 |

Advisory findings are reported and do not block.
