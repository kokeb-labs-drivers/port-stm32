# DMA

The port's DMA layer: SPI by DMA through port-owned staging buffers, and a timer-triggered ADC scan streamed by
circular DMA. It implements kb-core's `kb::dma` vocabulary (regions, buffers and leases, cache hooks, streams,
capabilities, stream assignments) for the STM32 DMA1/DMA2 engines. This page is the port's DMA record in the fixed
form: regions, masters, rules, errata handled, the conformance result per case, and what was bench verified.

| Header | What |
| --- | --- |
| `kb/port/stm32/dma.hpp` | Masters, region tags, the assignment checks, `DmaStream` (one stream or channel), `DmaTransfer<Region>` (one leased buffer transfer), `PortCacheOps`, `HandleDmaIrq` |
| `kb/port/stm32/spi_dma.hpp` | `SpiDmaLink<Region, Bytes>`: the two streams and the staging pair of one SPI bus (`SpiBus::AttachDma`) |
| `kb/port/stm32/adc_stream.hpp` | `AdcDmaStream<Frame, Region>`: an ADC scan into `kb::dma::Stream<Frame, 2, Region>`, with its one-shot fallback |

## Regions and masters

kb-core knows policies, not addresses. The port's region tags carry the policy, the line, the alignment unit, the
masters that reach the memory, and its window (`kBase`, `kSize`) for the start-time placement check. A board narrows
a window by defining its own tag with the same members (the board generator writes them from `board.yml`).

| Tag | Memory | Policy | Line / unit | Masters |
| --- | --- | --- | --- | --- |
| `h7::D2Sram` | D2 SRAM1 to SRAM3 behind a non-cacheable MPU region | `NonCacheable` (**the default H7 region**) | 1 / 32 B | DMA1, DMA2, MDMA |
| `h7::AxiSram` | AXI SRAM (D1), write-back cached | `Maintained` | 32 / 32 B | DMA1, DMA2, MDMA |
| `h7::Sram4` | D3 SRAM4 behind a non-cacheable MPU region | `NonCacheable` | 1 / 32 B | BDMA, DMA1, DMA2, MDMA |
| `h7::Dtcm` | DTCM | `Coherent` | 1 / 4 B | MDMA only (declared so a DMA1/2 buffer there fails to build) |
| `Sram` | Main SRAM of F4, G4, L4 (no data cache) | `Coherent` | 1 / 4 B | DMA1, DMA2 |

Masters are `kMasterDma1`, `kMasterDma2`, `kMasterBdma`, `kMasterMdma`. The port drives DMA1 and DMA2 only; BDMA and
MDMA are in the model so the checks can refuse them. In a `kb::dma::StreamAssignment`, engine 1 is DMA1 and 2 is
DMA2; the stream is 0 to 7 on F4 and H7 and the channel 1 to 8 on G4 and L4; the request is the DMAMUX request line
(G4, H7, L4+), the `CHSEL` channel selection (F4) or the `CSELR` value (L4).

The cache hooks (`PortCacheOps`) clean and invalidate by 32-byte line through `SCB->DCCMVAC` and `SCB->DCIMVAC` on
the Cortex-M7 when its D-cache is on, and are barriers only on the cacheless families. `kb::dma::Buffer` hands them
whole lines it owns, so an invalidate never reaches a neighbour.

## Rules the port keeps

1. **Caller memory is never a DMA target.** `SpiDevice::Transfer` copies through the bus's staging pair; zero-copy is
   only `DmaTransfer` over a typed `kb::dma::Buffer`.
2. **Assignment, not search.** A stream comes from the board's `StreamAssignment`; `RequireStream<S>()` and
   `RequireStreamReaches<S, Region>()` check it at compile time, `EnsureReady()` again at start (engine, stream,
   request range, interrupt tier, a stream already enabled by someone else is refused).
3. **The port writes the NVIC priority** of each stream from the assignment in `EnsureReady()` and reads it back; a
   read-back that differs is refused (`NotReady`, port code `0xFE`). A vendor code generator that rewrote the
   priority cannot go unnoticed.
