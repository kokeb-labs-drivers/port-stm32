// Boundary file (deviations/boundary-files.yml): the system, GPIO, EXTI, ADC, timer (PWM and
// clock), DWT and ITM calls of the STM32 HAL and CMSIS behind sdk.hpp. Family differences use the
// CMSIS family macros (STM32F4, STM32G4, STM32H7, STM32L4) and the instance macros of the device
// header, here and in the other boundary files only. SPDX-License-Identifier: MIT Copyright (c)
// 2026 The port-stm32 authors
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/time.hpp"
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

namespace kb::port::stm32::sdk {

// error.hpp names the HAL's values; a change in the HAL stops the build here.
static_assert(hal::kOk == static_cast<uint32_t>(HAL_OK));
static_assert(hal::kError == static_cast<uint32_t>(HAL_ERROR));
static_assert(hal::kBusy == static_cast<uint32_t>(HAL_BUSY));
static_assert(hal::kTimeout == static_cast<uint32_t>(HAL_TIMEOUT));

namespace {
constexpr std::size_t kPorts = 11U;
constexpr std::size_t kLines = 16U;
constexpr std::size_t kAdcs = 6U;
constexpr std::size_t kTimers = 21U;

[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// --- GPIO ----------------------------------------------------------------------------------------
[[nodiscard]] uint32_t ExtiModeOf(kb::iface::Edge edge) noexcept {
  uint32_t mode = GPIO_MODE_IT_RISING_FALLING;
  if (edge == kb::iface::Edge::Rising) {
    mode = GPIO_MODE_IT_RISING;
  } else if (edge == kb::iface::Edge::Falling) {
    mode = GPIO_MODE_IT_FALLING;
  } else {
    mode = GPIO_MODE_IT_RISING_FALLING;
  }
  return mode;
}

[[nodiscard]] GPIO_TypeDef* PortOf(GpioPort port) noexcept {
  // The ports this device has, by index; nullptr where it has none.
  const std::array<GPIO_TypeDef*, kPorts> ports{
      GPIOA, GPIOB, GPIOC,
#if defined(GPIOD)
      GPIOD,
#else
      nullptr,
#endif
#if defined(GPIOE)
      GPIOE,
#else
      nullptr,
#endif
#if defined(GPIOF)
      GPIOF,
#else
      nullptr,
#endif
#if defined(GPIOG)
      GPIOG,
#else
      nullptr,
#endif
#if defined(GPIOH)
      GPIOH,
#else
      nullptr,
#endif
#if defined(GPIOI)
      GPIOI,
#else
      nullptr,
#endif
#if defined(GPIOJ)
      GPIOJ,
#else
      nullptr,
#endif
#if defined(GPIOK)
      GPIOK,
#else
      nullptr,
#endif
  };
  const auto index = static_cast<std::size_t>(port);
  return (index < ports.size()) ? ports[index] : nullptr;
}

// The port clocks, in two halves.
void PortClockLow(GpioPort port) noexcept {
  if (port == GpioPort::A) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
  } else if (port == GpioPort::B) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
  } else if (port == GpioPort::C) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
#if defined(GPIOD)
  } else if (port == GpioPort::D) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
#endif
#if defined(GPIOE)
  } else if (port == GpioPort::E) {
    __HAL_RCC_GPIOE_CLK_ENABLE();
#endif
#if defined(GPIOF)
  } else if (port == GpioPort::F) {
    __HAL_RCC_GPIOF_CLK_ENABLE();
#endif
  } else {
    // a port of the other half, or one the device does not have
  }
}

void PortClockHigh(GpioPort port) noexcept {
  static_cast<void>(port);
#if defined(GPIOG)
  if (port == GpioPort::G) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
  }
#endif
#if defined(GPIOH)
  if (port == GpioPort::H) {
    __HAL_RCC_GPIOH_CLK_ENABLE();
  }
#endif
#if defined(GPIOI)
  if (port == GpioPort::I) {
    __HAL_RCC_GPIOI_CLK_ENABLE();
  }
#endif
#if defined(GPIOJ)
  if (port == GpioPort::J) {
    __HAL_RCC_GPIOJ_CLK_ENABLE();
  }
#endif
#if defined(GPIOK)
  if (port == GpioPort::K) {
    __HAL_RCC_GPIOK_CLK_ENABLE();
  }
#endif
}

void PortClock(GpioPort port) noexcept {
  PortClockLow(port);
  PortClockHigh(port);
}

