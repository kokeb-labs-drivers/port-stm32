// AdcChannel over the fake HAL ADC (the shared instance, calibration, scaling), and PwmChannel over
// the fake HAL timers (the period, the compare, the idle level), with every refusal. Test code.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace {
using kb::iface::Duty;
constexpr int kPortA = 0;
constexpr int kPortB = 1;

stm::AdcConfig Adc(uint8_t channel = 1U) {
  stm::AdcConfig c{};
  c.instance = 1U;
  c.channel = channel;
  c.pin = PinId{GpioPort::A, 0U, 0U};
  return c;
}

stm::PwmConfig Pwm() {
  stm::PwmConfig c{};
  c.timer = 3U;
  c.channel = 2U;
  c.pin = PinId{GpioPort::B, 5U, 2U};
  c.frequency_hz = 1000U;
  return c;
}

void ReenterRead(void* context) {
  const kb::Result<uint32_t> r = static_cast<stm::AdcChannel*>(context)->ReadRaw();
  KB_CHECK(!r.IsOk() && (r.Error() == Status::Busy));
}
} // namespace

KB_TEST("adc: millivolts from counts, rounded") {
  static_assert(stm::AdcChannel::ToMillivolts(4095U, 4095U, 3300U) == 3300);
  static_assert(stm::AdcChannel::ToMillivolts(0U, 4095U, 3300U) == 0);
  static_assert(stm::AdcChannel::ToMillivolts(2048U, 4095U, 3300U) == 1650);
  static_assert(stm::AdcChannel::ToMillivolts(32768U, 65535U, 3000U) == 1500);
  KB_CHECK(stm::AdcChannel::ToMillivolts(1U, 4095U, 3300U) == 1);
}

KB_TEST("adc: a channel opens and calibrates the instance once and converts") {
  Fresh();
  {
    stm::AdcChannel ch{Adc()};
    KB_CHECK(FailedWith(ch.ReadRaw(), Status::NotReady, stm::port_code::kNotReady));
    KB_REQUIRE(ch.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(ch.EnsureReady(), Status::Ok);
    KB_CHECK_EQ(ch.FullScale(), 4095U);
    KB_CHECK_EQ(fake::ModeOf(kPortA, 0), GPIO_MODE_ANALOG);
    KB_CHECK(fake::ClockEnabled(kF4 ? "ADC1" : "ADC12"));
    KB_CHECK_EQ(fake::Calls("HAL_ADCEx_Calibration_Start"), kF4 ? 0U : 1U);
    fake::AdcRaw(4095U);
    KB_CHECK(ch.ReadRaw().ValueOr(0U) == 4095U);
    KB_CHECK(ch.ReadMillivolts().ValueOr(0) == 3300);
    KB_CHECK_EQ(fake::AdcLastChannel(), kF4 ? 1U : ((1U << 26U) | 0x1000U));
    KB_CHECK_EQ(fake::AdcLastSampling(), kF4 ? ADC_SAMPLETIME_84CYCLES : ADC_SAMPLETIME_47CYCLES_5);
    stm::AdcConfig b = Adc(2U);
    b.pin = PinId{};
    b.sampling = stm::AdcSampling::Long;
    stm::AdcChannel second{b};
    KB_REQUIRE(second.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(fake::Calls("HAL_ADC_Init"), 1U); // shared
    KB_CHECK(second.ReadRaw().IsOk());
    KB_CHECK_EQ(fake::AdcLastSampling(),
                kF4 ? ADC_SAMPLETIME_480CYCLES : ADC_SAMPLETIME_640CYCLES_5);
    KB_CHECK(ch.LastError().IsOk());
    KB_CHECK_EQ(second.Release(), Status::Ok);
    KB_CHECK_EQ(fake::Calls("HAL_ADC_DeInit"), 0U);
  }
  KB_CHECK_EQ(fake::Calls("HAL_ADC_DeInit"), 1U);
  stm::AdcConfig s = Adc();
  s.sampling = stm::AdcSampling::Short;
  stm::AdcChannel short_ch{s};
  KB_REQUIRE(short_ch.EnsureReady() == Status::Ok);
  KB_CHECK(short_ch.ReadRaw().IsOk());
  KB_CHECK_EQ(fake::AdcLastSampling(), kF4 ? ADC_SAMPLETIME_15CYCLES : ADC_SAMPLETIME_6CYCLES_5);
}

KB_TEST("adc: refusals and HAL failures") {
  Fresh();
  stm::AdcConfig c = Adc();
  c.instance = 9U;
  stm::AdcChannel bad{c};
  KB_CHECK_EQ(bad.EnsureReady(), Status::InvalidArgument);
  c = Adc(20U);
  stm::AdcChannel bad_channel{c};
  KB_CHECK_EQ(bad_channel.EnsureReady(), Status::InvalidArgument);
  c = Adc();
  c.vref_mv = 0U;
  stm::AdcChannel no_vref{c};
  KB_CHECK_EQ(no_vref.EnsureReady(), Status::InvalidArgument);
  c = Adc();
  c.conversion_timeout = kb::Milliseconds{0U};
  stm::AdcChannel no_timeout{c};
  KB_CHECK_EQ(no_timeout.EnsureReady(), Status::InvalidArgument);
  c = Adc();
  c.pin.pin = 16U;
  stm::AdcChannel bad_pin{c};
  KB_CHECK_EQ(bad_pin.EnsureReady(), Status::InvalidArgument);
  stm::AdcChannel ch{Adc()};
  fake::Fail("HAL_ADC_Init", HAL_ERROR, 0x2U);
  KB_CHECK_EQ(ch.EnsureReady(), Status::Fault);
  if (!kF4) {
    fake::Fail("HAL_ADCEx_Calibration_Start", HAL_TIMEOUT);
    KB_CHECK_EQ(ch.EnsureReady(), Status::Timeout);
  }
  KB_REQUIRE(ch.EnsureReady() == Status::Ok);
  fake::Fail("HAL_ADC_PollForConversion", HAL_TIMEOUT);
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Timeout, Code(HAL_TIMEOUT)));
  fake::Fail("HAL_ADC_ConfigChannel", HAL_ERROR);
  KB_CHECK(FailedWith(ch.ReadMillivolts(), Status::Fault, Code(HAL_ERROR)));
  fake::Fail("HAL_ADC_Start", HAL_BUSY);
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Busy, Code(HAL_BUSY)));
  fake::Hook("HAL_ADC_Start", ReenterRead, &ch);
  KB_CHECK(ch.ReadRaw().IsOk());
  fake::Fail("HAL_ADC_DeInit", HAL_ERROR);
  KB_CHECK_EQ(ch.Release(), Status::Fault);
}

