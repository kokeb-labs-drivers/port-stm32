# Evidence form

Copy this form to `runs/<yyyy-mm-dd>-<board>.md` for each run and fill every cell. A cell that cannot be filled says
why. Measured values are copied from the `KB-BENCH` lines of the console log, which is committed beside the form.

## Run

| Field | Value |
| --- | --- |
| Date | |
| Board and revision | |
| Silicon revision (`DBGMCU->IDCODE` REV_ID) | |
| STM32Cube HAL version and commit (`docs/soup.md`) | |
| CMSIS device and core commits | |
| Toolchain (`arm-none-eabi-gcc --version`) | |
| Clock tree (core, AHB, APB1/2, SPI123 kernel, ADC clock) | |
| RTOS and interrupt priorities (DMA streams, SPI, ADC stream, SysTick) | |
| Image commit (port-stm32) and kb-core lock commit | |
| `dma_bench.elf` SHA-256 | |
| Wiring check (jumper PB5-PA6, divider on the six ADC inputs, USB) | |
| Probe and console (SWD probe, VCP port, 115200 8N1) | |
| Allocations after start (expected none: no heap in the port) | |

## Items

| Item (`KB-BENCH` name) | Design case | Expected | Measured | PASS/FAIL | Log line |
| --- | --- | --- | --- | --- | --- |
| `spi-d2-it-8` | T3 on D2 | 100 frames of 8 bytes back unchanged | | | |
| `spi-d2-dma-64` | T1/T2 on D2 | 100 DMA frames of 64 bytes back unchanged | | | |
| `spi-d2-dma-1k` | T1/T2 on D2 | 4 DMA frames of 1024 bytes back unchanged | | | |
| `spi-axi-it-8` | T3 with the D-cache on | as above, AXI staging | | | |
| `spi-axi-dma-64` | T1/T2 with the D-cache on | as above, AXI staging | | | |
| `spi-axi-dma-1k` | T1/T2 with the D-cache on | as above, AXI staging | | | |
| `spi-isr-cost` | B19 | `max_isr_cycles` recorded per path | | | |
| `spi-1k-at-it-ceiling-x4` | ceiling | 4 frames back unchanged at the interrupt ceiling | | | |
| `dma-te-dtcm` | T14, B21, B31 | `Fault`, `te` ≥ 1, `reentries` 0 | | | |
| `spi-ovr-forced` | T14 | `BusError`, not faulted | | | |
| `t7-en-clear` | T7 | `Timeout`, `max_stop_cycles` ≤ `bound` | | | |
| `nvic-priority-readback` | B32 | priority equals the assignment after a vendor value | | | |
| `mpu-readback` | start-time check | `NotReady` without the MPU region | | | |
| `adc-start` | B16, B33 | started twice `Ok`, mode DMA, `readback` 6 | | | |
| `adc-1khz-x6-rate` | rate, jitter, B19 | 1990 to 2010 frames in 2 s; jitter and ISR cycles recorded | | | |
| `adc-consumer-blocked` | T9 | `overruns` > 0, gap flagged | | | |
| `adc-stall-supervision` | N11, T12 | `Restart` after the trigger stops, frames resume | | | |
| `adc-ovr-counted` | B5, T13 | `hw_overruns` increased by the restart | | | |

## Verdict

| Field | Value |
| --- | --- |
| Items passed / total | |
| Open items and why | |
| Run by (role, no personal data) | |