4. **Every interrupt entry services every flag** of its stream (transfer error, direct-mode error, FIFO error, half,
   complete: errors first), each cleared by its own clear bit and counted, whether a transfer is in flight or not
   (`DmaStats::unexpected`). Nothing is left set to re-assert the interrupt. `CGIF` is never written.
5. **Confirmed stop.** A stop clears EN and waits for EN to read 0, bounded by `sdk::DmaStopBoundCycles()` (10 µs of
   core cycles, never below 1024) and by the same number of reads. Only then does the memory return to the CPU; a stop
   that is not confirmed quarantines the buffer, the stream is `Faulted` and refuses transfers until `Recover()`
   (control register cleared, EN proven 0).
6. **Exactly once.** Completion, cancellation and the report meet in a `kb::dma::CompletionLatch`.
7. **Fallback is never silent.** `Capabilities()` and `Health()` of `SpiBus`, `DmaStream`, `DmaTransfer` and
   `AdcDmaStream` say which path serves; an ADC stream on its fallback flags every frame `Fallback()`.
8. **Start-time placement.** `SpiBus::EnsureReady()` and `AdcDmaStream::Start()` refuse (`NotReady`, port code
   `0xFC`) a buffer outside its region's window, and a `NonCacheable` region that no enabled MPU region makes
   uncached while the D-cache is on (`MPU->RNR/RBAR/RASR` read back).
9. **Default H7 region: the D2 window.** Staging buffers and ADC rings go to `h7::D2Sram` unless a board chooses
   `h7::AxiSram` for a buffer (`Maintained`, cleaned and invalidated by line).

### SPI

| Item | Behaviour |
| --- | --- |
| Which frames | `SpiDmaConfig::min_bytes` (default 16) and up by DMA at the device's SCK; shorter by interrupt at the interrupt ceiling |
| Order, H7 (SPI with TSIZE) | SPE cleared and TSIZE written, RXDMAEN, receive stream, transmit stream, TXDMAEN, SPE, CSTART |
| Order, F4, G4, L4 | RXDMAEN, receive stream, transmit stream, TXDMAEN, SPE |
| End | The receive stream's completion ends the frame; the transmit stream may run with errors-only interrupts (`IrqUse::ErrorsOnly`) and is concluded from its registers; the requests are disabled, SPE cleared on H7 |
| Size | One pass of at most the staging capacity; larger frames `OutOfRange` (no silent cut). `SpiLimitsOf<Link>` with `kb::dma::RequireBusCarries` checks drivers at compile time |
| Errors | Stream error: `Fault` (port code `0xFA` and the flags). SPI overrun or mode fault: `BusError`. Deadline: both streams stopped with proof, `Timeout`. Unconfirmed stop: `Fault` (`0xFB`) until `SpiBus::Recover()` |
| Priorities | Both streams at the SPI interrupt's priority (refused otherwise): an error and a completion of one frame never preempt each other |

### ADC scan stream

| Item | Behaviour |
| --- | --- |
| Hardware | Scan of the configured ranks at each rising edge of the board's trigger timer (`trigger`: the family's `ADC_EXTERNALTRIG_*`); circular DMA over a two-frame ring (`Frames = 2`) |
| Overrun | `OVRMOD = 0` (data preserved: the requests stop visibly); OVR read and cleared in the drain and counted with `OnOverrun` (never a frame); the restart reads it as the stop reason; no OVR or EOC interrupt |
| Drainer | The stream's interrupt only (the owner never reads the flags); halves published oldest first; a half the DMA lapped (both flags pending, or the transfer counter inside it after its copy) is published `Torn` |
| Owner | `Service()`: at most `frames_per_pass` frames, the latest published through a seqlock (`Latest()` for any task), then `Stream::Supervise` (signed stall time, restart, back-off, fallback) |
| Start | After the scheduler (refused while the HAL time base does not run); idempotent; refused when the ADC kernel clock reads 0 (the port never writes the kernel-clock multiplexer); the instance is the stream's alone (an `AdcChannel` on it is refused, and the other way round) |
| DMA or fallback | DMA only when a stream is assigned **and** the conversion count read back from the sequence register equals `StreamShape.elements`; otherwise the one-shot fallback (each `Service()` converts every rank by interrupt), reported in `FallbackReason()`, `Mode()`, `Capabilities()`, `Health()` and every frame |
| Calibration | Offset and linearity on H7, offset on G4 and L4, none on F4 |

