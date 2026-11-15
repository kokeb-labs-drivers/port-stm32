// PwmChannel over the fake HAL timers (the period, the compare, the idle level), with every
// refusal. Test code. SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace {
using kb::iface::Duty;
constexpr int kPortB = 1;

stm::PwmConfig Pwm() {
  stm::PwmConfig c{};
  c.timer = 3U;
  c.channel = 2U;
  c.pin = PinId{GpioPort::B, 5U, 2U};
  c.frequency_hz = 1000U;
  return c;
}
} // namespace

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
  d.frequency_hz = 2000U; // the timer's period
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

KB_TEST("pwm: one period per timer; the clock's timer is refused") {
  Fresh();
  stm::PwmChannel first{Pwm()};
  KB_REQUIRE(first.EnsureReady() == Status::Ok);
  stm::PwmConfig other = Pwm();
  other.channel = 3U;
  other.pin = PinId{GpioPort::B, 0U, 2U};
  other.frequency_hz = 2000U; // another period on the same timer would corrupt the first's duty
  stm::PwmChannel conflicting{other};
  KB_CHECK_EQ(conflicting.EnsureReady(), Status::InvalidArgument);
  KB_CHECK(FailedWith(conflicting.LastError(), Status::InvalidArgument, stm::port_code::kConflict));
  other.frequency_hz = 1000U;
  stm::PwmChannel same{other};
  KB_REQUIRE(same.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_PWM_Init"), 1U);
  KB_CHECK_EQ(same.Reload(), first.Reload());
  KB_CHECK_EQ(first.SetFrequencyHz(3000U), Status::InvalidArgument); // shared: fixed
  KB_CHECK_EQ(same.Release(), Status::Ok);
  KB_CHECK_EQ(first.SetFrequencyHz(3000U), Status::Ok); // alone again: free to change
  KB_CHECK_EQ(first.Release(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_PWM_DeInit"), 1U);
}
