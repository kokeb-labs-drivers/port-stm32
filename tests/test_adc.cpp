// AdcChannel over the fake HAL ADC (the shared instance, calibration, scaling), with every refusal.
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace {
constexpr int kPortA = 0;

stm::AdcConfig Adc(uint8_t channel = 1U) {
  stm::AdcConfig c{};
  c.instance = 1U;
  c.channel = channel;
  c.pin = PinId{GpioPort::A, 0U, 0U};
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
  c = Adc(19U); // F4 has channels 0 to 18
  stm::AdcChannel nineteen{c};
  KB_CHECK_EQ(nineteen.EnsureReady(), kF4 ? Status::InvalidArgument : Status::Ok);
  KB_CHECK_EQ(nineteen.Release(), Status::Ok);
  c = Adc();
  c.lock_timeout = kb::Milliseconds{stm::kMaxTimeout.Count() + 1U};
  stm::AdcChannel too_long{c};
  KB_CHECK_EQ(too_long.EnsureReady(), Status::InvalidArgument);
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
  KB_CHECK(fake::IrqEnabled(kF4 ? ADC_IRQn : ADC1_2_IRQn));
  fake::SetTickStep(0U);    // the HAL tick stands still: the wait is on the port's clock
  fake::Stall("ADC", true); // the end-of-conversion interrupt never comes
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Timeout, Code(HAL_TIMEOUT)));
  KB_CHECK_EQ(fake::Calls("HAL_ADC_Stop_IT"), 1U);
  fake::Fail("HAL_ADC_Stop_IT", HAL_ERROR, 0x10U); // the stop failing too: in the Detail
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Timeout, Code(HAL_TIMEOUT, 0x10U)));
  fake::Stall("ADC", false);
  fake::Fail("conversion", HAL_ERROR, 0x04U); // the error callback ends the conversion
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Fault, Code(HAL_ERROR, 0x04U)));
  fake::Fail("HAL_ADC_ConfigChannel", HAL_ERROR);
  KB_CHECK(FailedWith(ch.ReadMillivolts(), Status::Fault, Code(HAL_ERROR)));
  fake::Fail("HAL_ADC_Start_IT", HAL_BUSY);
  KB_CHECK(FailedWith(ch.ReadRaw(), Status::Busy, Code(HAL_BUSY)));
  stm::HandleAdcIrq(1U, 3U); // the vector reaches the HAL's handler of the open instance only
  KB_CHECK_EQ(fake::Calls("HAL_ADC_IRQHandler"), 1U);
  fake::Hook("HAL_ADC_Start_IT", ReenterRead, &ch);
  KB_CHECK(ch.ReadRaw().IsOk());
  fake::Fail("HAL_ADC_DeInit", HAL_ERROR);
  KB_CHECK_EQ(ch.Release(), Status::Fault);
}

KB_TEST("adc: the instance is initialised once and freed by its last channel, in any order") {
  Fresh();
  stm::AdcChannel a{Adc(1U)};
  stm::AdcConfig cb = Adc(2U);
  cb.pin = PinId{};
  stm::AdcChannel b{cb};
  KB_REQUIRE(a.EnsureReady() == Status::Ok);
  KB_REQUIRE(b.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_ADC_Init"), 1U);
  KB_CHECK_EQ(a.Release(), Status::Ok); // the first opener leaves: b still converts
  KB_CHECK_EQ(fake::Calls("HAL_ADC_DeInit"), 0U);
  KB_CHECK(b.ReadRaw().IsOk());
  KB_CHECK_EQ(b.Release(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_ADC_DeInit"), 1U);
  fake::Fail("HAL_ADC_Init", HAL_ERROR);
  KB_CHECK_EQ(a.EnsureReady(), Status::Fault); // a failed open leaves the instance closed
  KB_REQUIRE(b.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_ADC_Init"), 3U);
}

KB_TEST("adc: the H7 asynchronous divider meets the ADC clock limit at any kernel clock") {
  bool ok = false;
  static_assert(stm::kAdcAsyncDividers.size() == 12U);
  KB_CHECK_EQ(stm::AdcAsyncDividerIndex(36000000U, stm::kH7AdcMaxHz, ok), 0U); // /1
  KB_CHECK(ok);
  KB_CHECK_EQ(stm::AdcAsyncDividerIndex(64000000U, stm::kH7AdcMaxHz, ok), 1U);  // /2: 32 MHz
  KB_CHECK_EQ(stm::AdcAsyncDividerIndex(200000000U, stm::kH7AdcMaxHz, ok), 3U); // /6: 33.3 MHz
  KB_CHECK_EQ(stm::AdcAsyncDividerIndex(480000000U, stm::kH7AdcMaxHz, ok), 7U); // /16: 30 MHz
  KB_CHECK(ok);
  static_cast<void>(stm::AdcAsyncDividerIndex(0U, stm::kH7AdcMaxHz, ok));
  KB_CHECK(!ok); // the kernel clock not running
  static_cast<void>(stm::AdcAsyncDividerIndex(4000000000U, 1000000U, ok));
  KB_CHECK(!ok); // even /256 too fast
  for (uint32_t hz = 1000000U; hz <= 550000000U; hz += 7000000U) {
    const std::size_t i = stm::AdcAsyncDividerIndex(hz, stm::kH7AdcMaxHz, ok);
    KB_CHECK(ok && ((hz / stm::kAdcAsyncDividers[i]) <= stm::kH7AdcMaxHz));
    KB_CHECK((i == 0U) || ((hz / stm::kAdcAsyncDividers[i - 1U]) > stm::kH7AdcMaxHz)); // smallest
  }
}

KB_TEST("adc: a vector none of whose instances the port has open is switched off") {
  Fresh();
  const IRQn_Type irq = kF4 ? ADC_IRQn : ADC3_IRQn;
  HAL_NVIC_EnableIRQ(irq);
  stm::HandleAdcIrq(3U, 3U);
  KB_CHECK(!fake::IrqEnabled(irq));
}