### 64-bit atomics on Cortex-M (known limit)

`kb::dma::Stream` keeps its frame time stamp in a `std::atomic<uint64_t>`. A 32-bit Cortex-M has no 64-bit atomic
load or store, the Arm GNU toolchain lowers them to calls of `__atomic_load_8` and `__atomic_store_8`, and it ships
no `libatomic`: a firmware that uses `AdcDmaStream` does not link without them. The port library does not provide
them (they are reserved names taking a pointer to void, which would need a MISRA deviation in the port). The bench
app carries a weak pair (`tests/target/dma/atomic64.c`: interrupts masked for the two word accesses, correct on a
single core) that an application may copy until kb-core keeps the time stamp in 32-bit atomics.

## Errata handled

| Sheet (re-check the section numbers against the current revision before citing) | Item | Port behaviour |
| --- | --- | --- |
| ES0392 (H742/743/750/753), ES0445 (H745/747/755/757) | BDMA disable failure and missed error flag on a simultaneous transfer error and global flag clear | No global clear on any engine: every flag by its own bit; BDMA not driven |
| ES0392, ES0491 (H72x/73x), ES0430/ES0431 (G4) | DMAMUX SOFx / OFx flags not asserted in some write coincidences | The port uses no DMAMUX synchronisation or request generator |
| ES0182 (F40x/41x) | DMA2 corruption with concurrent AHB and APB2 peripherals | Not handled in the port: a board must not plan such a DMA2 use (generator rule R11); stated here |
| ES0445 | Cortex-M4 byte and half-word loads from FMC SDRAM | No region tag for SDRAM; the dual-core CM4 build is not covered |

## Conformance: kb-core's suite T1 to T18 on this port

`tests/test_dma_conformance.cpp` runs every case of `kb/dma/host/conformance.hpp` against the port's real classes
with their boundary (`src/boundary/dma.cpp`, `spi_dma.cpp`, `adc_stream.cpp`) replaced, at link time, by
`tests/dma_model/dma_model.cpp`, which drives kb-core's host DMA-and-cache model (`kb::dma::host::Machine`,
`DmaEngine`, `HwRing`). The adapter rigs (`tests/dma_model/port_rig.hpp`) have the member functions of kb-core's
`HostRig` and `AdcRig`; `Port()` forwards to the port's `DmaTransfer` and only translates the result types. Each case
runs in the port's `Maintained` region (H7 AXI SRAM, 32-byte lines: every clean and invalidate checked by the model)
and in the default `NonCacheable` region (H7 D2), with the windows opened to host memory. Both host variants (G4-like,
F4-like) run them.

