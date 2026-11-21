// Boundary file (deviations/boundary-files.yml): the DMA engines behind sdk.hpp, by register: the
// stream (F4, H7) or channel (G4, L4) control, address and count registers, the flag and clear
// registers one bit at a time, the request routing (DMAMUX on G4, H7 and L4+, CHSEL on F4, CSELR on
// L4), the stream's NVIC line, and the Cortex-M7 data cache and MPU read-back. Each function
// converts, accesses and returns; every decision is in the platform-free src/dma.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>

#include "kb/dma/assignment.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "sdk.hpp"
#if defined(KB_PORT_STM32_G4)
#include "stm32g4xx_hal.h"
#elif defined(KB_PORT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(KB_PORT_STM32_F4)
#include "stm32f4xx_hal.h"
#elif defined(KB_PORT_STM32_L4)
#include "stm32l4xx_hal.h"
#else
#error "define one of KB_PORT_STM32_G4, KB_PORT_STM32_H7, KB_PORT_STM32_F4, KB_PORT_STM32_L4"
#endif

#if defined(DMA1_Stream0)
// The device's DMA kind selects preprocessor branches below: a macro by necessity.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define KB_PORT_STM32_DMA_STREAMS 1 // F4, H7: DMA_Stream_TypeDef, streams 0 to 7
#elif defined(DMA1_Channel1)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define KB_PORT_STM32_DMA_STREAMS 0 // G4, L4: DMA_Channel_TypeDef, channels 1 to 8
#else
#error "the device has no DMA1"
#endif