[[nodiscard]] uint16_t MaskOf(PinId pin) noexcept {
  return static_cast<uint16_t>(1U << pin.pin);
}

[[nodiscard]] uint32_t PullOf(Pull pull) noexcept {
  uint32_t p = GPIO_NOPULL;
  if (pull == Pull::Up) {
    p = GPIO_PULLUP;
  } else if (pull == Pull::Down) {
    p = GPIO_PULLDOWN;
  } else {
    p = GPIO_NOPULL;
  }
  return p;
}

[[nodiscard]] uint32_t SpeedOf(Speed speed) noexcept {
  uint32_t s = GPIO_SPEED_FREQ_LOW;
  if (speed == Speed::Medium) {
    s = GPIO_SPEED_FREQ_MEDIUM;
  } else if (speed == Speed::High) {
    s = GPIO_SPEED_FREQ_HIGH;
  } else if (speed == Speed::VeryHigh) {
    s = GPIO_SPEED_FREQ_VERY_HIGH;
  } else {
    s = GPIO_SPEED_FREQ_LOW;
  }
  return s;
}

std::array<uint16_t, kPorts>& Claims() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<uint16_t, kPorts> claims{};
  return claims;
}

struct ExtiSlot {
  kb::iface::GpioCallback fn{nullptr};
  void* context{nullptr};
  GpioPort port{GpioPort::A};
};

std::array<ExtiSlot, kLines>& Exti() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<ExtiSlot, kLines> lines{};
  return lines;
}

[[nodiscard]] IRQn_Type ExtiIrq(uint8_t line) noexcept {
  IRQn_Type irq = EXTI15_10_IRQn;
  if (line == 0U) {
    irq = EXTI0_IRQn;
  } else if (line == 1U) {
    irq = EXTI1_IRQn;
  } else if (line == 2U) {
    irq = EXTI2_IRQn;
  } else if (line == 3U) {
    irq = EXTI3_IRQn;
  } else if (line == 4U) {
    irq = EXTI4_IRQn;
  } else if (line <= 9U) {
    irq = EXTI9_5_IRQn;
  } else {
    irq = EXTI15_10_IRQn;
  }
  return irq;
}

// --- ADC -----------------------------------------------------------------------------------------
struct AdcSlot {
  ADC_HandleTypeDef handle{};
  uint32_t users{0U};
  kb::rtos::Mutex lock;
};

std::array<AdcSlot, kAdcs>& Adcs() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<AdcSlot, kAdcs> adcs{};
  return adcs;
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
  h.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
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
  const HAL_StatusTypeDef s = HAL_ADCEx_Calibration_Start(&h, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
  return Code(s, h.ErrorCode);
#else
  const HAL_StatusTypeDef s = HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED);
  return Code(s, h.ErrorCode);
#endif
}

[[nodiscard]] uint32_t AdcOne(ADC_HandleTypeDef& h, uint8_t channel, AdcSampling sampling,
                              uint32_t conversion_ms, uint32_t& raw) noexcept {
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
    s = HAL_ADC_Start(&h);
  }
  if (s == HAL_OK) {
    s = HAL_ADC_PollForConversion(&h, conversion_ms);
    raw = (s == HAL_OK) ? HAL_ADC_GetValue(&h) : 0U;
    const HAL_StatusTypeDef stop = HAL_ADC_Stop(&h); // always, so the next conversion starts clean
    s = (s == HAL_OK) ? stop : s;
  }
  return Code(s, h.ErrorCode);
}

// --- timers --------------------------------------------------------------------------------------
struct TimerSlot {
  TIM_HandleTypeDef handle{};
  uint32_t users{0U};
};

std::array<TimerSlot, kTimers>& Timers() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<TimerSlot, kTimers> timers{};
  return timers;
}

std::array<uint32_t, kTimers>& Wraps() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<uint32_t, kTimers> wraps{};
  return wraps;
}

// The timers the port serves, by number, in two halves.
[[nodiscard]] TIM_TypeDef* TimerOfLow(uint8_t timer) noexcept {
  TIM_TypeDef* t = nullptr;
  switch (timer) {
#if defined(TIM1)
  case 1U:
    t = TIM1;
    break;
#endif
#if defined(TIM2)
  case 2U:
    t = TIM2;
    break;
#endif
#if defined(TIM3)
  case 3U:
    t = TIM3;
    break;
#endif
#if defined(TIM4)
  case 4U:
    t = TIM4;
    break;
#endif
#if defined(TIM5)
  case 5U:
    t = TIM5;
    break;
#endif
#if defined(TIM8)
  case 8U:
    t = TIM8;
    break;
#endif
  default:
    t = nullptr; // a timer of the other half, or one the device does not have
    break;
  }
  return t;
}

