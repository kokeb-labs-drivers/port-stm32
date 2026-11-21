// The DMA boundary of the port on host (the link seam of src/boundary/dma.cpp, spi_dma.cpp and
// adc_stream.cpp): every sdk:: DMA, SPI-by-DMA and ADC-stream call drives kb-core's host model
// (kb::dma::host::Machine, DmaEngine, HwRing) instead of registers. This header is the tests'
// control of it. Test code, outside the MISRA claim. SPDX-License-Identifier: MIT Copyright (c)
// 2026 The port-stm32 authors
#ifndef KB_PORT_STM32_TESTS_DMA_MODEL_HPP
#define KB_PORT_STM32_TESTS_DMA_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "kb/dma/host/engine.hpp"
#include "kb/dma/host/machine.hpp"
#include "kb/dma/host/stream_rig.hpp"

namespace dma_model {

/** @brief Deliberate defects of the boundary, to show the conformance cases see through the port.
 */
struct BoundaryFaults {
  bool clean_noop{false};      ///< CacheClean does nothing (T1 must fail).
  bool invalidate_noop{false}; ///< CacheInvalidate does nothing (T2 must fail).
  bool en_reads_zero{false};   ///< EN reads 0 at once after a disable (T7 must fail).
};

/** @brief Everything back to nothing bound, no knobs (call with fake::Reset()). */
void Reset();
/** @brief The machine the cache calls go to (nullptr: cache calls do nothing). */
void BindMachine(kb::dma::host::Machine* machine);
/** @brief Stream `stream` of `engine` is moved by `dma` (several streams may share one engine: the
 * one armed last owns its flags); a receive writes `seed + i`. */
void BindEngine(uint8_t engine, uint8_t stream, kb::dma::host::DmaEngine* dma, uint8_t seed = 0U);
/** @brief Stream `stream` of `engine` is the circular ADC ring `ring` on `machine`. */
void BindRing(uint8_t engine, uint8_t stream, kb::dma::host::HwRing* ring,
              kb::dma::host::Machine* machine);
/** @brief The receive seed of a bound stream. */
void SetSeed(uint8_t engine, uint8_t stream, uint8_t seed);
/** @brief The boundary's defects. */
void SetFaults(const BoundaryFaults& faults);
/** @brief The NVIC priority reads back as `value` (-1: as written). */
void ForcePriorityReadback(int value);
/** @brief The D-cache is on (`on`) and the MPU makes the DMA windows uncached (`mpu`). */
void SetCache(bool on, bool mpu);
/** @brief The stream reads EN = 1 before the port starts it (someone else's). */
void SetForeignEnabled(uint8_t engine, uint8_t stream, bool on);

// --- SPI
// ------------------------------------------------------------------------------------------
/** @brief SpiDmaGo runs the bound engines to their end and serves their interrupts (default on). */
void SetSpiRuns(bool on);
/** @brief The SPI error flags SpiDmaErrors reports (sdk::kSpiDma*). */
void SetSpiErrors(uint32_t errors);
/** @brief SpiDmaPrepare fails with `code` (0: works). */
void FailSpiPrepare(uint32_t code);

// --- ADC stream
// -----------------------------------------------------------------------------------
/** @brief The conversion count AdcStreamConversions reads back (-1: the channels opened). */
void ForceConversions(int count);
/** @brief AdcStreamOpen / AdcStreamStart fail with `code` (0: works). */
void FailAdcOpen(uint32_t code);
void FailAdcStart(uint32_t code);
/** @brief Times AdcStreamStart and AdcStreamOpen were called. */
[[nodiscard]] uint32_t AdcStarts();
[[nodiscard]] uint32_t AdcOpens();

// --- NVIC and inspection
// --------------------------------------------------------------------------
/** @brief The NVIC: every enabled, bound stream whose pending flags include an enabled source (or
 * an error) has its vector run once (HandleDmaIrq). The number of vectors run. */
uint32_t Nvic();
/** @brief One engine cycle of every bound engine, then the NVIC. */
void Tick();
[[nodiscard]] bool IrqEnabled(uint8_t engine, uint8_t stream);
[[nodiscard]] uint8_t PriorityWritten(uint8_t engine, uint8_t stream);
/** @brief The interrupt sources the port enabled at the last arm (kDmaFlag* bits). */
[[nodiscard]] uint32_t ArmedIrqs(uint8_t engine, uint8_t stream);
/** @brief The request routed to the stream, and whether its configuration is circular. */
[[nodiscard]] uint16_t Request(uint8_t engine, uint8_t stream);
[[nodiscard]] bool Circular(uint8_t engine, uint8_t stream);
/** @brief Single-flag clears per kDmaFlag* bit of a stream since Reset(). */
[[nodiscard]] uint32_t Clears(uint8_t engine, uint8_t stream, uint32_t flag);
/** @brief Raises `flags` (kDmaFlag* bits) on a stream directly (nothing in flight). */
void RaiseFlags(uint8_t engine, uint8_t stream, uint32_t flags);
/** @brief The order of the boundary calls of the SPI and ADC paths ("prepare", "arm:1:0", "go",
 * ...). */
[[nodiscard]] const std::vector<std::string>& Log();
/** @brief Index of the first log entry equal to `entry` (Log().size() when absent). */
[[nodiscard]] std::size_t Find(const std::string& entry);

} // namespace dma_model

#endif // KB_PORT_STM32_TESTS_DMA_MODEL_HPP
