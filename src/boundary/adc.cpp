// Boundary file (deviations/boundary-files.yml): the ADC calls of the STM32 HAL behind sdk.hpp, the
// HAL's ADC completion callbacks and the ADC interrupt entry point. Each function converts its
// arguments, calls the HAL and returns VendorCode(HAL status, the handle's ErrorCode); the handles
// live in the table below. Every decision is in the platform-free src/adc.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
// Slot pointers shared with the interrupts are std::atomic.
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/irq.hpp"
#include "sdk.hpp"
#include "state.hpp"
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

namespace kb::port::stm32::sdk {

namespace {
[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// --- ADC -----------------------------------------------------------------------------------------
struct AdcSlot {
  ADC_HandleTypeDef handle{};
  std::atomic<Completion*> conversion{nullptr}; ///< The instance's completion while it is open.
};

std::array<AdcSlot, kAdcs>& Adcs() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<AdcSlot, kAdcs> adcs{};
  return adcs;
}

[[nodiscard]] IRQn_Type AdcIrq(uint8_t instance) noexcept {
#if defined(STM32F4)
  static_cast<void>(instance);
  return ADC_IRQn; // ADC1 to ADC3 share one vector
#elif defined(STM32H7)
  IRQn_Type irq = ADC_IRQn; // ADC1 and ADC2
#if defined(ADC3)
  if (instance == 3U) {
    irq = ADC3_IRQn;
  }
#endif
  return irq;
#elif defined(STM32G4)
  IRQn_Type irq = ADC1_2_IRQn;
#if defined(ADC3)
  if (instance == 3U) {
    irq = ADC3_IRQn;
  }
#endif
#if defined(ADC4)
  if (instance == 4U) {
    irq = ADC4_IRQn;
  }
#endif
#if defined(ADC5)
  if (instance == 5U) {
    irq = ADC5_IRQn;
  }
#endif
  return irq;
#else
#if defined(ADC2)
  IRQn_Type irq = ADC1_2_IRQn;
#else
  IRQn_Type irq = ADC1_IRQn;
#endif
#if defined(ADC3)
  if (instance == 3U) {
    irq = ADC3_IRQn;
  }
#endif
  return irq;
#endif
}

[[nodiscard]] ADC_TypeDef* AdcOf(uint8_t instance) noexcept {
  ADC_TypeDef* a = nullptr;
  if (instance == 1U) {
    a = ADC1;
#if defined(ADC2)
  } else if (instance == 2U) {
    a = ADC2;
#endif
#if defined(ADC3)
  } else if (instance == 3U) {
    a = ADC3;
#endif
#if defined(ADC4)
  } else if (instance == 4U) {
    a = ADC4;
#endif
#if defined(ADC5)
  } else if (instance == 5U) {
    a = ADC5;
#endif
  } else {
    a = nullptr;
  }
  return a;
}

void AdcClock(uint8_t instance) noexcept {
#if defined(STM32F4)
  if (instance == 1U) {
    __HAL_RCC_ADC1_CLK_ENABLE();
#if defined(ADC2)
  } else if (instance == 2U) {
    __HAL_RCC_ADC2_CLK_ENABLE();
#endif
#if defined(ADC3)
  } else if (instance == 3U) {
    __HAL_RCC_ADC3_CLK_ENABLE();
#endif
  } else {
    // no other ADC on F4
  }
#elif defined(STM32G4)
  if (instance <= 2U) {
    __HAL_RCC_ADC12_CLK_ENABLE();
  } else {
#if defined(ADC345_COMMON)
    __HAL_RCC_ADC345_CLK_ENABLE();
#endif
  }
#elif defined(STM32H7)
  if (instance <= 2U) {
    __HAL_RCC_ADC12_CLK_ENABLE();
  } else {
    __HAL_RCC_ADC3_CLK_ENABLE();
  }
#else
  static_cast<void>(instance);
  __HAL_RCC_ADC_CLK_ENABLE();
#endif
}

void AdcInitFields(ADC_HandleTypeDef& h) noexcept {
#if !defined(STM32H7)
  h.Init.DataAlign = ADC_DATAALIGN_RIGHT; // H7: right aligned unless LeftBitShift says otherwise
#endif
  h.Init.ContinuousConvMode = DISABLE;
  h.Init.DiscontinuousConvMode = DISABLE;
  h.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  h.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  h.Init.NbrOfConversion = 1U;
  h.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
#if defined(STM32F4)
  h.Init.Resolution = ADC_RESOLUTION_12B;
  h.Init.ScanConvMode = DISABLE;
  h.Init.DMAContinuousRequests = DISABLE;
#elif defined(STM32H7)
  h.Init.Resolution = ADC_RESOLUTION_16B;
  h.Init.ScanConvMode = ADC_SCAN_DISABLE;
  h.Init.LowPowerAutoWait = DISABLE;
  h.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  h.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  h.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  h.Init.OversamplingMode = DISABLE;
#else
  h.Init.Resolution = ADC_RESOLUTION_12B;
  h.Init.ScanConvMode = ADC_SCAN_DISABLE;
  h.Init.LowPowerAutoWait = DISABLE;
  h.Init.DMAContinuousRequests = DISABLE;
  h.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  h.Init.OversamplingMode = DISABLE;
#endif
}

[[nodiscard]] uint32_t AdcChannelOf(uint8_t channel) noexcept {
#if defined(STM32F4)
  return channel; // ADC_CHANNEL_<n> is n on F4
#else
  return __LL_ADC_DECIMAL_NB_TO_CHANNEL(channel);
#endif
}

[[nodiscard]] uint32_t SamplingOf(AdcSampling sampling) noexcept {
#if defined(STM32F4)
  constexpr std::array<uint32_t, 3> kTimes{ADC_SAMPLETIME_15CYCLES, ADC_SAMPLETIME_84CYCLES,
                                           ADC_SAMPLETIME_480CYCLES};
#elif defined(STM32H7)
  constexpr std::array<uint32_t, 3> kTimes{ADC_SAMPLETIME_8CYCLES_5, ADC_SAMPLETIME_64CYCLES_5,
                                           ADC_SAMPLETIME_810CYCLES_5};
#else
  constexpr std::array<uint32_t, 3> kTimes{ADC_SAMPLETIME_6CYCLES_5, ADC_SAMPLETIME_47CYCLES_5,
                                           ADC_SAMPLETIME_640CYCLES_5};
#endif
  return kTimes[static_cast<std::size_t>(sampling)];
}

[[nodiscard]] uint32_t AdcCalibrate(ADC_HandleTypeDef& h) noexcept {
#if defined(STM32F4)
  static_cast<void>(h); // the F4 ADC has no calibration
  return hal::kOk;
#elif defined(STM32H7)
  const HAL_StatusTypeDef s =
      HAL_ADCEx_Calibration_Start(&h, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED);
  return Code(s, h.ErrorCode);
#else
  const HAL_StatusTypeDef s = HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED);
  return Code(s, h.ErrorCode);
#endif
}

[[nodiscard]] uint32_t AdcStartOne(ADC_HandleTypeDef& h, uint8_t channel,
                                   AdcSampling sampling) noexcept {
  ADC_ChannelConfTypeDef c{};
  c.Channel = AdcChannelOf(channel);
  c.SamplingTime = SamplingOf(sampling);
#if defined(STM32F4)
  c.Rank = 1U;
#else
  c.Rank = ADC_REGULAR_RANK_1;
  c.SingleDiff = ADC_SINGLE_ENDED;
  c.OffsetNumber = ADC_OFFSET_NONE;
#endif
  HAL_StatusTypeDef s = HAL_ADC_ConfigChannel(&h, &c);
  if (s == HAL_OK) {
    s = HAL_ADC_Start_IT(&h); // ends in HAL_ADC_ConvCpltCallback or HAL_ADC_ErrorCallback
  }
  // ErrorCode only when the start failed: the conversion may already have ended.
  return (s == HAL_OK) ? hal::kOk : Code(s, h.ErrorCode);
}
} // namespace