KB_TEST("pwm: the period has the smallest prescaler; the compare rounds to nearest") {
  bool ok = false;
  stm::PwmPeriod p = stm::ComputePwmPeriod(160000000U, 1000U, ok);
  KB_CHECK(ok);
  KB_CHECK_EQ(p.prescaler, 2U); // 160 MHz / 3 = 53.3 MHz, 53333 counts per period
  KB_CHECK_EQ(p.reload, 53332U);
  p = stm::ComputePwmPeriod(160000000U, 20000U, ok);
  KB_CHECK(ok);
  KB_CHECK_EQ(p.prescaler, 0U);
  KB_CHECK_EQ(p.reload, 7999U);
  p = stm::ComputePwmPeriod(160000000U, 80000000U, ok);
  KB_CHECK(ok);
  KB_CHECK_EQ(p.reload, 1U);
  static_cast<void>(stm::ComputePwmPeriod(160000000U, 0U, ok));
  KB_CHECK(!ok);
  static_cast<void>(stm::ComputePwmPeriod(160000000U, 80000001U, ok));
  KB_CHECK(!ok);
  p = stm::ComputePwmPeriod(0xFFFFFFFFU, 1U, ok); // the slowest: the prescaler at its 16-bit limit
  KB_CHECK(ok);
  KB_CHECK_EQ(p.prescaler, 65535U);
  static_assert(stm::PwmChannel::CompareFor(Duty{0U}, 999U) == 0U);
  static_assert(stm::PwmChannel::CompareFor(Duty{10000U}, 999U) == 1000U);
  static_assert(stm::PwmChannel::CompareFor(Duty{2500U}, 999U) == 250U);
  static_assert(stm::PwmChannel::CompareFor(Duty{1U}, 99U) == 0U);
}