namespace kb::port::stm32::sdk {

namespace {
constexpr uint8_t kPerEngine = 8U;

#if KB_PORT_STM32_DMA_STREAMS
// --- F4, H7: streams
// -------------------------------------------------------------------------------
[[nodiscard]] DMA_Stream_TypeDef* StreamOf(uint8_t engine, uint8_t stream) noexcept {
  const std::array<DMA_Stream_TypeDef*, 16> streams{
      DMA1_Stream0, DMA1_Stream1, DMA1_Stream2, DMA1_Stream3, DMA1_Stream4, DMA1_Stream5,
      DMA1_Stream6, DMA1_Stream7, DMA2_Stream0, DMA2_Stream1, DMA2_Stream2, DMA2_Stream3,
      DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7};
  const uint8_t index = DmaIndex(engine, stream);
  return (index < 16U) ? streams[index] : nullptr;
}

[[nodiscard]] DMA_TypeDef* EngineOf(uint8_t engine) noexcept {
  return (engine == kEngineDma2) ? DMA2 : DMA1;
}

// The bit offset of stream s in LISR/HISR (and LIFCR/HIFCR): 0, 6, 16, 22 for streams 0 to 3 (4 to
// 7).
[[nodiscard]] uint32_t FlagShift(uint8_t stream) noexcept {
  constexpr std::array<uint32_t, 4> kShift{0U, 6U, 16U, 22U};
  return kShift[stream & 3U];
}

// The kDmaFlag* bits and their bit inside a stream's six-bit group of LISR/HISR.
struct FlagBit {
  uint32_t flag;
  uint32_t bit;
};
constexpr std::array<FlagBit, 5> kFlagBits{{{.flag = kDmaFlagFifoError, .bit = 0U},
                                            {.flag = kDmaFlagDirectError, .bit = 2U},
                                            {.flag = kDmaFlagTransferError, .bit = 3U},
                                            {.flag = kDmaFlagHalf, .bit = 4U},
                                            {.flag = kDmaFlagComplete, .bit = 5U}}};

[[nodiscard]] IRQn_Type IrqOf(uint8_t engine, uint8_t stream) noexcept {
  constexpr std::array<IRQn_Type, 16> kIrqs{
      DMA1_Stream0_IRQn, DMA1_Stream1_IRQn, DMA1_Stream2_IRQn, DMA1_Stream3_IRQn,
      DMA1_Stream4_IRQn, DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, DMA1_Stream7_IRQn,
      DMA2_Stream0_IRQn, DMA2_Stream1_IRQn, DMA2_Stream2_IRQn, DMA2_Stream3_IRQn,
      DMA2_Stream4_IRQn, DMA2_Stream5_IRQn, DMA2_Stream6_IRQn, DMA2_Stream7_IRQn};
  return kIrqs[DmaIndex(engine, stream) & 15U];
}
#else
// --- G4, L4: channels
// ------------------------------------------------------------------------------
#if defined(DMA1_Channel8)
constexpr uint8_t kChannels = 8U;
#else
constexpr uint8_t kChannels = 7U;
#endif

[[nodiscard]] DMA_Channel_TypeDef* StreamOf(uint8_t engine, uint8_t stream) noexcept {
  const std::array<DMA_Channel_TypeDef*, 16> channels{
      DMA1_Channel1, DMA1_Channel2, DMA1_Channel3, DMA1_Channel4, DMA1_Channel5,
      DMA1_Channel6, DMA1_Channel7,
#if defined(DMA1_Channel8)
      DMA1_Channel8,
#else
      nullptr,
#endif
      DMA2_Channel1, DMA2_Channel2, DMA2_Channel3, DMA2_Channel4, DMA2_Channel5,
#if defined(DMA2_Channel6)
      DMA2_Channel6,
#else
      nullptr,
#endif
#if defined(DMA2_Channel7)
      DMA2_Channel7,
#else
      nullptr,
#endif
#if defined(DMA2_Channel8)
      DMA2_Channel8};
#else
      nullptr};
#endif
  const uint8_t index = DmaIndex(engine, stream);
  return (index < 16U) ? channels[index] : nullptr;
}

[[nodiscard]] DMA_TypeDef* EngineOf(uint8_t engine) noexcept {
  return (engine == kEngineDma2) ? DMA2 : DMA1;
}

// The bit offset of channel c in ISR/IFCR: 4 * (c - 1); GIF +0, TCIF +1, HTIF +2, TEIF +3.
[[nodiscard]] uint32_t FlagShift(uint8_t stream) noexcept {
  return 4U * (static_cast<uint32_t>(stream) - 1U);
}

struct FlagBit {
  uint32_t flag;
  uint32_t bit;
};
constexpr std::array<FlagBit, 3> kFlagBits{{{.flag = kDmaFlagComplete, .bit = 1U},
                                            {.flag = kDmaFlagHalf, .bit = 2U},
                                            {.flag = kDmaFlagTransferError, .bit = 3U}}};

[[nodiscard]] IRQn_Type IrqOf(uint8_t engine, uint8_t stream) noexcept {
  constexpr std::array<IRQn_Type, 16> kIrqs{
      DMA1_Channel1_IRQn, DMA1_Channel2_IRQn, DMA1_Channel3_IRQn, DMA1_Channel4_IRQn,
      DMA1_Channel5_IRQn, DMA1_Channel6_IRQn, DMA1_Channel7_IRQn,
#if defined(DMA1_Channel8)
      DMA1_Channel8_IRQn,
#else
      DMA1_Channel1_IRQn,
#endif
      DMA2_Channel1_IRQn, DMA2_Channel2_IRQn, DMA2_Channel3_IRQn, DMA2_Channel4_IRQn,
      DMA2_Channel5_IRQn,
#if defined(DMA2_Channel6)
      DMA2_Channel6_IRQn,
#else
      DMA2_Channel1_IRQn,
#endif
#if defined(DMA2_Channel7)
      DMA2_Channel7_IRQn,
#else
      DMA2_Channel1_IRQn,
#endif
#if defined(DMA2_Channel8)
      DMA2_Channel8_IRQn
#else
      DMA2_Channel1_IRQn
#endif
  };
  return kIrqs[DmaIndex(engine, stream) & 15U];
}
#endif

void EngineClock(uint8_t engine) noexcept {
#if defined(DMAMUX1) && !defined(STM32H7)
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
#endif
  if (engine == kEngineDma2) {
    __HAL_RCC_DMA2_CLK_ENABLE();
  } else {
    __HAL_RCC_DMA1_CLK_ENABLE();
  }
}

[[nodiscard]] uint32_t SizeBits(DmaWidth width) noexcept {
  return static_cast<uint32_t>(width); // 00 byte, 01 half-word, 10 word on every family
}

void Route(uint8_t engine, uint8_t stream, uint16_t request) noexcept {
#if defined(DMAMUX1)
  // DMAMUX1 channel n serves DMA1 stream/channel n (n from 0), then DMA2 after DMA1's last one.
#if KB_PORT_STM32_DMA_STREAMS
  const uint32_t channel = DmaIndex(engine, stream);
#else
  const uint32_t channel = (engine == kEngineDma2) ? (kChannels + stream - 1U) : (stream - 1U);
#endif
  const std::array<DMAMUX_Channel_TypeDef*, 16> muxes{
      DMAMUX1_Channel0,  DMAMUX1_Channel1,  DMAMUX1_Channel2,  DMAMUX1_Channel3,
      DMAMUX1_Channel4,  DMAMUX1_Channel5,  DMAMUX1_Channel6,  DMAMUX1_Channel7,
      DMAMUX1_Channel8,  DMAMUX1_Channel9,  DMAMUX1_Channel10, DMAMUX1_Channel11,
#if defined(DMAMUX1_Channel12)
      DMAMUX1_Channel12, DMAMUX1_Channel13, DMAMUX1_Channel14, DMAMUX1_Channel15};
#else
      nullptr,          nullptr,          nullptr,           nullptr};
#endif
  DMAMUX_Channel_TypeDef* const ch = (channel < 16U) ? muxes[channel] : nullptr;
  if (ch != nullptr) {
    ch->CCR = (static_cast<uint32_t>(request) << DMAMUX_CxCR_DMAREQ_ID_Pos) & DMAMUX_CxCR_DMAREQ_ID;
  }
#elif KB_PORT_STM32_DMA_STREAMS
  // F4: the channel selection in the stream's CR (written by DmaConfigure).
  static_cast<void>(engine);
  static_cast<void>(stream);
  static_cast<void>(request);
#else
  // L4: four CSELR bits per channel.
  DMA_Request_TypeDef* const sel = (engine == kEngineDma2) ? DMA2_CSELR : DMA1_CSELR;
  const uint32_t shift = 4U * (static_cast<uint32_t>(stream) - 1U);
  sel->CSELR = (sel->CSELR & ~(0xFU << shift)) | ((static_cast<uint32_t>(request) & 0xFU) << shift);
#endif
}
} // namespace