// --- ADC -----------------------------------------------------------------------------------------
bool AdcExists(uint8_t instance, uint8_t channel) noexcept {
#if defined(STM32F4)
  constexpr uint8_t kMaxChannel = 18U; // IN0 to IN15, temperature, VREFINT, VBAT
#else
  constexpr uint8_t kMaxChannel = 19U;
#endif
  return (instance < kAdcs) && (AdcOf(instance) != nullptr) && (channel <= kMaxChannel);
}

uint8_t AdcBits(uint8_t instance) noexcept {
  static_cast<void>(instance);
#if defined(STM32H7)
  return 16U;
#else
  return 12U;
#endif
}

namespace {
// The ADC clock: the synchronous HCLK / 4 (PCLK2 / 4 on F4); on H7, when HCLK / 4 is above the
// limit, the asynchronous kernel clock (RCC ADCSEL) with the smallest divider that meets it.
[[nodiscard]] uint32_t AdcClockChoice(uint32_t& prescaler) noexcept {
  prescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
#if defined(STM32F4)
  const uint32_t sync_hz = HAL_RCC_GetPCLK2Freq() / 4U;
#else
  const uint32_t sync_hz = HAL_RCC_GetHCLKFreq() / 4U;
#endif
  uint32_t code = (sync_hz == 0U) ? port_code::kNoClock : hal::kOk;
#if defined(STM32H7)
  if ((code == hal::kOk) && (sync_hz > kH7AdcMaxHz)) {
    constexpr std::array<uint32_t, kAdcAsyncDividers.size()> kAsync{
        ADC_CLOCK_ASYNC_DIV1,  ADC_CLOCK_ASYNC_DIV2,   ADC_CLOCK_ASYNC_DIV4,
        ADC_CLOCK_ASYNC_DIV6,  ADC_CLOCK_ASYNC_DIV8,   ADC_CLOCK_ASYNC_DIV10,
        ADC_CLOCK_ASYNC_DIV12, ADC_CLOCK_ASYNC_DIV16,  ADC_CLOCK_ASYNC_DIV32,
        ADC_CLOCK_ASYNC_DIV64, ADC_CLOCK_ASYNC_DIV128, ADC_CLOCK_ASYNC_DIV256};
    const uint32_t kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC);
    bool ok = false;
    const std::size_t index = AdcAsyncDividerIndex(kernel_hz, kH7AdcMaxHz, ok);
    prescaler = kAsync[index];
    code = ok ? hal::kOk : ((kernel_hz == 0U) ? port_code::kNoClock : port_code::kUnsupported);
  }
#endif
  return code;
}
} // namespace