[[nodiscard]] TIM_TypeDef* TimerOfHigh(uint8_t timer) noexcept {
  TIM_TypeDef* t = nullptr;
  switch (timer) {
#if defined(TIM9)
  case 9U:
    t = TIM9;
    break;
#endif
#if defined(TIM12)
  case 12U:
    t = TIM12;
    break;
#endif
#if defined(TIM15)
  case 15U:
    t = TIM15;
    break;
#endif
#if defined(TIM16)
  case 16U:
    t = TIM16;
    break;
#endif
#if defined(TIM17)
  case 17U:
    t = TIM17;
    break;
#endif
#if defined(TIM20)
  case 20U:
    t = TIM20;
    break;
#endif
  default:
    t = nullptr; // a timer of the other half, or one the device does not have
    break;
  }
  return t;
}

[[nodiscard]] TIM_TypeDef* TimerOf(uint8_t timer) noexcept {
  TIM_TypeDef* const low = TimerOfLow(timer);
  return (low != nullptr) ? low : TimerOfHigh(timer);
}

// True for the timers on APB2 (TIM1, TIM8, TIM9 to TIM11, TIM15 to TIM17, TIM20).
[[nodiscard]] bool OnApb2(uint8_t timer) noexcept {
  return (timer == 1U) || (timer == 8U) || ((timer >= 9U) && (timer <= 11U)) ||
         ((timer >= 15U) && (timer <= 17U)) || (timer == 20U);
}

// The timer clocks, in two halves (one switch each).
void TimerClockLow(uint8_t timer) noexcept {
  switch (timer) {
#if defined(TIM1)
  case 1U:
    __HAL_RCC_TIM1_CLK_ENABLE();
    break;
#endif
#if defined(TIM2)
  case 2U:
    __HAL_RCC_TIM2_CLK_ENABLE();
    break;
#endif
#if defined(TIM3)
  case 3U:
    __HAL_RCC_TIM3_CLK_ENABLE();
    break;
#endif
#if defined(TIM4)
  case 4U:
    __HAL_RCC_TIM4_CLK_ENABLE();
    break;
#endif
#if defined(TIM5)
  case 5U:
    __HAL_RCC_TIM5_CLK_ENABLE();
    break;
#endif
#if defined(TIM8)
  case 8U:
    __HAL_RCC_TIM8_CLK_ENABLE();
    break;
#endif
  default:
    break; // a timer of the other half, or one the device does not have
  }
}

void TimerClockHigh(uint8_t timer) noexcept {
  switch (timer) {
#if defined(TIM9)
  case 9U:
    __HAL_RCC_TIM9_CLK_ENABLE();
    break;
#endif
#if defined(TIM12)
  case 12U:
    __HAL_RCC_TIM12_CLK_ENABLE();
    break;
#endif
#if defined(TIM15)
  case 15U:
    __HAL_RCC_TIM15_CLK_ENABLE();
    break;
#endif
#if defined(TIM16)
  case 16U:
    __HAL_RCC_TIM16_CLK_ENABLE();
    break;
#endif
#if defined(TIM17)
  case 17U:
    __HAL_RCC_TIM17_CLK_ENABLE();
    break;
#endif
#if defined(TIM20)
  case 20U:
    __HAL_RCC_TIM20_CLK_ENABLE();
    break;
#endif
  default:
    break; // a timer of the other half, or one the device does not have
  }
}

void TimerClock(uint8_t timer) noexcept {
  TimerClockLow(timer);
  TimerClockHigh(timer);
}

[[nodiscard]] uint32_t ChannelOf(uint8_t channel) noexcept {
  constexpr std::array<uint32_t, 4> kChannels{TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3,
                                              TIM_CHANNEL_4};
  return kChannels[static_cast<std::size_t>(channel - 1U)];
}

[[nodiscard]] uint32_t TimerInit(TimerSlot& slot, uint8_t timer, const PwmPeriod& period) noexcept {
  TimerClock(timer);
  slot.handle.Instance = TimerOf(timer);
  slot.handle.Init.Prescaler = period.prescaler;
  slot.handle.Init.CounterMode = TIM_COUNTERMODE_UP;
  slot.handle.Init.Period = period.reload;
  slot.handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  slot.handle.Init.RepetitionCounter = 0U;
  slot.handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  const HAL_StatusTypeDef s = HAL_TIM_PWM_Init(&slot.handle);
  return Code(s, 0U);
}
} // namespace