| Case | What | AXI (`Maintained`) | D2 (`NonCacheable`) | How the port serves it |
| --- | --- | --- | --- | --- |
| T1 | transmit needs a clean | pass | pass | `DmaTransfer` / `SpiDmaLink` lend through `kb::dma::Buffer::Lend` |
| T2 | receive needs an invalidate before and after | pass | pass | as T1, completion settled in task context |
| T3 | the CPU path does no maintenance | pass | pass | interrupt frames call no cache hook (`test_spi_dma.cpp` counts the model's calls) |
| T4 | a neighbour of the receive buffer survives | pass | pass | the buffer type owns whole lines |
| T5 | no CPU view during a lease | pass | pass | `CpuView()` empty while leased |
| T6 | no DMA write after a timeout | pass | pass | `Poll` stops the overdue stream with EN read 0 |
| T7 | an abort waits for the enable bit | pass | pass | bounded EN wait in `DmaStream::stop` |
| T8 | receive rounding stays inside the buffer | pass | pass | padded staging (no rounding on STM32; the model rounds to 4) |
| T9 | a held frame is stable, the overrun shows | pass | pass | `AdcDmaStream` copy-out into `kb::dma::Stream` |
| T10 | a lapped half is flagged | pass | pass | both flags pending, counter inside the half after its copy |
| T11 | one drainer | pass | pass | no task-side drain exists |
| T12 | the stall check is signed | pass | pass | `kb::dma::Stream::CheckStall` |
| T13 | an overflow is not data | pass | pass | ADC OVR read in the drain, `OnOverrun` |
| T14 | error flags | pass | pass | every flag by its own bit, `Fault` with the flags, reported once |
| T15 | a cancel racing a completion | pass | pass | `CompletionLatch` |
| T16 | an unconfirmed stop quarantines | pass | pass | quarantine, `Faulted`, `Recover()` |
| T17 | the fallback is reported | pass | pass | a stream without an assignment reports `Interrupt` and its ceiling |
| T18 | a synchronous transfer goes through staging | pass | pass | the real `SpiDevice::Transfer` on a bus with an `SpiDmaLink` |

Three cases also run with a defect in the boundary model (`dma_model::BoundaryFaults`) and must fail: T1 with the
clean removed, T2 with the invalidate removed, T7 with EN reading 0 at once. They do, so the suite sees through the
port to its boundary. Port unit tests beside it: `test_dma.cpp` (B31 a flag with nothing in flight cleared, counted,
no re-entry; B32 priority written and read back, refused on a mismatch; refusals, errors, quarantine, recovery,
dispatch, region check), `test_spi_dma.cpp` (order, data, CPU path, limits, SCK, timeout, SPI overrun, stream errors,
quarantine and recovery, start-time checks), `test_adc_stream.cpp` (B33 conversion count against the frame and the
one-shot fallback, signed stall time and restart, OVR counted, single drainer, seqlock publication, idempotent start,
kernel clock 0, shared instance, bounded pass), and compile-fail tests (a DTCM buffer for DMA1, a priority outside the
implemented bits, an engine that is not DMA1/2, a driver frame above the staging buffers, a frame that is not 16-bit
samples).

## Bench verification

Nothing of this page has run on hardware yet. The bench kit is ready: `tests/target/dma/dma_bench.cpp` (built in CI
for the four families), the procedure for an STM32H7 board and the evidence form in
[Bench verification](assurance/bench/README.md). Until a filled form is under `docs/assurance/bench/runs/`, every
on-target item stays open:

- the SPI loopback with the D-cache on, in the D2 window and in AXI SRAM (T1, T2, T3 on real caches, speculative
  refill);
- the transfer error of a DMA1 stream lent DTCM memory, and the error flag serviced without re-entry (T14, B21, B31);
- the SPI overrun of a stalled receive stream (T14);
- the EN-clear latency against `DmaStopBoundCycles()` (T7);
- 1 kB frames at the interrupt ceiling ×4 and at the full SCK, the interrupt cost (DWT);
- the NVIC priority read back after a vendor initialisation set another value (B32);
- the MPU read-back refusing a D2 window without its MPU region;
- the ADC scan at 1 kHz × 6 ranks: rate, jitter, interrupt cost; the consumer blocked (overrun, gap); the trigger timer
  stopped (stall supervision, restart); an ADC overrun with `OVRMOD = 0` counted.

Exercised on STM32H7 hardware with an earlier implementation (not this port): a timer-triggered multi-channel ADC scan
at 1 kHz into a two-half circular DMA buffer in D2 SRAM, half- and full-transfer interrupts on the stream above the
RTOS syscall limit and the ADC's own interrupts off; SPI full-duplex circular DMA at 2 160 half buffers per second for
about eleven minutes with zero DMA errors. Observed on STM32H7: an error flag left set by a stream interrupt that
bypasses the vendor handler re-asserts an interrupt above the RTOS limit for ever; a vendor configuration generator
rewrote DMA interrupt priorities when the project was regenerated; an ADC conversion count that differed from the DMA
ring's frame wrapped the ring on the wrong boundary. These are the reasons for rules 3 and 4 and for the conversion
count check.

## Not done

- Zero-copy `SpiDevice::Transfer(Lease&&, ...)`: `DmaTransfer` lends typed buffers, the SPI bus does not yet.
- `Frames > 2` (double-buffer mode), BDMA, MDMA, UART and I2C DMA (later phases of the DMA design).
- The DMA boundary files are exercised only by the four family firmware builds and the bench: the host tests link the
  model in their place. They are compiled over the HAL fakes (register declarations only) for the MISRA scan and
  clang-tidy, but their register sequences are not unit tested on host.