uint8_t DmaIndex(uint8_t engine, uint8_t stream) noexcept {
  uint8_t index = kDmaNoIndex;
  const bool engine_ok = (engine == kEngineDma1) || (engine == kEngineDma2);
#if KB_PORT_STM32_DMA_STREAMS
  if (engine_ok && (stream < kPerEngine)) {
    index = static_cast<uint8_t>(((engine - 1U) * kPerEngine) + stream);
  }
#else
  if (engine_ok && (stream >= 1U) && (stream <= kChannels)) {
    index = static_cast<uint8_t>(((engine - 1U) * kPerEngine) + (stream - 1U));
  }
#endif
  return index;
}

bool DmaRequestValid(uint16_t request) noexcept {
#if defined(DMAMUX1)
  return request <= (DMAMUX_CxCR_DMAREQ_ID >> DMAMUX_CxCR_DMAREQ_ID_Pos);
#elif KB_PORT_STM32_DMA_STREAMS
  return request <= 7U; // CHSEL
#else
  return request <= 15U; // CSELR
#endif
}

uint32_t DmaConfigure(const DmaProgram& program) noexcept {
  const kb::dma::StreamAssignment& a = program.assignment;
  uint32_t code = port_code::kInvalidArgument;
  auto* const s = StreamOf(a.engine, a.stream);
  if (s != nullptr) {
    EngineClock(a.engine);
    Route(a.engine, a.stream, a.request);
    const uint32_t size = SizeBits(program.peripheral.width);
    const bool to_device = program.peripheral.dir == kb::dma::Direction::ToDevice;
    const bool circular = a.mode == kb::dma::ChannelMode::Circular;
    const auto level = static_cast<uint32_t>(a.priority);
#if KB_PORT_STM32_DMA_STREAMS
    s->CR = 0U; // EN 0 (checked free by the caller)
    s->PAR = static_cast<uint32_t>(program.peripheral.address);
    s->FCR = 0U; // direct mode: FIFO off, no FIFO error interrupt
    uint32_t cr = DMA_SxCR_MINC | (size << DMA_SxCR_PSIZE_Pos) | (size << DMA_SxCR_MSIZE_Pos) |
                  (level << DMA_SxCR_PL_Pos);
    cr |= to_device ? DMA_SxCR_DIR_0 : 0U;
    cr |= circular ? DMA_SxCR_CIRC : 0U;
#if defined(DMA_SxCR_CHSEL_Pos)
    cr |= (static_cast<uint32_t>(a.request) << DMA_SxCR_CHSEL_Pos) & DMA_SxCR_CHSEL;
#endif
    s->CR = cr;
#else
    s->CCR = 0U;
    s->CPAR = static_cast<uint32_t>(program.peripheral.address);
    uint32_t ccr = DMA_CCR_MINC | (size << DMA_CCR_PSIZE_Pos) | (size << DMA_CCR_MSIZE_Pos) |
                   (level << DMA_CCR_PL_Pos);
    ccr |= to_device ? DMA_CCR_DIR : 0U;
    ccr |= circular ? DMA_CCR_CIRC : 0U;
    s->CCR = ccr;
#endif
    code = hal::kOk;
  }
  return code;
}