// --- system --------------------------------------------------------------------------------------
uint32_t TickMs() noexcept {
  return HAL_GetTick();
}

uint32_t CoreClockHz() noexcept {
  return SystemCoreClock;
}

uint32_t Cycles() noexcept {
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(STM32H7)
    DWT->LAR = 0xC5ACCE55U; // the Cortex-M7 DWT is locked after reset
#endif
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }
  return DWT->CYCCNT;
}

// --- GPIO ----------------------------------------------------------------------------------------
bool PortExists(GpioPort port) noexcept {
  return PortOf(port) != nullptr;
}

uint32_t PinClaim(PinId pin) noexcept {
  uint16_t& claims = Claims()[static_cast<std::size_t>(pin.port)];
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool free = (claims & MaskOf(pin)) == 0U;
  if (free) {
    claims = static_cast<uint16_t>(claims | MaskOf(pin));
  }
  __set_PRIMASK(primask);
  return free ? hal::kOk : port_code::kOwned;
}

void PinUnclaim(PinId pin) noexcept {
  uint16_t& claims = Claims()[static_cast<std::size_t>(pin.port)];
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  claims = static_cast<uint16_t>(claims & static_cast<uint16_t>(~MaskOf(pin)));
  __set_PRIMASK(primask);
}

uint32_t PinOutput(PinId pin, bool open_drain, Pull pull, Speed speed, bool high) noexcept {
  PortClock(pin.port);
  HAL_GPIO_WritePin(PortOf(pin.port), MaskOf(pin),
                    high ? GPIO_PIN_SET : GPIO_PIN_RESET); // first: no glitch
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
  init.Pull = PullOf(pull);
  init.Speed = SpeedOf(speed);
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinInput(PinId pin, Pull pull) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = PullOf(pull);
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinAlternate(PinId pin, bool open_drain, Pull pull) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = open_drain ? GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
  init.Pull = PullOf(pull);
  init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  init.Alternate = pin.af;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinAnalog(PinId pin) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

void PinReset(PinId pin) noexcept {
  HAL_GPIO_DeInit(PortOf(pin.port), MaskOf(pin));
}

void PinWrite(PinId pin, bool high) noexcept {
  HAL_GPIO_WritePin(PortOf(pin.port), MaskOf(pin), high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool PinRead(PinId pin) noexcept {
  return HAL_GPIO_ReadPin(PortOf(pin.port), MaskOf(pin)) == GPIO_PIN_SET;
}

void PinToggle(PinId pin) noexcept {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  HAL_GPIO_TogglePin(PortOf(pin.port), MaskOf(pin));
  __set_PRIMASK(primask);
}

namespace {
// The pull setting a pin has now (its PUPDR bits); no pull for a port the device lacks (refused
// earlier).
[[nodiscard]] uint32_t PullBitsOf(PinId pin) noexcept {
  const GPIO_TypeDef* const port = PortOf(pin.port);
  return (port != nullptr) ? ((port->PUPDR >> (2U * pin.pin)) & 3U) : GPIO_NOPULL;
}
} // namespace

uint32_t ExtiEnable(PinId pin, kb::iface::Edge edge, kb::iface::GpioCallback fn, void* context,
                    uint8_t priority) noexcept {
  ExtiSlot& slot = Exti()[pin.pin];
  uint32_t code = port_code::kOwned; // the line belongs to the same pin number on another port
  if ((slot.fn == nullptr) || (slot.port == pin.port)) {
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    GPIO_InitTypeDef init{};
    init.Pin = MaskOf(pin);
    init.Mode = ExtiModeOf(edge);
    init.Pull = PullBitsOf(pin);
    init.Speed = GPIO_SPEED_FREQ_LOW;
    slot.fn = nullptr; // no call while the line is reconfigured
    slot.context = context;
    slot.port = pin.port;
    HAL_GPIO_Init(PortOf(pin.port), &init);
    slot.fn = fn;
    HAL_NVIC_SetPriority(ExtiIrq(pin.pin), priority, 0U);
    HAL_NVIC_EnableIRQ(ExtiIrq(pin.pin));
    code = hal::kOk;
  }
  return code;
}

uint32_t ExtiDisable(PinId pin) noexcept {
  ExtiSlot& slot = Exti()[pin.pin];
  const uint32_t pull = PullBitsOf(pin);
  slot.fn = nullptr;
  slot.context = nullptr;
  HAL_GPIO_DeInit(PortOf(pin.port), MaskOf(pin)); // clears the EXTI line's configuration
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = pull;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

// --- ADC -----------------------------------------------------------------------------------------
bool AdcExists(uint8_t instance, uint8_t channel) noexcept {
  constexpr uint8_t kMaxChannel = 19U;
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

uint32_t AdcOpen(uint8_t instance) noexcept {
  AdcSlot& slot = Adcs()[instance];
  uint32_t code = hal::kOk;
  if (slot.users == 0U) {
    AdcClock(instance);
    slot.handle.Instance = AdcOf(instance);
    AdcInitFields(slot.handle);
    const HAL_StatusTypeDef s = HAL_ADC_Init(&slot.handle);
    code = Code(s, slot.handle.ErrorCode);
    if (code == hal::kOk) {
      code = AdcCalibrate(slot.handle);
    }
    static_cast<void>(slot.lock.Create());
  }
  if (code == hal::kOk) {
    ++slot.users;
  }
  return code;
}

uint32_t AdcClose(uint8_t instance) noexcept {
  AdcSlot& slot = Adcs()[instance];
  uint32_t code = hal::kOk;
  if (slot.users > 0U) {
    --slot.users;
  }
  if (slot.users == 0U) {
    const HAL_StatusTypeDef s = HAL_ADC_DeInit(&slot.handle);
    code = Code(s, slot.handle.ErrorCode);
  }
  return code;
}

uint32_t AdcConvert(uint8_t instance, uint8_t channel, AdcSampling sampling, uint32_t conversion_ms,
                    uint32_t lock_ms, uint32_t& raw) noexcept {
  AdcSlot& slot = Adcs()[instance];
  uint32_t code = port_code::kLockTimeout;
  if (slot.lock.TryLockFor(kb::Milliseconds{lock_ms})) {
    code = AdcOne(slot.handle, channel, sampling, conversion_ms, raw);
    slot.lock.Unlock();
  }
  return code;
}

// --- timers (PWM) --------------------------------------------------------------------------------
bool TimerExists(uint8_t timer) noexcept {
  return (timer < kTimers) && (TimerOf(timer) != nullptr);
}

uint32_t TimerClockHz(uint8_t timer) noexcept {
  const uint32_t hclk = HAL_RCC_GetHCLKFreq();
  const uint32_t pclk = OnApb2(timer) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
  return (pclk == hclk) ? pclk : (2U * pclk); // the timer clock doubles behind an APB divider
}

uint32_t PwmOpen(const PwmConfig& config, const PwmPeriod& period) noexcept {
  TimerSlot& slot = Timers()[config.timer];
  uint32_t code = (slot.users == 0U) ? TimerInit(slot, config.timer, period) : hal::kOk;
  if (code == hal::kOk) {
    code = PinAlternate(config.pin, false, Pull::None);
  }
  if (code == hal::kOk) {
    TIM_OC_InitTypeDef oc{};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_StatusTypeDef s = HAL_TIM_PWM_ConfigChannel(&slot.handle, &oc, ChannelOf(config.channel));
    if (s == HAL_OK) {
      s = HAL_TIM_PWM_Start(&slot.handle, ChannelOf(config.channel));
    }
    code = Code(s, 0U);
  }
  if (code == hal::kOk) {
    ++slot.users;
  }
  return code;
}

uint32_t PwmSetPeriod(uint8_t timer, const PwmPeriod& period) noexcept {
  TIM_HandleTypeDef& h = Timers()[timer].handle;
  __HAL_TIM_SET_PRESCALER(&h, period.prescaler);
  __HAL_TIM_SET_AUTORELOAD(&h, period.reload);
  const HAL_StatusTypeDef s = HAL_TIM_GenerateEvent(&h, TIM_EVENTSOURCE_UPDATE);
  return Code(s, 0U);
}

void PwmSetCompare(uint8_t timer, uint8_t channel, uint32_t compare) noexcept {
  TIM_HandleTypeDef& h = Timers()[timer].handle;
  __HAL_TIM_SET_COMPARE(&h, ChannelOf(channel), compare);
}

uint32_t PwmClose(uint8_t timer, uint8_t channel) noexcept {
  TimerSlot& slot = Timers()[timer];
  HAL_StatusTypeDef s = HAL_TIM_PWM_Stop(&slot.handle, ChannelOf(channel));
  if (slot.users > 0U) {
    --slot.users;
  }
  if ((s == HAL_OK) && (slot.users == 0U)) {
    s = HAL_TIM_PWM_DeInit(&slot.handle);
  }
  return Code(s, 0U);
}

// --- the clock's timer ---------------------------------------------------------------------------
bool ClockTimerExists(uint8_t timer) noexcept {
  return ((timer == 2U) || (timer == 5U)) && TimerExists(timer);
}

uint32_t ClockStart(const ClockConfig& config) noexcept {
  constexpr uint32_t kTickHz = 1000000U;
  TimerSlot& slot = Timers()[config.timer];
  uint32_t code = hal::kOk;
  if (slot.users == 0U) {
    const uint32_t clock =
        (config.timer_clock_hz != 0U) ? config.timer_clock_hz : TimerClockHz(config.timer);
    TimerClock(config.timer);
    slot.handle.Instance = TimerOf(config.timer);
    slot.handle.Init.Prescaler = ((clock + (kTickHz / 2U)) / kTickHz) - 1U;
    slot.handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    slot.handle.Init.Period = 0xFFFFFFFFU;
    slot.handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    slot.handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_StatusTypeDef s = HAL_TIM_Base_Init(&slot.handle);
    if (s == HAL_OK) {
      __HAL_TIM_CLEAR_FLAG(&slot.handle, TIM_FLAG_UPDATE);
      __HAL_TIM_ENABLE_IT(&slot.handle, TIM_IT_UPDATE);
      const IRQn_Type irq = (config.timer == 2U) ? TIM2_IRQn : TIM5_IRQn;
      HAL_NVIC_SetPriority(irq, config.irq_priority, 0U);
      HAL_NVIC_EnableIRQ(irq);
      s = HAL_TIM_Base_Start(&slot.handle);
    }
    code = Code(s, 0U);
    slot.users = (code == hal::kOk) ? 1U : 0U;
  }
  return code;
}

void ClockSample(uint8_t timer, uint32_t& high_before, uint32_t& counter, uint32_t& high_after,
                 bool& wrap_pending) noexcept {
  TimerSlot& slot = Timers()[timer];
  if (slot.users == 0U) {
    high_before = 0U;
    counter = 0U;
    high_after = 0U;
    wrap_pending = false;
  } else {
    const volatile uint32_t& wraps = Wraps()[timer];
    high_before = wraps;
    counter = slot.handle.Instance->CNT;
    high_after = wraps;
    wrap_pending = __HAL_TIM_GET_FLAG(&slot.handle, TIM_FLAG_UPDATE) != 0U;
  }
}

// --- ITM -----------------------------------------------------------------------------------------
bool ItmEnabled() noexcept {
  return ((ITM->TCR & ITM_TCR_ITMENA_Msk) != 0U) && ((ITM->TER & 1U) != 0U);
}

bool ItmPut(char c, uint32_t spins) noexcept {
  uint32_t left = spins;
  while ((ITM->PORT[0U].u32 == 0U) && (left > 0U)) {
    --left;
  }
  const bool free = ITM->PORT[0U].u32 != 0U;
  if (free) {
    ITM->PORT[0U].u8 = std::bit_cast<uint8_t>(c); // the same bits, no numeric conversion
  }
  return free;
}

// --- interrupt entry points ----------------------------------------------------------------------
void HandleExtiIrq(uint8_t first, uint8_t last) noexcept {
  for (uint8_t line = first; (line <= last) && (line < kLines); ++line) {
    const uint32_t mask = 1U << line;
    if (__HAL_GPIO_EXTI_GET_IT(mask) != 0U) {
      __HAL_GPIO_EXTI_CLEAR_IT(mask);
      const ExtiSlot& slot = Exti()[line];
      if (slot.fn != nullptr) {
        slot.fn(slot.context);
      }
    }
  }
}

void HandleClockIrq(uint8_t timer) noexcept {
  if (timer < kTimers) {
    TimerSlot& slot = Timers()[timer];
    if ((slot.users != 0U) && (__HAL_TIM_GET_FLAG(&slot.handle, TIM_FLAG_UPDATE) != 0U)) {
      __HAL_TIM_CLEAR_FLAG(&slot.handle, TIM_FLAG_UPDATE);
      ++Wraps()[timer];
    }
  }
}

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {
// The public entry points (irq.hpp) are the boundary's: they reach the tables above.
void HandleExtiIrq(uint8_t first, uint8_t last) noexcept {
  sdk::HandleExtiIrq(first, last);
}
void HandleClockIrq(uint8_t timer) noexcept {
  sdk::HandleClockIrq(timer);
}
} // namespace kb::port::stm32
