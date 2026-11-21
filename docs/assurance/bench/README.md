# Bench verification

The DMA layer ([DMA](../../dma.md)) is tested on host against kb-core's DMA-and-cache model. What a model cannot show
(real caches and speculative reads, bus errors, EN-clear latency, interrupt cost, ADC overrun behaviour, MPU set-up)
is verified on a board, by hand, with the bench app and the fixed evidence form here.

| File | What |
| --- | --- |
| [procedure-stm32h7.md](procedure-stm32h7.md) | Step-by-step procedure for an STM32H7 board of the H743/H747 class, with the wiring and every item's hardware |
| [evidence-form.md](evidence-form.md) | The form a run fills in: one table per run, one row per item |
| `runs/<yyyy-mm-dd>-<board>.md` | Filled forms, one per run (none yet) |
| `../../../tests/target/dma/` | The bench app `dma_bench` (built in CI for G4, H7, F4 and L4 by the STM32 family matrix) |

The app prints one line per item:

```text
KB-BENCH <item> PASS|FAIL|SKIP key=value ...
```

A run is complete when every item of the procedure has a line, the form is filled, and the console log is attached.
A failing item is recorded as failing, with its log; it is not re-run until it passes without a note.

Boards: the procedure is written for an STM32H7 board of the H743/H747 class (a Nucleo-H743ZI2 or similar). The F4,
G4 and L4 builds of the app run the SPI items on their cacheless SRAM and print `SKIP` for the H7-only items; a
procedure for them follows when a board of those families is on the bench.