uint32_t AdcClockCheck() noexcept {
  uint32_t prescaler = 0U;
  return AdcClockChoice(prescaler);
}

uint32_t AdcOpen(uint8_t instance, Completion& conversion, uint8_t priority) noexcept {
  AdcSlot& slot = Adcs()[instance];
  AdcClock(instance);
  slot.handle.Instance = AdcOf(instance);
  AdcInitFields(slot.handle);
  uint32_t prescaler = 0U;
  static_cast<void>(AdcClockChoice(prescaler)); // checked by AdcClockCheck() before the open
  slot.handle.Init.ClockPrescaler = prescaler;  // the HAL sets the H7 BOOST mode from it
  const HAL_StatusTypeDef s = HAL_ADC_Init(&slot.handle);
  uint32_t code = Code(s, slot.handle.ErrorCode);
  if (code == hal::kOk) {
    code = AdcCalibrate(slot.handle);
  }
  if (code == hal::kOk) {
    slot.conversion.store(&conversion, std::memory_order_release);
    HAL_NVIC_SetPriority(AdcIrq(instance), priority, 0U);
    HAL_NVIC_EnableIRQ(
        AdcIrq(instance)); // shared vectors stay enabled: the handler checks the slot
  }
  return code;
}

uint32_t AdcClose(uint8_t instance) noexcept {
  AdcSlot& slot = Adcs()[instance];
  slot.conversion.store(nullptr, std::memory_order_release);
  const HAL_StatusTypeDef s = HAL_ADC_DeInit(&slot.handle);
  return Code(s, slot.handle.ErrorCode);
}

uint32_t AdcStart(uint8_t instance, uint8_t channel, AdcSampling sampling) noexcept {
  return AdcStartOne(Adcs()[instance].handle, channel, sampling);
}

uint32_t AdcStop(uint8_t instance) noexcept {
  ADC_HandleTypeDef& h = Adcs()[instance].handle;
  const HAL_StatusTypeDef s = HAL_ADC_Stop_IT(&h);
  return Code(s, h.ErrorCode);
}

namespace {
// The slot of the ADC handle `h`, or nullptr.
[[nodiscard]] AdcSlot* AdcSlotOf(const ADC_HandleTypeDef* h) noexcept {
  AdcSlot* found = nullptr;
  for (AdcSlot& slot : Adcs()) {
    if (&slot.handle == h) {
      found = &slot;
    }
  }
  return found;
}
} // namespace

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {
// The interrupt entry point (irq.hpp): the HAL's ADC handler of each open instance on the vector.
void HandleAdcIrq(uint8_t first, uint8_t last) noexcept {
  bool any = false;
  for (uint8_t i = first; (i <= last) && (i < kAdcs); ++i) {
    sdk::AdcSlot& slot = sdk::Adcs()[i];
    if (slot.conversion.load(std::memory_order_acquire) != nullptr) {
      any = true;
      HAL_ADC_IRQHandler(&slot.handle);
    }
  }
  if (!any) {
    HAL_NVIC_DisableIRQ(sdk::AdcIrq(first)); // no instance of the vector is the port's
  }
}
} // namespace kb::port::stm32

// The HAL's ADC callbacks (weak in the HAL, defined here): the end of the conversion the port
// started. C linkage and global names by definition.
extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the HAL fixes these names
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
  kb::port::stm32::sdk::AdcSlot* const slot = kb::port::stm32::sdk::AdcSlotOf(hadc);
  kb::port::stm32::Completion* const done =
      (slot != nullptr) ? slot->conversion.load(std::memory_order_acquire) : nullptr;
  if (done != nullptr) {
    done->Complete(kb::port::stm32::hal::kOk, HAL_ADC_GetValue(hadc));
  }
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef* hadc) {
  kb::port::stm32::sdk::AdcSlot* const slot = kb::port::stm32::sdk::AdcSlotOf(hadc);
  kb::port::stm32::Completion* const done =
      (slot != nullptr) ? slot->conversion.load(std::memory_order_acquire) : nullptr;
  if (done != nullptr) {
    done->Complete(kb::port::stm32::VendorCode(static_cast<uint32_t>(HAL_ERROR), hadc->ErrorCode));
  }
}
// NOLINTEND(readability-identifier-naming)
} // extern "C"
