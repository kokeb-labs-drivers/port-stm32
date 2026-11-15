// Clock (the 64-bit extension of a 32-bit timer) and BusyWait (DWT). Test code.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

KB_TEST("clock: an update interrupt of a timer the port's clock does not run on is switched off") {
  Fresh();
  // TIM5 has no Clock yet in this test binary (the clock tests below): another user's.
  HAL_NVIC_EnableIRQ(TIM5_IRQn);
  stm::HandleClockIrq(5U);
  KB_CHECK(!fake::IrqEnabled(TIM5_IRQn));
}

KB_TEST("clock: Start configures the 32-bit timer at 1 MHz with its wrap interrupt") {
  Fresh();
  const stm::Clock before{};
  KB_CHECK(before.Now().Us() == 0U); // not started yet
  stm::ClockConfig cfg{};
  cfg.timer = 2U;
  cfg.irq_priority = 2U;
  stm::Clock clock{cfg};
  kb_fake_tim[2].CNT = 4000000000U; // left running by a loader before a jump
  fake::SetCounterStep(0U);
  KB_CHECK_EQ(clock.Start(), Status::Ok);
  KB_CHECK(kb_fake_tim[2].CNT.value == 0U); // zeroed at start
  KB_CHECK_EQ(clock.Start(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_Base_Init"), 1U);
  KB_CHECK_EQ(fake::TimInit(2).Prescaler, 159U); // 160 MHz timer clock
  KB_CHECK_EQ(fake::TimInit(2).Period, 0xFFFFFFFFU);
  KB_CHECK(fake::IrqEnabled(TIM2_IRQn));
  KB_CHECK_EQ(fake::IrqPriority(TIM2_IRQn), 2U);
  kb_fake_tim[2].CNT = 1234U;
  KB_CHECK(clock.Now().Us() == 1234U);
  kb_fake_tim[2].SR = TIM_FLAG_UPDATE;
  stm::HandleClockIrq(2U);
  KB_CHECK_EQ(kb_fake_tim[2].SR, 0U);
  KB_CHECK(clock.Now().Us() == ((uint64_t{1U} << 32U) | 1234U));
  kb_fake_tim[2].SR = TIM_FLAG_UPDATE; // wrapped, interrupt not run yet
  kb_fake_tim[2].CNT = 3U;
  KB_CHECK(clock.Now().Us() == ((uint64_t{2U} << 32U) | 3U));
  stm::PwmConfig pwm{};
  pwm.timer = 2U; // the clock's timer
  pwm.channel = 1U;
  pwm.pin = PinId{GpioPort::A, 0U, 1U};
  stm::PwmChannel on_clock{pwm};
  KB_CHECK_EQ(on_clock.EnsureReady(), Status::Busy); // refused: it would stop the clock
  KB_CHECK(FailedWith(on_clock.LastError(), Status::Busy, stm::port_code::kOwned));
  KB_CHECK(clock.Now().Us() == ((uint64_t{2U} << 32U) | 3U));
  stm::HandleClockIrq(2U);
  stm::HandleClockIrq(2U); // no flag: nothing counted
  KB_CHECK(clock.Now().Us() == ((uint64_t{2U} << 32U) | 3U));
  stm::HandleClockIrq(30U); // not a timer
  KB_CHECK(clock.LastError().IsOk());
}

KB_TEST("clock: TIM5, a given timer clock, refusals and failures") {
  Fresh();
  stm::ClockConfig cfg{};
  cfg.timer = 5U;
  cfg.timer_clock_hz = 84000000U;
  stm::Clock clock{cfg};
  fake::Fail("HAL_TIM_Base_Init", HAL_ERROR);
  KB_CHECK_EQ(clock.Start(), Status::Fault);
  fake::Fail("HAL_TIM_Base_Start", HAL_BUSY);
  KB_CHECK_EQ(clock.Start(), Status::Busy);
  KB_CHECK_EQ(clock.Start(), Status::Ok); // a failed start leaves the timer free
  KB_CHECK_EQ(fake::TimInit(5).Prescaler, 83U);
  KB_CHECK(fake::IrqEnabled(TIM5_IRQn));
  stm::ClockConfig bad{};
  bad.timer = 3U; // 16-bit
  stm::Clock wrong{bad};
  KB_CHECK_EQ(wrong.Start(), Status::InvalidArgument);
}

KB_TEST("busy wait: cycles at the core clock, rounded up; the DWT is enabled on first use") {
  static_assert(stm::BusyWait::Cycles(1U, 16000000U) == 16U);
  static_assert(stm::BusyWait::Cycles(1U, 1500000U) == 2U); // 1.5 cycles round up
  static_assert(stm::BusyWait::Cycles(999U, 480000000U) == 479520U);
  Fresh();
  stm::BusyWait wait{};
  wait.WaitUs(kb::Microseconds{100U});
  KB_CHECK(fake::CyclesRead() >= 1600U);
  KB_CHECK((kb_fake_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
  KB_CHECK((kb_fake_coredebug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0U);
  fake::SetCycleStep(0x10000000U);
  SystemCoreClock = 480000000U;
  wait.WaitUs(kb::Microseconds{4000000U}); // long waits are split, the counter never wraps a step
  KB_CHECK(fake::CyclesRead() >= (uint64_t{4U} * 480000000U));
}
