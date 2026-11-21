// Boundary file (deviations/boundary-files.yml): the ADC of a DMA scan stream behind sdk.hpp: the
// HAL initialisation in scan mode with an external trigger and circular DMA requests, the overrun
// mode data-preserved, the ranks, the calibration, the conversion count and the data register read
// back, the DMA request bits, the overrun flag, start and stop. The stream's handle is its own (the
// instance belongs to the stream alone, src/adc_stream.cpp); the DMA stream is
// src/boundary/dma.cpp. Every decision is in the platform-free src/adc_stream.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/error.hpp"
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

std::array<ADC_HandleTypeDef, kAdcs>& Handles() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<ADC_HandleTypeDef, kAdcs> handles{};
  return handles;
}

[[nodiscard]] ADC_TypeDef* AdcRegs(uint8_t instance) noexcept {
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

void StreamClock(uint8_t instance) noexcept {
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

[[nodiscard]] uint32_t ChannelOf(uint8_t channel) noexcept {
#if defined(STM32F4)
  return channel;
#else
  return __LL_ADC_DECIMAL_NB_TO_CHANNEL(channel);
#endif
}

[[nodiscard]] uint32_t RankOf(std::size_t rank) noexcept {
#if defined(STM32F4)
  return static_cast<uint32_t>(rank + 1U);
#else
  constexpr std::array<uint32_t, 16> kRanks{
      ADC_REGULAR_RANK_1,  ADC_REGULAR_RANK_2,  ADC_REGULAR_RANK_3,  ADC_REGULAR_RANK_4,
      ADC_REGULAR_RANK_5,  ADC_REGULAR_RANK_6,  ADC_REGULAR_RANK_7,  ADC_REGULAR_RANK_8,
      ADC_REGULAR_RANK_9,  ADC_REGULAR_RANK_10, ADC_REGULAR_RANK_11, ADC_REGULAR_RANK_12,
      ADC_REGULAR_RANK_13, ADC_REGULAR_RANK_14, ADC_REGULAR_RANK_15, ADC_REGULAR_RANK_16};
  return kRanks[rank & 15U];
#endif
}

[[nodiscard]] uint32_t SampleTimeOf(AdcSampling sampling) noexcept {
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

void StreamInitFields(ADC_HandleTypeDef& h, std::size_t ranks, uint32_t trigger) noexcept {
  h.Init.ClockPrescaler = AdcClockPrescaler();
  h.Init.ContinuousConvMode = DISABLE;
  h.Init.DiscontinuousConvMode = DISABLE;
  h.Init.ExternalTrigConv = trigger;
  h.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  h.Init.NbrOfConversion = static_cast<uint32_t>(ranks);
  h.Init.EOCSelection = ADC_EOC_SEQ_CONV;
#if defined(STM32F4)
  h.Init.Resolution = ADC_RESOLUTION_12B;
  h.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  h.Init.ScanConvMode = ENABLE;
  h.Init.DMAContinuousRequests = ENABLE; // DDS: requests go on after the last of a pass (circular)
#elif defined(STM32H7)
  h.Init.Resolution = ADC_RESOLUTION_16B;
  h.Init.ScanConvMode = ADC_SCAN_ENABLE;
  h.Init.LowPowerAutoWait = DISABLE;
  h.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  h.Init.Overrun = ADC_OVR_DATA_PRESERVED; // OVRMOD 0: an overrun stops the requests visibly
  h.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  h.Init.OversamplingMode = DISABLE;
#else
  h.Init.Resolution = ADC_RESOLUTION_12B;
  h.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  h.Init.ScanConvMode = ADC_SCAN_ENABLE;
  h.Init.LowPowerAutoWait = DISABLE;
  h.Init.DMAContinuousRequests = ENABLE; // DMACFG circular
  h.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  h.Init.OversamplingMode = DISABLE;
#endif
}

[[nodiscard]] uint32_t Calibrate(ADC_HandleTypeDef& h) noexcept {
#if defined(STM32F4)
  static_cast<void>(h);
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
} // namespace

uint32_t AdcStreamOpen(uint8_t instance, std::span<const uint8_t> channels, AdcSampling sampling,
                       uint32_t trigger) noexcept {
  uint32_t code = port_code::kInvalidArgument;
  ADC_TypeDef* const regs = AdcRegs(instance);
  if ((regs != nullptr) && (instance < kAdcs) && !channels.empty() && (channels.size() <= 16U)) {
    ADC_HandleTypeDef& h = Handles()[instance];
    StreamClock(instance);
    h.Instance = regs;
    StreamInitFields(h, channels.size(), trigger);
    HAL_StatusTypeDef s = HAL_ADC_Init(&h);
    code = Code(s, h.ErrorCode);
    if (code == hal::kOk) {
      code = Calibrate(h);
    }
    for (std::size_t rank = 0U; (code == hal::kOk) && (rank < channels.size()); ++rank) {
      ADC_ChannelConfTypeDef c{};
      c.Channel = ChannelOf(channels[rank]);
      c.Rank = RankOf(rank);
      c.SamplingTime = SampleTimeOf(sampling);
#if !defined(STM32F4)
      c.SingleDiff = ADC_SINGLE_ENDED;
      c.OffsetNumber = ADC_OFFSET_NONE;
#endif
      s = HAL_ADC_ConfigChannel(&h, &c);
      code = Code(s, h.ErrorCode);
    }
  }
  return code;
}

uint32_t AdcStreamConversions(uint8_t instance) noexcept {
  const ADC_TypeDef* const a = AdcRegs(instance);
  return (a != nullptr) ? (((a->SQR1 & ADC_SQR1_L) >> ADC_SQR1_L_Pos) + 1U) : 0U;
}

std::uintptr_t AdcStreamDataRegister(uint8_t instance) noexcept {
  const ADC_TypeDef* const a = AdcRegs(instance);
  std::uintptr_t address = 0U;
  if (a != nullptr) {
    const volatile uint32_t* const dr = &a->DR;
    address = std::bit_cast<std::uintptr_t>(dr);
  }
  return address;
}

uint32_t AdcStreamStart(uint8_t instance) noexcept {
  uint32_t code = port_code::kInvalidArgument;
  ADC_TypeDef* const a = AdcRegs(instance);
  if ((a != nullptr) && (instance < kAdcs)) {
#if defined(STM32F4)
    a->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS;
#elif defined(STM32H7)
    a->CFGR = (a->CFGR & ~ADC_CFGR_DMNGT) | ADC_CONVERSIONDATA_DMA_CIRCULAR;
#else
    a->CFGR |= ADC_CFGR_DMAEN | ADC_CFGR_DMACFG;
#endif
    ADC_HandleTypeDef& h = Handles()[instance];
    const HAL_StatusTypeDef s =
        HAL_ADC_Start(&h); // enables the ADC; conversions wait for the trigger
    code = Code(s, h.ErrorCode);
  }
  return code;
}

bool AdcStreamTakeOverrun(uint8_t instance) noexcept {
  ADC_TypeDef* const a = AdcRegs(instance);
  bool overrun = false;
  if (a != nullptr) {
#if defined(STM32F4)
    overrun = (a->SR & ADC_SR_OVR) != 0U;
    if (overrun) {
      a->SR = ~ADC_SR_OVR; // rc_w0: only OVR is cleared
    }
#else
    overrun = (a->ISR & ADC_ISR_OVR) != 0U;
    if (overrun) {
      a->ISR = ADC_ISR_OVR; // rc_w1
    }
#endif
  }
  return overrun;
}

uint32_t AdcStreamStop(uint8_t instance) noexcept {
  uint32_t code = port_code::kInvalidArgument;
  ADC_TypeDef* const a = AdcRegs(instance);
  if ((a != nullptr) && (instance < kAdcs)) {
    ADC_HandleTypeDef& h = Handles()[instance];
    const HAL_StatusTypeDef s = HAL_ADC_Stop(&h);
    code = Code(s, h.ErrorCode);
#if defined(STM32F4)
    a->CR2 &= ~(ADC_CR2_DMA | ADC_CR2_DDS);
#elif defined(STM32H7)
    a->CFGR &= ~ADC_CFGR_DMNGT;
#else
    a->CFGR &= ~(ADC_CFGR_DMAEN | ADC_CFGR_DMACFG);
#endif
  }
  return code;
}

uint32_t AdcStreamClose(uint8_t instance) noexcept {
  uint32_t code = port_code::kInvalidArgument;
  if ((instance < kAdcs) && (AdcRegs(instance) != nullptr)) {
    ADC_HandleTypeDef& h = Handles()[instance];
    const HAL_StatusTypeDef s = HAL_ADC_DeInit(&h);
    code = Code(s, h.ErrorCode);
    h.Instance = nullptr;
  }
  return code;
}

} // namespace kb::port::stm32::sdk