void DmaArm(uint8_t engine, uint8_t stream, std::uintptr_t memory, uint32_t items,
            uint32_t irq) noexcept {
  auto* const s = StreamOf(engine, stream);
  if (s != nullptr) {
#if KB_PORT_STM32_DMA_STREAMS
    s->M0AR = static_cast<uint32_t>(memory);
    s->NDTR = items;
    uint32_t cr = s->CR & ~(DMA_SxCR_TCIE | DMA_SxCR_HTIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    cr |= ((irq & kDmaFlagComplete) != 0U) ? DMA_SxCR_TCIE : 0U;
    cr |= ((irq & kDmaFlagHalf) != 0U) ? DMA_SxCR_HTIE : 0U;
    cr |= ((irq & kDmaFlagTransferError) != 0U) ? DMA_SxCR_TEIE : 0U;
    cr |= ((irq & kDmaFlagDirectError) != 0U) ? DMA_SxCR_DMEIE : 0U;
    s->CR = cr;
    s->CR = cr | DMA_SxCR_EN;
#else
    s->CMAR = static_cast<uint32_t>(memory);
    s->CNDTR = items;
    uint32_t ccr = s->CCR & ~(DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE);
    ccr |= ((irq & kDmaFlagComplete) != 0U) ? DMA_CCR_TCIE : 0U;
    ccr |= ((irq & kDmaFlagHalf) != 0U) ? DMA_CCR_HTIE : 0U;
    ccr |= ((irq & kDmaFlagTransferError) != 0U) ? DMA_CCR_TEIE : 0U;
    s->CCR = ccr;
    s->CCR = ccr | DMA_CCR_EN;
#endif
  }
}

bool DmaEnabled(uint8_t engine, uint8_t stream) noexcept {
  const auto* const s = StreamOf(engine, stream);
#if KB_PORT_STM32_DMA_STREAMS
  return (s != nullptr) && ((s->CR & DMA_SxCR_EN) != 0U);
#else
  return (s != nullptr) && ((s->CCR & DMA_CCR_EN) != 0U);
#endif
}

void DmaDisable(uint8_t engine, uint8_t stream) noexcept {
  auto* const s = StreamOf(engine, stream);
  if (s != nullptr) {
#if KB_PORT_STM32_DMA_STREAMS
    s->CR &= ~DMA_SxCR_EN;
#else
    s->CCR &= ~DMA_CCR_EN;
#endif
  }
}

void DmaReset(uint8_t engine, uint8_t stream) noexcept {
  auto* const s = StreamOf(engine, stream);
  if (s != nullptr) {
#if KB_PORT_STM32_DMA_STREAMS
    s->CR = 0U;
    s->NDTR = 0U;
#else
    s->CCR = 0U;
    s->CNDTR = 0U;
#endif
    for (const FlagBit& f : kFlagBits) {
      DmaClearFlag(engine, stream, f.flag);
    }
  }
}

uint32_t DmaRemaining(uint8_t engine, uint8_t stream) noexcept {
  const auto* const s = StreamOf(engine, stream);
#if KB_PORT_STM32_DMA_STREAMS
  return (s != nullptr) ? (s->NDTR & 0xFFFFU) : 0U;
#else
  return (s != nullptr) ? (s->CNDTR & 0xFFFFU) : 0U;
#endif
}

uint32_t DmaFlags(uint8_t engine, uint8_t stream) noexcept {
  uint32_t flags = 0U;
  if (StreamOf(engine, stream) != nullptr) {
    const DMA_TypeDef* const d = EngineOf(engine);
#if KB_PORT_STM32_DMA_STREAMS
    const uint32_t reg = (stream < 4U) ? d->LISR : d->HISR;
#else
    const uint32_t reg = d->ISR;
#endif
    const uint32_t group = reg >> FlagShift(stream);
    for (const FlagBit& f : kFlagBits) {
      flags |= ((group & (1U << f.bit)) != 0U) ? f.flag : 0U;
    }
  }
  return flags;
}

void DmaClearFlag(uint8_t engine, uint8_t stream, uint32_t flag) noexcept {
  if (StreamOf(engine, stream) != nullptr) {
    DMA_TypeDef* const d = EngineOf(engine);
    for (const FlagBit& f : kFlagBits) {
      if (f.flag == flag) {
        const uint32_t bit = 1U << (FlagShift(stream) + f.bit);
#if KB_PORT_STM32_DMA_STREAMS
        if (stream < 4U) {
          d->LIFCR = bit;
        } else {
          d->HIFCR = bit;
        }
#else
        d->IFCR = bit; // the flag's own clear bit, never CGIF alone
#endif
      }
    }
  }
}

void DmaIrqPriority(uint8_t engine, uint8_t stream, uint8_t priority) noexcept {
  HAL_NVIC_SetPriority(IrqOf(engine, stream), priority, 0U);
}

uint8_t DmaIrqPriorityRead(uint8_t engine, uint8_t stream) noexcept {
  uint32_t preempt = 0U;
  uint32_t sub = 0U;
  HAL_NVIC_GetPriority(IrqOf(engine, stream), HAL_NVIC_GetPriorityGrouping(), &preempt, &sub);
  return static_cast<uint8_t>(preempt);
}

void DmaIrqEnable(uint8_t engine, uint8_t stream, bool on) noexcept {
  if (DmaIndex(engine, stream) != kDmaNoIndex) {
    if (on) {
      HAL_NVIC_EnableIRQ(IrqOf(engine, stream));
    } else {
      HAL_NVIC_DisableIRQ(IrqOf(engine, stream));
    }
  }
}

uint32_t DmaStopBoundCycles() noexcept {
  // Two data items plus the bus latency are well under 10 us on every family; never below 1024.
  const uint32_t ten_us = SystemCoreClock / 100000U;
  return (ten_us > 1024U) ? ten_us : 1024U;
}

// --- data cache and MPU
// ----------------------------------------------------------------------------

// By line, as CMSIS's SCB_*DCache_by_Addr do: the address of each 32-byte line into DCCMVAC (clean)
// or DCIMVAC (invalidate), then DSB and ISB. kb::dma::Buffer hands only whole lines it owns.
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
namespace {
constexpr std::uintptr_t kLine = 32U;
} // namespace
#endif

void CacheClean(std::uintptr_t address, std::size_t bytes) noexcept {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if ((bytes != 0U) && ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)) {
    __DSB();
    for (std::uintptr_t a = address & ~(kLine - 1U); a < (address + bytes); a += kLine) {
      SCB->DCCMVAC = static_cast<uint32_t>(a);
    }
    __DSB();
    __ISB();
  }
#else
  static_cast<void>(address);
  static_cast<void>(bytes);
#endif
}