KB_TEST("pwm: EnsureReady holds the idle level; duty, enable, frequency") {
  Fresh();
  stm::PwmChannel pwm{Pwm()};
  KB_CHECK_EQ(pwm.SetDuty(Duty{5000U}), Status::NotReady);
  KB_CHECK_EQ(pwm.Enable(true), Status::NotReady);
  KB_REQUIRE(pwm.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(pwm.EnsureReady(), Status::Ok);
  KB_CHECK(fake::ClockEnabled("TIM3"));
  KB_CHECK_EQ(fake::ModeOf(kPortB, 5), GPIO_MODE_AF_PP);
  KB_CHECK_EQ(fake::AlternateOf(kPortB, 5), 2U);
  KB_CHECK_EQ(fake::TimInit(3).Period, pwm.Reload());
  // APB1 timer: PCLK1 80 MHz behind a divider of 2 runs at 160 MHz.
  KB_CHECK_EQ(pwm.Reload(), 53332U);
  KB_CHECK_EQ(kb_fake_tim[3].CCR[1], 0U); // idle low
  KB_CHECK_EQ(pwm.SetDuty(Duty{2500U}), Status::Ok);
  KB_CHECK_EQ(kb_fake_tim[3].CCR[1], 0U); // not applied while disabled
  KB_CHECK_EQ(pwm.Enable(true), Status::Ok);
  KB_CHECK_EQ(kb_fake_tim[3].CCR[1], 13333U);
  KB_CHECK_EQ(pwm.SetFrequencyHz(20000U), Status::Ok);
  KB_CHECK_EQ(kb_fake_tim[3].ARR, 7999U);
  KB_CHECK_EQ(kb_fake_tim[3].CCR[1], 2000U); // the duty kept as a fraction
  KB_CHECK_EQ(pwm.Enable(false), Status::Ok);
  KB_CHECK_EQ(kb_fake_tim[3].CCR[1], 0U);
  KB_CHECK_EQ(pwm.SetFrequencyHz(0U), Status::InvalidArgument);
  fake::Fail("HAL_TIM_GenerateEvent", HAL_ERROR);
  KB_CHECK_EQ(pwm.SetFrequencyHz(1000U), Status::Fault);
  KB_CHECK_EQ(pwm.Reload(), 7999U);
  KB_CHECK(FailedWith(pwm.LastError(), Status::Fault, Code(HAL_ERROR)));
  KB_CHECK_EQ(pwm.Release(), Status::Ok);
}

KB_TEST("pwm: idle high, an APB2 timer, settings before EnsureReady, shared timers, failures") {
  Fresh();
  stm::PwmConfig c = Pwm();
  c.timer = 1U;
  c.channel = 4U;
  c.idle_high = true;
  stm::PwmChannel pwm{c};
  KB_CHECK_EQ(pwm.SetFrequencyHz(2000U), Status::Ok);
  KB_REQUIRE(pwm.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(kb_fake_tim[1].CCR[3], pwm.Reload() + 1U); // always high while disabled
  stm::PwmConfig d = Pwm();
  d.timer = 1U;
  d.channel = 1U;
  d.pin = PinId{GpioPort::A, 8U, 6U};
  stm::PwmChannel shared{d};
  KB_REQUIRE(shared.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_PWM_Init"), 1U);
  KB_CHECK_EQ(shared.Release(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_PWM_DeInit"), 0U);
  KB_CHECK_EQ(pwm.Release(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_PWM_DeInit"), 1U);
  const auto refused = [](stm::PwmConfig e) {
    stm::PwmChannel p{e};
    return p.EnsureReady() == Status::InvalidArgument;
  };
  stm::PwmConfig e = Pwm();
  e.timer = 6U; // a basic timer: no channels on the fake device
  KB_CHECK(refused(e));
  e = Pwm();
  e.channel = 0U;
  KB_CHECK(refused(e));
  e = Pwm();
  e.channel = 5U;
  KB_CHECK(refused(e));
  e = Pwm();
  e.pin = PinId{};
  KB_CHECK(refused(e));
  e = Pwm();
  e.frequency_hz = 0U;
  KB_CHECK(refused(e));
  e = Pwm();
  e.timer_clock_hz = 1000U;
  e.frequency_hz = 1000U; // above timer clock / 2
  KB_CHECK(refused(e));
  stm::PwmChannel failing{Pwm()};
  fake::Fail("HAL_TIM_PWM_Init", HAL_ERROR);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Fault);
  fake::Fail("HAL_TIM_PWM_ConfigChannel", HAL_ERROR);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Fault);
  fake::Fail("HAL_TIM_PWM_Start", HAL_BUSY);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Busy);
  KB_REQUIRE(failing.EnsureReady() == Status::Ok);
  fake::Fail("HAL_TIM_PWM_Stop", HAL_ERROR);
  KB_CHECK_EQ(failing.Release(), Status::Fault);
}

KB_TEST("adc: a failed stop after the conversion is reported") {
  Fresh();
  stm::AdcChannel ch{Adc()};
  KB_REQUIRE(ch.EnsureReady() == Status::Ok);
  fake::Fail("HAL_ADC_Stop", HAL_ERROR);
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Fault, Code(HAL_ERROR)));
  KB_CHECK(ch.ReadRaw().IsOk());
}