void CacheInvalidate(std::uintptr_t address, std::size_t bytes) noexcept {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if ((bytes != 0U) && ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)) {
    __DSB();
    for (std::uintptr_t a = address & ~(kLine - 1U); a < (address + bytes); a += kLine) {
      SCB->DCIMVAC = static_cast<uint32_t>(a);
    }
    __DSB();
    __ISB();
  }
#else
  static_cast<void>(address);
  static_cast<void>(bytes);
#endif
}

void DataBarrier() noexcept {
  __DSB();
}

bool DataCacheOn() noexcept {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  return (SCB->CCR & SCB_CCR_DC_Msk) != 0U;
#else
  return false;
#endif
}

bool MpuUncached(std::uintptr_t base, std::size_t size) noexcept {
  bool uncached = false;
#if defined(__MPU_PRESENT) && (__MPU_PRESENT == 1U)
  if ((MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0U) {
    const uint32_t regions = (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
    // The highest-numbered enabled region that covers the window decides (ARMv7-M overlap rule).
    for (uint32_t r = 0U; r < regions; ++r) {
      MPU->RNR = r;
      const uint32_t rasr = MPU->RASR;
      const uint32_t rbar = MPU->RBAR & MPU_RBAR_ADDR_Msk;
      const uint32_t log2 = ((rasr & MPU_RASR_SIZE_Msk) >> MPU_RASR_SIZE_Pos) + 1U;
      const uint64_t span = uint64_t{1U} << log2;
      const bool enabled = (rasr & MPU_RASR_ENABLE_Msk) != 0U;
      const bool whole = (rasr & MPU_RASR_SRD_Msk) == 0U; // no disabled sub-region
      const bool covers = (base >= rbar) && ((uint64_t{base} + size) <= (uint64_t{rbar} + span));
      if (enabled && whole && covers) {
        const uint32_t tex = (rasr & MPU_RASR_TEX_Msk) >> MPU_RASR_TEX_Pos;
        const bool c = (rasr & MPU_RASR_C_Msk) != 0U;
        // Normal non-cacheable (TEX 001, C 0, B 0), or device / strongly ordered (TEX 000, C 0).
        uncached = !c && ((tex == 1U) || (tex == 0U));
      }
    }
  }
#else
  static_cast<void>(base);
  static_cast<void>(size);
#endif
  return uncached;
}

} // namespace kb::port::stm32::sdk
